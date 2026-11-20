from __future__ import annotations
import ctypes as C
import gzip
import logging
from logging.handlers import RotatingFileHandler
import struct
import sys
import threading
import time
from collections import deque
from pathlib import Path
import numpy as np
import sounddevice as sd
from PySide6.QtWidgets import QMessageBox
from app.engine import VgmEngine
from app.scope_history import ScopeHistory


def read_gd3(path):
    with open(path, 'rb') as f:
        compressed = f.read(2) == b'\x1f\x8b'
    with (gzip.open if compressed else open)(path, 'rb') as f:
        raw = f.read(64*1024*1024+1)
    if len(raw)>64*1024*1024:
        raise ValueError('Plik przekracza limit odczytu tagow 64 MiB')
    if len(raw)<0x18 or raw[:4]!=b'Vgm ':
        raise ValueError('Nieprawidlowy naglowek VGM')
    offset=struct.unpack_from('<I',raw,0x14)[0]
    if not offset: return {}
    pos=0x14+offset
    if pos+12>len(raw) or raw[pos:pos+4]!=b'Gd3 ':
        raise ValueError('Nieprawidlowy blok GD3')
    size=struct.unpack_from('<I',raw,pos+8)[0]
    if size%2 or pos+12+size>len(raw): raise ValueError('Niepelny blok GD3')
    values=raw[pos+12:pos+12+size].decode('utf-16-le').split('\0')
    keys=('title','title_jp','game','game_jp','system','system_jp','composer','composer_jp','date','ripper','notes')
    return dict(zip(keys,values))


class LibVgmEngine(VgmEngine):
    def __init__(self, dll_path=None):
        base=Path(sys._MEIPASS) if getattr(sys,'frozen',False) else Path(__file__).resolve().parent.parent
        self.lib=C.CDLL(str(Path(dll_path) if dll_path else base/'vgm_engine.dll'))
        H,I,F=C.c_void_p,C.c_int,C.c_float
        signatures={
            'vgm_open':([C.c_char_p,C.POINTER(H)],I),'vgm_close':([H],None),
            'vgm_play':([H],I),'vgm_pause':([H],I),'vgm_stop':([H],I),
            'vgm_get_channel_count':([H],I),
            'vgm_get_channel_name':([H,I,C.POINTER(C.c_char),I],I),
            'vgm_get_mix_samples':([H,C.POINTER(F),I],I),
            'vgm_get_channel_samples':([H,I,C.POINTER(F),I],I),
            'vgm_set_channel_mute':([H,I,I],I),'vgm_set_volume':([H,F],I)}
        for name,(args,result) in signatures.items():
            f=getattr(self.lib,name); f.argtypes=args; f.restype=result
        self.handle=None; self.current_path=None; self.title=''; self.metadata={}
        self.channel_count=0; self.buffer_size=512; self._stream=None
        self._lock=threading.RLock(); self._callback_error=None; self._volume=0.8
        self._history=ScopeHistory(0); self._display_time=-float('inf')
        self._mix=np.zeros((512,2),dtype=np.float32)
        self._channels=np.zeros((0,512),dtype=np.float32)
        self._durations=deque(maxlen=512); self._events=deque(maxlen=64)
        self._callbacks=0; self._status_count=0; self._overruns=0
        self._max_ms=0.0; self._last_report=0.0; self._output_latency=0.0
        self.log_path=Path.home()/'VGM-Scope-audio.log'
        self._logger=logging.getLogger('VGM-Scope.audio')
        self._logger.setLevel(logging.INFO)
        self._logger.propagate=False
        if not self._logger.handlers:
            try:
                handler=RotatingFileHandler(self.log_path,maxBytes=1000000,backupCount=1,encoding='utf-8')
                handler.setFormatter(logging.Formatter('%(asctime)s %(message)s'))
                self._logger.addHandler(handler)
            except OSError:
                self._logger.addHandler(logging.NullHandler())

    def _check(self,code,operation):
        if code!=0: raise RuntimeError(f'{operation}: kod libvgm {code}')

    def _close_stream(self):
        stream,self._stream=self._stream,None
        if stream is not None:
            try:
                if stream.active: self._display_time=stream.time
                stream.stop()
            finally:
                stream.close()

    def load(self,path):
        self._close_stream()
        with self._lock:
            if self.handle: self.lib.vgm_close(self.handle)
            self.handle=None; self.current_path=None
            handle=C.c_void_p()
            self._check(self.lib.vgm_open(str(path).encode('utf-8'),C.byref(handle)),'Otwieranie pliku')
            self.handle=handle
            count=self.lib.vgm_get_channel_count(handle)
            if count<0: raise RuntimeError(f'Lista kanalow: kod {count}')
            self.channel_count=count; self.current_path=Path(path)
            self._history=ScopeHistory(count)
            self._channels=np.zeros((count,512),dtype=np.float32)
            self._callback_error=None; self._display_time=-float('inf')
            try: self.metadata=read_gd3(path)
            except Exception as exc: self.metadata={'warning':str(exc)}
            m=self.metadata
            title=m.get('title') or m.get('title_jp') or self.current_path.stem
            game=m.get('game') or m.get('game_jp') or 'brak tagu'
            composer=m.get('composer') or m.get('composer_jp') or 'brak tagu'
            system=m.get('system') or m.get('system_jp') or 'brak tagu'
            self.title=f'{title}\nGra: {game}\nKompozytor: {composer}\nSystem: {system}'
            if m.get('warning'): self.title+='\nGD3: '+m['warning']
            # Native gain stays at unity for both mix and isolated channels.
            self._check(self.lib.vgm_set_volume(handle,1.0),'Glosnosc')

    def play(self):
        try:
            if not self.handle: raise RuntimeError('Nie zaladowano utworu')
            if self._stream is not None and self._stream.active: return
            self._close_stream(); self._callback_error=None
            sd.check_output_settings(samplerate=48000,channels=2,dtype='float32')
            with self._lock:
                self._check(self.lib.vgm_play(self.handle),'Start dekodera')
                self._history=ScopeHistory(self.channel_count)
                self._display_time=-float('inf')
                self._durations.clear(); self._events.clear()
                self._callbacks=0; self._status_count=0; self._overruns=0; self._max_ms=0.0
            self._stream=sd.OutputStream(samplerate=48000,channels=2,dtype='float32',blocksize=512,latency='high',callback=self._audio_callback)
            self._output_latency=float(self._stream.latency)
            self._logger.info('Start: %s; output latency %.2f ms; block 512/48000',self.current_path,self._output_latency*1000)
            self._stream.start()
        except Exception as exc:
            self._close_stream()
            QMessageBox.critical(None,'VGM Scope - blad Play',str(exc))

    def pause(self):
        self._close_stream()
        with self._lock:
            if self.handle: self._check(self.lib.vgm_pause(self.handle),'Pauza')

    def stop(self):
        self._close_stream()
        with self._lock:
            if self.handle: self._check(self.lib.vgm_stop(self.handle),'Stop')
            self._history=ScopeHistory(self.channel_count)
            self._display_time=-float('inf')

    def toggle(self):
        if self._stream is not None and self._stream.active: self.pause()
        else: self.play()

    def set_volume(self,value):
        with self._lock:
            self._volume=max(0.0,min(1.0,value/100.0))

    def channel_names(self):
        names=[]
        with self._lock:
            for i in range(self.channel_count):
                buf=C.create_string_buffer(512)
                self._check(self.lib.vgm_get_channel_name(self.handle,i,buf,len(buf)),'Nazwa kanalu')
                names.append(buf.value.decode('utf-8',errors='replace'))
        return names

    def _audio_callback(self,outdata,frames,time_info,status):
        started=time.perf_counter()
        outdata.fill(0)
        try:
            with self._lock:
                if not self.handle: return
                if frames!=512: raise RuntimeError(f'Nieoczekiwany blok audio: {frames}')
                if status:
                    self._status_count+=1
                    self._events.append((float(time_info.currentTime),str(status)))
                ptr=C.POINTER(C.c_float)
                self._check(self.lib.vgm_get_mix_samples(self.handle,self._mix.ctypes.data_as(ptr),frames),'Renderowanie miksu')
                np.multiply(self._mix,self._volume,out=outdata)
                for ch in range(self.channel_count):
                    self._check(self.lib.vgm_get_channel_samples(self.handle,ch,self._channels[ch].ctypes.data_as(ptr),frames),f'Renderowanie kanalu {ch}')
                self._history.write(self._channels,float(time_info.outputBufferDacTime))
        except Exception as exc:
            self._callback_error=str(exc); outdata.fill(0)
            raise sd.CallbackAbort
        finally:
            elapsed=time.perf_counter()-started
            self._durations.append(elapsed*1000)
            self._callbacks+=1; self._max_ms=max(self._max_ms,elapsed*1000)
            if elapsed>frames/48000: self._overruns+=1

    def get_audio_diagnostics(self):
        with self._lock:
            durations=list(self._durations)
            return {'callbacks':self._callbacks,'mean_ms':sum(durations)/max(1,len(durations)),
                    'max_ms':self._max_ms,'deadline_ms':512/48,
                    'overruns':self._overruns,'status_count':self._status_count,
                    'events':list(self._events),'output_latency_ms':self._output_latency*1000}

    def get_channel_buffers(self):
        with self._lock:
            error,self._callback_error=self._callback_error,None
            if self._stream is not None and self._stream.active:
                self._display_time=float(self._stream.time)
            buffers=self._history.read(self._display_time)
        now=time.monotonic()
        if now-self._last_report>=5:
            self._last_report=now
            self._logger.info('Diagnostics: %s',self.get_audio_diagnostics())
        if error:
            self._logger.error('Audio abort: %s',error)
            self._close_stream()
            QMessageBox.critical(None,'VGM Scope - blad audio',error)
        return buffers

    def set_channel_mute(self,channel_index,muted):
        with self._lock:
            if self.handle: self._check(self.lib.vgm_set_channel_mute(self.handle,channel_index,int(muted)),'Mute')
