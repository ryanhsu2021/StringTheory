# -*- coding: utf-8 -*-
"""开始播放并连续抓帧：用于观察弦波的传播（空格触发，避免坐标点击）"""
import sys, time, ctypes
sys.path.insert(0, r'F:\WorkBuddy\DotSequencer\tools')
import wincap

user32 = ctypes.WinDLL('user32', use_last_error=True)

keyword = sys.argv[1] if len(sys.argv) > 1 else 'String Theory'
prefix = sys.argv[2] if len(sys.argv) > 2 else 'shots/frame'
gaps = [float(x) for x in (sys.argv[3].split(',') if len(sys.argv) > 3 else '0.30,0.25,0.25,0.30'.split(','))]

wins = wincap.find_windows(keyword)
if not wins:
    print('WINDOW_NOT_FOUND')
    sys.exit(1)

hwnd = wins[0][0]
wincap.focus(hwnd)
time.sleep(0.25)

user32.keybd_event(0x20, 0, 0, 0)
time.sleep(0.05)
user32.keybd_event(0x20, 0, 2, 0)

for i, g in enumerate(gaps):
    time.sleep(g)
    w, h, bgra = wincap.capture(hwnd)
    out = '%s_%d.png' % (prefix, i)
    wincap.write_png(out, w, h, bgra)
    print('saved', out, w, h)
