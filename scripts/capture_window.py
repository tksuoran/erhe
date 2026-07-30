#!/usr/bin/env python3
"""Capture a top-level window of a process into a PNG (Windows only).

Intended for judging windowed-editor rendering when the headless MCP
capture_screenshot is not available. POLICY (see AGENTS.md): an AI agent
must ALWAYS ask the user before capturing the windowed editor or any
other window - the user may be using the computer for something else,
the target window may be occluded, and the user must always be aware
when a non-headless capture is taken.

Usage:
  py -3 scripts/capture_window.py                          # editor.exe -> logs/window_capture.png
  py -3 scripts/capture_window.py --process editor.exe --out logs/foo.png
  py -3 scripts/capture_window.py --foreground             # bring window to front + screen-blit
                                                           # (needed for content PrintWindow cannot render)

Default capture path is PrintWindow with PW_RENDERFULLCONTENT, which
reads DWM-composited content without changing window z-order or focus.
--foreground raises the window and blits from the screen instead; it is
more likely to capture GPU swapchain content verbatim but disturbs
whatever the user is doing.
"""
import argparse
import ctypes
import struct
import sys
import time
import zlib
from ctypes import wintypes


user32   = ctypes.windll.user32
gdi32    = ctypes.windll.gdi32
kernel32 = ctypes.windll.kernel32


def enable_dpi_awareness() -> None:
    # Per-monitor v2 when available; without this GetWindowRect returns
    # virtualized coordinates on scaled displays and the blit is cropped.
    try:
        user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    except Exception:
        pass


def pids_for_process(name: str) -> set[int]:
    TH32CS_SNAPPROCESS = 0x00000002

    class PROCESSENTRY32W(ctypes.Structure):
        _fields_ = [
            ("dwSize",              wintypes.DWORD),
            ("cntUsage",            wintypes.DWORD),
            ("th32ProcessID",       wintypes.DWORD),
            ("th32DefaultHeapID",   ctypes.c_size_t),
            ("th32ModuleID",        wintypes.DWORD),
            ("cntThreads",          wintypes.DWORD),
            ("th32ParentProcessID", wintypes.DWORD),
            ("pcPriClassBase",      ctypes.c_long),
            ("dwFlags",             wintypes.DWORD),
            ("szExeFile",           ctypes.c_wchar * 260),
        ]

    snapshot = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snapshot == wintypes.HANDLE(-1).value:
        return set()
    pids: set[int] = set()
    entry = PROCESSENTRY32W()
    entry.dwSize = ctypes.sizeof(PROCESSENTRY32W)
    try:
        if kernel32.Process32FirstW(snapshot, ctypes.byref(entry)):
            while True:
                if entry.szExeFile.lower() == name.lower():
                    pids.add(int(entry.th32ProcessID))
                if not kernel32.Process32NextW(snapshot, ctypes.byref(entry)):
                    break
    finally:
        kernel32.CloseHandle(snapshot)
    return pids


def find_main_window(pids: set[int]) -> int | None:
    GW_OWNER = 4
    found: list[int] = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def on_window(hwnd, _lparam):
        if not user32.IsWindowVisible(hwnd):
            return True
        pid = wintypes.DWORD(0)
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if (pid.value in pids) and (user32.GetWindow(hwnd, GW_OWNER) == 0):
            found.append(int(hwnd) if not isinstance(hwnd, int) else hwnd)
        return True

    user32.EnumWindows(on_window, 0)
    return found[0] if found else None


def capture_window(hwnd: int, foreground: bool) -> tuple[int, int, bytes]:
    if foreground:
        user32.SetForegroundWindow(hwnd)
        time.sleep(0.7)

    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    width  = rect.right - rect.left
    height = rect.bottom - rect.top
    if (width <= 0) or (height <= 0):
        raise RuntimeError(f"bad window rect {width}x{height}")

    screen_dc = user32.GetDC(None)
    memory_dc = gdi32.CreateCompatibleDC(screen_dc)
    bitmap    = gdi32.CreateCompatibleBitmap(screen_dc, width, height)
    gdi32.SelectObject(memory_dc, bitmap)
    try:
        captured = False
        if not foreground:
            PW_RENDERFULLCONTENT = 0x00000002
            captured = bool(user32.PrintWindow(hwnd, memory_dc, PW_RENDERFULLCONTENT))
        if not captured:
            SRCCOPY = 0x00CC0020
            gdi32.BitBlt(memory_dc, 0, 0, width, height, screen_dc, rect.left, rect.top, SRCCOPY)

        class BITMAPINFOHEADER(ctypes.Structure):
            _fields_ = [
                ("biSize",          wintypes.DWORD),
                ("biWidth",         ctypes.c_long),
                ("biHeight",        ctypes.c_long),
                ("biPlanes",        wintypes.WORD),
                ("biBitCount",      wintypes.WORD),
                ("biCompression",   wintypes.DWORD),
                ("biSizeImage",     wintypes.DWORD),
                ("biXPelsPerMeter", ctypes.c_long),
                ("biYPelsPerMeter", ctypes.c_long),
                ("biClrUsed",       wintypes.DWORD),
                ("biClrImportant",  wintypes.DWORD),
            ]

        info = BITMAPINFOHEADER()
        info.biSize     = ctypes.sizeof(BITMAPINFOHEADER)
        info.biWidth    = width
        info.biHeight   = -height  # negative = top-down rows
        info.biPlanes   = 1
        info.biBitCount = 32
        info.biCompression = 0  # BI_RGB

        buffer = ctypes.create_string_buffer(width * height * 4)
        DIB_RGB_COLORS = 0
        rows = gdi32.GetDIBits(memory_dc, bitmap, 0, height, buffer, ctypes.byref(info), DIB_RGB_COLORS)
        if rows != height:
            raise RuntimeError(f"GetDIBits returned {rows} of {height} rows")
        return width, height, buffer.raw
    finally:
        gdi32.DeleteObject(bitmap)
        gdi32.DeleteDC(memory_dc)
        user32.ReleaseDC(None, screen_dc)


def write_png(path: str, width: int, height: int, bgra: bytes) -> None:
    # Vectorized BGRA -> RGB via strided slicing (a per-pixel Python loop is
    # seconds-slow at editor resolutions).
    rgb = bytearray(width * height * 3)
    rgb[0::3] = bgra[2::4]
    rgb[1::3] = bgra[1::4]
    rgb[2::3] = bgra[0::4]
    stride = width * 3
    raw = b"".join(
        b"\x00" + bytes(rgb[y * stride:(y + 1) * stride])
        for y in range(height)
    )

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
        )

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # 8-bit RGB
    with open(path, "wb") as file:
        file.write(b"\x89PNG\r\n\x1a\n")
        file.write(chunk(b"IHDR", header))
        file.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        file.write(chunk(b"IEND", b""))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--process",    default="editor.exe", help="process image name (default: editor.exe)")
    parser.add_argument("--out",        default="logs/window_capture.png", help="output PNG path")
    parser.add_argument("--foreground", action="store_true", help="raise the window and blit from screen instead of PrintWindow")
    arguments = parser.parse_args()

    if sys.platform != "win32":
        print("capture_window.py is Windows only", file=sys.stderr)
        return 1

    enable_dpi_awareness()
    pids = pids_for_process(arguments.process)
    if not pids:
        print(f"no running process named {arguments.process}", file=sys.stderr)
        return 1
    hwnd = find_main_window(pids)
    if hwnd is None:
        print(f"{arguments.process} has no visible top-level window", file=sys.stderr)
        return 1

    width, height, pixels = capture_window(hwnd, arguments.foreground)
    write_png(arguments.out, width, height, pixels)
    print(f"saved {arguments.out} ({width} x {height})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
