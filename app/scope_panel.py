from __future__ import annotations
import time
import numpy as np
from PySide6.QtCore import Signal
from PySide6.QtGui import QPainter, QColor, QPen, QPainterPath
from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QLineEdit, QComboBox, QCheckBox


class ScopeCanvas(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.samples=np.empty(0,dtype=np.float32)
        self.window_ms=20; self.gain=1.0; self.auto=False; self.trigger=True
        self._auto_gain=1.0; self._updated=time.monotonic()
        self.setMinimumHeight(100)

    def set_samples(self, samples):
        self.samples=np.asarray(samples,dtype=np.float32)
        self.update()

    def paintEvent(self, event):
        p=QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        p.fillRect(self.rect(),QColor(12,12,16))
        w,h=self.width(),self.height()
        p.setPen(QPen(QColor(45,45,55),1))
        p.drawLine(0,h//2,w,h//2)
        for division in range(1,4):
            x=int(w*division/4); p.drawLine(x,0,x,h)
        count=int(self.window_ms*48)
        raw=self.samples
        locked=False
        if raw.size>=count:
            start=raw.size-count
            if self.trigger and raw.size>=count+2:
                low=max(0,start-count)
                section=raw[low:start+1]
                peak=float(np.max(np.abs(section))) if section.size else 0.0
                armed=False; crossing=None
                if peak>1e-5:
                    for i in range(low+1,start+1):
                        if raw[i-1]<-peak*0.03: armed=True
                        if armed and raw[i-1]<=0<raw[i]:
                            crossing=i-1+float(-raw[i-1]/(raw[i]-raw[i-1]))
                            armed=False
                if crossing is not None and crossing+count-1<=raw.size-1:
                    raw=np.interp(crossing+np.arange(count),np.arange(raw.size),raw)
                    locked=True
                else:
                    raw=raw[start:]
            else:
                raw=raw[start:]
        else:
            raw=np.pad(raw,(count-raw.size,0))
        peak=float(np.max(np.abs(raw))) if raw.size else 0.0
        now=time.monotonic(); dt=max(0.0,now-self._updated); self._updated=now
        target=min(64.0,0.8/peak) if peak>1e-4 else 1.0
        if target<self._auto_gain: self._auto_gain=target
        else: self._auto_gain+=(target-self._auto_gain)*(1-np.exp(-dt/0.4))
        gain=self._auto_gain if self.auto else self.gain
        values=raw*gain
        clipped=bool(np.any(np.abs(values)>1.0))
        y=h/2-np.clip(values,-1.0,1.0)*(h/2-18)
        p.setPen(QPen(QColor(0,230,180),1.2))
        if len(y)>w and w>0:
            edges=np.linspace(0,len(y),w+1,dtype=int)
            for x in range(w):
                part=y[edges[x]:edges[x+1]]
                if part.size: p.drawLine(x,int(part.min()),x,int(part.max()))
        elif len(y)>1:
            path=QPainterPath(); path.moveTo(0,float(y[0]))
            for i in range(1,len(y)):
                path.lineTo(i*(w-1)/(len(y)-1),float(y[i]))
            p.drawPath(path)
        p.setPen(QColor(255,160,100) if clipped else QColor(180,180,190))
        mode='AUTO' if self.auto else 'GAIN'
        trig=('TRIG' if locked else 'WAIT') if self.trigger else 'FREE'
        suffix=' DISPLAY CLIP' if clipped else ''
        p.drawText(5,14,f'{self.window_ms} ms | {mode} x{gain:.2f} | {trig} | peak {peak:.3f}{suffix}')


class ScopePanel(QWidget):
    label_edited=Signal(int,str)
    mute_toggled=Signal(int,bool)
    solo_toggled=Signal(int,bool)

    def __init__(self, channel_index, label, parent=None):
        super().__init__(parent)
        self.channel_index=channel_index; self.is_muted=False
        self.setStyleSheet('QWidget { background: #101014; border: 1px solid #26262e; border-radius: 6px; }')
        layout=QVBoxLayout(self); layout.setContentsMargins(8,6,8,6); layout.setSpacing(4)
        top=QHBoxLayout()
        self.label_edit=QLineEdit(label)
        self.label_edit.setStyleSheet('QLineEdit { background: transparent; border: none; color: #d8d8e0; font-weight: 600; }')
        self.label_edit.editingFinished.connect(self._emit_label)
        top.addWidget(self.label_edit); top.addStretch()
        self.mute_button=QPushButton('M'); self.mute_button.setCheckable(True)
        self.mute_button.setFixedSize(28,24); self.mute_button.setToolTip('Mute')
        self.mute_button.toggled.connect(self._mute_changed)
        self.solo_button=QPushButton('S'); self.solo_button.setCheckable(True)
        self.solo_button.setFixedSize(28,24)
        self.solo_button.setToolTip('Solo: kliknij ponownie, aby przywrocic wszystkie kanaly')
        self.solo_button.toggled.connect(self._solo_changed)
        top.addWidget(self.mute_button); top.addWidget(self.solo_button)
        self.canvas=ScopeCanvas()
        controls=QHBoxLayout()
        self.timebase=QComboBox()
        for ms in (10,20,40): self.timebase.addItem(f'{ms} ms',ms)
        self.timebase.setCurrentIndex(1)
        self.timebase.setToolTip('Czas calej szerokosci wykresu')
        self.timebase.currentIndexChanged.connect(self._display_changed)
        self.gain_box=QComboBox()
        for gain in (0.25,0.5,1,2,4,8,16): self.gain_box.addItem(f'x{gain}',gain)
        self.gain_box.setCurrentIndex(2)
        self.gain_box.setToolTip('Wzmocnienie tylko obrazu, bez zmiany dzwieku')
        self.gain_box.currentIndexChanged.connect(self._display_changed)
        self.auto_box=QCheckBox('AUTO'); self.auto_box.setChecked(False)
        self.auto_box.setToolTip('Normalizacja obrazu: wysokosc nie pokazuje wtedy wzglednej glosnosci kanalow')
        self.auto_box.toggled.connect(self._display_changed)
        self.trigger_box=QCheckBox('TRIG'); self.trigger_box.setChecked(True)
        self.trigger_box.setToolTip('Wyzwalanie: narastajace przejscie przez zero; opoznienie wyboru fazy do jednej szerokosci okna')
        self.trigger_box.toggled.connect(self._display_changed)
        for control in (self.timebase,self.gain_box,self.auto_box,self.trigger_box): controls.addWidget(control)
        layout.addLayout(top); layout.addLayout(controls); layout.addWidget(self.canvas)

    def _display_changed(self,*args):
        self.canvas.window_ms=int(self.timebase.currentData())
        self.canvas.gain=float(self.gain_box.currentData())
        self.canvas.auto=self.auto_box.isChecked()
        self.canvas.trigger=self.trigger_box.isChecked()
        self.gain_box.setEnabled(not self.canvas.auto)
        self.canvas.update()

    def _emit_label(self):
        self.label_edited.emit(self.channel_index,self.label_edit.text())

    def _mute_changed(self,checked):
        self.is_muted=checked; self.mute_toggled.emit(self.channel_index,checked)

    def _solo_changed(self,checked):
        self.solo_toggled.emit(self.channel_index,checked)

    def set_muted(self,muted):
        self.is_muted=muted
        self.mute_button.blockSignals(True); self.mute_button.setChecked(muted); self.mute_button.blockSignals(False)

    def set_solo(self,solo):
        self.solo_button.blockSignals(True); self.solo_button.setChecked(solo); self.solo_button.blockSignals(False)

    def set_samples(self,samples):
        self.canvas.set_samples(samples)
