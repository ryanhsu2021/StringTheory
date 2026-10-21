# -*- coding: utf-8 -*-
"""启动 String Theory -> 等窗口 -> 点击 V1 TIMBRE 展开菜单 -> 整屏截图"""
import sys, time, ctypes, subprocess
from ctypes import wintypes
sys.path.insert(0, r'F:\WorkBuddy\DotSequencer\tools')
import wincap

user32 = ctypes.WinDLL('user32', use_last_error=True)
user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
user32.ClientToScreen.restype = wintypes.BOOL

EXE = r'F:\WorkBuddy\StringTheory\build\StringTheory_artefacts\Release\Standalone\String Theory.exe'
subprocess.Popen(['taskkill', '/F', '/IM', 'String Theory.exe'],
                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(1.0)
subprocess.Popen([EXE])
time.sleep(6.0)

wins = wincap.find_windows('String Theory')
if not wins:
    print('WINDOW_NOT_FOUND'); sys.exit(1)
hwnd = wins[0][0]
wincap.focus(hwnd)
time.sleep(0.4)

# move the window to the screen origin + topmost, so the region grab below
# actually contains it (multi-monitor / behind-other-windows safe)
user32.SetWindowPos(hwnd, -1, 10, 10, 0, 0, 0x0001 | 0x0004 | 0x0040)  # NOSIZE|NOZORDER? -> SWP_NOSIZE=0x1, SWP_SHOWWINDOW=0x40
time.sleep(0.6)

rect = wintypes.RECT()
user32.GetWindowRect(hwnd, ctypes.byref(rect))
print('window rect', rect.left, rect.top, rect.right, rect.bottom)

# V1 timbre box (client pixels). SendInput clicks get clamped to the screen
# edge on this tall window, so post the click straight to the window proc.
wincap.client_click(hwnd, 160, 848)
time.sleep(1.2)

gdi32 = ctypes.WinDLL('gdi32')
x, y = rect.left, rect.top
w, h = rect.right - rect.left + 420, rect.bottom - rect.top + 60
hdc_screen = user32.GetDC(0)
hdc_mem = gdi32.CreateCompatibleDC(hdc_screen)
hbm = gdi32.CreateCompatibleBitmap(hdc_screen, w, h)
gdi32.SelectObject(hdc_mem, hbm)
gdi32.BitBlt(hdc_mem, 0, 0, w, h, hdc_screen, x, y, 0x00CC0020)

# minimal BMP writer
import struct
class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [('biSize', ctypes.c_uint32), ('biWidth', ctypes.c_int32),
                ('biHeight', ctypes.c_int32), ('biPlanes', ctypes.c_uint16),
                ('biBitCount', ctypes.c_uint16), ('biCompression', ctypes.c_uint32),
                ('biSizeImage', ctypes.c_uint32), ('biXPelsPerMeter', ctypes.c_int32),
                ('biYPelsPerMeter', ctypes.c_int32), ('biClrUsed', ctypes.c_uint32),
                ('biClrImportant', ctypes.c_uint32)]
bmi = BITMAPINFOHEADER()
bmi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
bmi.biWidth = w; bmi.biHeight = -h; bmi.biPlanes = 1; bmi.biBitCount = 32
bmi.biCompression = 0
buf = ctypes.create_string_buffer(w * h * 4)
gdi32.GetDIBits(hdc_screen, hbm, 0, h, buf, ctypes.byref(bmi), 0)
row = w * 4
bmp_path = r'F:\WorkBuddy\StringTheory\shots\menu_open.bmp'
data = buf.raw
filesize = 54 + len(data)
with open(bmp_path, 'wb') as f:
    f.write(b'BM'); f.write(struct.pack('<IHHI', filesize, 0, 0, 54))
    f.write(bytes(bmi)); f.write(data)
print('DONE', bmp_path)
