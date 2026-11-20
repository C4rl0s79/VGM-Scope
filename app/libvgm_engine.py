from __future__ import annotations
import ctypes as C
import gzip
import struct
import sys
import threading
from pathlib import Path
import sounddevice as sd
from PySide6.QtWidgets import QMessageBox
from app.engine import VgmEngine


def read_gd3(path):
    with open(path, 'rb') as f:
        compressed = f.read(2) == b'\x1f\x8b'
    opener = gzip.open if compressed else open
    with opener(path, 'rb') as f:
        raw = f.read(64*1024*1024+1)
    if len(raw)>64*1024*1024:
        raise ValueError('Plik przekracza limit odczytu tagów 64 MiB')
    if len(raw)<0x18 or raw[:4]!=b'Vgm ':
        raise ValueError('Nieprawidłowy nagłówek VGM')
    offset=struct.unpack_from('<I',raw,0x14)[0]
    if not offset: return {}
    pos=0x14+offset
    if pos+12>len(raw) or raw[pos:pos+4]!=b'Gd3 ':
        raise ValueError('Nieprawidłowy blok GD3')
    size=struct.unpack_from('<I',raw,pos+8)[0]
    if size%2 or pos+12+size>len(raw): raise ValueError('Niepełny blok GD3')
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
        self._lock=threading.RLock(); self._channel_buffers=[]
        self._callback_error=None; self._volume=0.8

    def _check(self,code,operation):
        if code!=0: raise RuntimeError(f'{operation}: kod libvgm {code}')

    def _close_stream(self):
        stream,self._stream=self._stream,None
        if stream is not None:
            try: stream.stop()
            finally: stream.close()

    def load(self,path):
        self._close_stream()
        with self._lock:
            if self.handle: self.lib.vgm_close(self.handle)
            self.handle=None; self.current_path=None
            handle=C.c_void_p()
            self._check(self.lib.vgm_open(str(path).encode('utf-8'),C.byref(handle)),'Otwieranie pliku')
            self.handle=handle
            count=self.lib.vgm_get_channel_count(handle)
            if count<0: raise RuntimeError(f'Lista kanałów: kod {count}')
            self.channel_count=count; self.current_path=Path(path)
            self._channel_buffers=[[] for _ in range(count)]; self._callback_error=None
            try: self.metadata=read_gd3(path)
            except Exception as exc: self.metadata={'warning':str(exc)}
            m=self.metadata
            title=m.get('title') or m.get('title_jp') or self.current_path.stem
            game=m.get('game') or m.get('game_jp') or 'brak tagu'
            composer=m.get('composer') or m.get('composer_jp') or 'brak tagu'
            system=m.get('system') or m.get('system_jp') or 'brak tagu'
            self.title=f'{title}\nGra: {game}\nKompozytor: {composer}\nSystem: {system}'
            if m.get('warning'): self.title+='\nGD3: '+m['warning']
            self._check(self.lib.vgm_set_volume(handle,self._volume),'Głośność')

    def play(self):
        try:
            if not self.handle: raise RuntimeError('Nie załadowano utworu')
            if self._stream is not None and self._stream.active: return
            self._close_stream(); self._callback_error=None
            sd.check_output_settings(samplerate=48000,channels=2,dtype='float32')
            with self._lock: self._check(self.lib.vgm_play(self.handle),'Start dekodera')
            self._stream=sd.OutputStream(samplerate=48000,channels=2,dtype='float32',blocksize=512,latency='high',callback=self._audio_callback)
            self._stream.start()
        except Exception as exc:
            self._close_stream()
            QMessageBox.critical(None,'VGM Scope — błąd Play',str(exc))

    def pause(self):
        self._close_stream()
        with self._lock:
            if self.handle: self._check(self.lib.vgm_pause(self.handle),'Pauza')

    def stop(self):
        self._close_stream()
        with self._lock:
            if self.handle: self._check(self.lib.vgm_stop(self.handle),'Stop')

    def toggle(self):
        if self._stream is not None and self._stream.active: self.pause()
        else: self.play()

    def set_volume(self,value):
        with self._lock:
            self._volume=value/100.0
            if self.handle: self._check(self.lib.vgm_set_volume(self.handle,self._volume),'Głośność')

    def channel_names(self):
        names=[]
        with self._lock:
            for i in range(self.channel_count):
                buf=C.create_string_buffer(512)
                self._check(self.lib.vgm_get_channel_name(self.handle,i,buf,len(buf)),'Nazwa kanału')
                names.append(buf.value.decode('utf-8',errors='replace'))
        return names

    def _audio_callback(self,outdata,frames,time_info,status):
        outdata.fill(0)
        try:
            with self._lock:
                if not self.handle: return
                mix=(C.c_float*(frames*2))()
                self._check(self.lib.vgm_get_mix_samples(self.handle,mix,frames),'Renderowanie miksu')
                for i in range(frames): outdata[i,0],outdata[i,1]=mix[i*2],mix[i*2+1]
                cache=[]
                for ch in range(self.channel_count):
                    buf=(C.c_float*frames)()
                    self._check(self.lib.vgm_get_channel_samples(self.handle,ch,buf,frames),f'Renderowanie kanału {ch}')
                    cache.append(list(buf))
                self._channel_buffers=cache
        except Exception as exc:
            self._callback_error=str(exc); outdata.fill(0)
            raise sd.CallbackAbort

    def get_channel_buffers(self):
        with self._lock:
            error,self._callback_error=self._callback_error,None
            buffers=[list(ch) for ch in self._channel_buffers]
        if error:
            self._close_stream()
            QMessageBox.critical(None,'VGM Scope — błąd audio',error)
        return buffers

    def set_channel_mute(self,channel_index,muted):
        with self._lock:
            if self.handle: self._check(self.lib.vgm_set_channel_mute(self.handle,channel_index,int(muted)),'Mute')
