#!/usr/bin/env python3
"""Capture one verified X11 client's offscreen surface. Never read the desktop."""
import array
import ctypes as C
import json
import os
from pathlib import Path
import select
import signal
import sys
import time
from PIL import Image

D, W = C.c_void_p, C.c_ulong


class Attributes(C.Structure):
    _fields_ = [('x', C.c_int), ('y', C.c_int), ('width', C.c_int), ('height', C.c_int), ('border_width', C.c_int), ('depth', C.c_int), ('visual', D), ('root', W), ('class_', C.c_int), ('bit_gravity', C.c_int), ('win_gravity', C.c_int), ('backing_store', C.c_int), ('backing_planes', W), ('backing_pixel', W), ('save_under', C.c_int), ('colormap', W), ('map_installed', C.c_int), ('map_state', C.c_int), ('all_event_masks', C.c_long), ('your_event_mask', C.c_long), ('do_not_propagate_mask', C.c_long), ('override_redirect', C.c_int), ('screen', D)]


class Visual(C.Structure):
    _fields_ = [('ext_data', D), ('visualid', W), ('class_', C.c_int), ('red_mask', W), ('green_mask', W), ('blue_mask', W), ('bits_per_rgb', C.c_int), ('map_entries', C.c_int)]


class XImage(C.Structure):
    _fields_ = [('width', C.c_int), ('height', C.c_int), ('xoffset', C.c_int), ('format', C.c_int), ('data', D), ('byte_order', C.c_int), ('bitmap_unit', C.c_int), ('bitmap_bit_order', C.c_int), ('bitmap_pad', C.c_int), ('depth', C.c_int), ('bytes_per_line', C.c_int), ('bits_per_pixel', C.c_int), ('red_mask', W), ('green_mask', W), ('blue_mask', W)]


class Cursor(C.Structure):
    _fields_ = [('x', C.c_short), ('y', C.c_short), ('width', C.c_ushort), ('height', C.c_ushort), ('xhot', C.c_ushort), ('yhot', C.c_ushort), ('serial', W), ('pixels', C.POINTER(W)), ('atom', W), ('name', C.c_char_p)]


def main(window, pid, start, draw_mouse):
    proc = Path('/proc') / str(pid)

    def identity():
        try:
            return pid > 1 and proc.stat().st_uid == os.getuid() and proc.joinpath('stat').read_text().rsplit(')', 1)[1].split()[19] == start
        except OSError:
            return False

    if window <= 0 or not identity():
        raise RuntimeError('Selected window process changed')
    x, co, fix, res = (C.CDLL(name) for name in ['libX11.so.6', 'libXcomposite.so.1', 'libXfixes.so.3', 'libXRes.so.1'])
    x.XOpenDisplay.argtypes = [C.c_char_p]; x.XOpenDisplay.restype = D
    x.XCloseDisplay.argtypes = [D]
    x.XGetWindowAttributes.argtypes = [D, W, C.POINTER(Attributes)]
    x.XGetGeometry.argtypes = [D, W, C.POINTER(W), C.POINTER(C.c_int), C.POINTER(C.c_int), C.POINTER(C.c_uint), C.POINTER(C.c_uint), C.POINTER(C.c_uint), C.POINTER(C.c_uint)]
    x.XGetImage.argtypes = [D, W, C.c_int, C.c_int, C.c_uint, C.c_uint, W, C.c_int]; x.XGetImage.restype = C.POINTER(XImage)
    x.XDestroyImage.argtypes = [D]
    x.XFree.argtypes = [D]
    x.XFreePixmap.argtypes = [D, W]
    x.XSync.argtypes = [D, C.c_int]
    x.XPending.argtypes = [D]
    x.XNextEvent.argtypes = [D, D]
    x.XSelectInput.argtypes = [D, W, C.c_long]
    x.XDefaultRootWindow.argtypes = [D]; x.XDefaultRootWindow.restype = W
    x.XGetInputFocus.argtypes = [D, C.POINTER(W), C.POINTER(C.c_int)]
    x.XTranslateCoordinates.argtypes = [D, W, W, C.c_int, C.c_int, C.POINTER(C.c_int), C.POINTER(C.c_int), C.POINTER(W)]
    co.XCompositeQueryVersion.argtypes = [D, C.POINTER(C.c_int), C.POINTER(C.c_int)]
    co.XCompositeRedirectWindow.argtypes = [D, W, C.c_int]
    co.XCompositeUnredirectWindow.argtypes = [D, W, C.c_int]
    co.XCompositeNameWindowPixmap.argtypes = [D, W]; co.XCompositeNameWindowPixmap.restype = W
    fix.XFixesGetCursorImage.argtypes = [D]; fix.XFixesGetCursorImage.restype = C.POINTER(Cursor)
    handler_type = C.CFUNCTYPE(C.c_int, D, D)
    handler = handler_type(lambda display, event: 0)
    x.XSetErrorHandler.argtypes = [handler_type]; x.XSetErrorHandler(handler)
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError('X11/XWayland display is unavailable')

    class Spec(C.Structure):
        _fields_ = [('client', W), ('mask', C.c_uint)]

    class Value(C.Structure):
        _fields_ = [('spec', Spec), ('length', C.c_long), ('value', D)]

    res.XResQueryClientIds.argtypes = [D, C.c_long, C.POINTER(Spec), C.POINTER(C.c_long), C.POINTER(C.POINTER(Value))]
    res.XResGetClientPid.argtypes = [C.POINTER(Value)]; res.XResGetClientPid.restype = C.c_int
    res.XResClientIdsDestroy.argtypes = [C.c_long, C.POINTER(Value)]

    def owner():
        spec, count, values = Spec(window, 2), C.c_long(), C.POINTER(Value)()
        res.XResQueryClientIds(display, 1, C.byref(spec), C.byref(count), C.byref(values))
        if not values:
            return False
        try:
            return any(res.XResGetClientPid(C.byref(values[i])) == pid for i in range(min(count.value, 64)))
        finally:
            res.XResClientIdsDestroy(count, values)

    pixmap, width, height, redirected, stopped = 0, 0, 0, False, False
    enabled, fps, refresh, last_check, next_frame, command_buffer, last_state = True, 60, True, 0, 0, b'', None

    def stop(*_):
        nonlocal stopped
        stopped = True

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)

    def state(hidden):
        nonlocal last_state
        value = dict(ready=True, hidden=hidden, retained=hidden, width=width, height=height)
        if value != last_state:
            print(json.dumps(value), file=sys.stderr, flush=True)
            last_state = value

    def renew():
        nonlocal pixmap, width, height, refresh
        new = co.XCompositeNameWindowPixmap(display, window)
        x.XSync(display, 0)
        root, gx, gy, w, h, border, depth = W(), C.c_int(), C.c_int(), C.c_uint(), C.c_uint(), C.c_uint(), C.c_uint()
        if not new or not x.XGetGeometry(display, new, C.byref(root), C.byref(gx), C.byref(gy), C.byref(w), C.byref(h), C.byref(border), C.byref(depth)):
            if new:
                x.XFreePixmap(display, new)
            return False
        if w.value < 4 or h.value < 4 or w.value > 8192 or h.value > 8192:
            x.XFreePixmap(display, new)
            raise RuntimeError('Unsupported selected surface size')
        old = pixmap
        pixmap, width, height, refresh = new, w.value, h.value, False
        if old:
            x.XFreePixmap(display, old)
        return True

    try:
        major, minor = C.c_int(), C.c_int()
        if not co.XCompositeQueryVersion(display, C.byref(major), C.byref(minor)) or (major.value, minor.value) < (0, 2):
            raise RuntimeError('Window capture requires XComposite 0.2 or newer')
        if not owner():
            raise RuntimeError('Selected window owner changed')
        co.XCompositeRedirectWindow(display, window, 0); redirected = True
        x.XSelectInput(display, window, 1 << 17); x.XSync(display, 0)
        if not renew():
            raise RuntimeError('Restore the game window once to initialize its offscreen surface')
        root = x.XDefaultRootWindow(display)
        while not stopped:
            now = time.monotonic()
            if now - last_check > .5:
                if not identity() or not owner():
                    raise RuntimeError('Selected game window closed or its owner changed')
                last_check = now
            event = (C.c_long * 24)()
            while x.XPending(display):
                x.XNextEvent(display, C.byref(event))
                kind = C.cast(C.byref(event), C.POINTER(C.c_int))[0]
                if kind == 17:
                    raise RuntimeError('Selected game window was destroyed')
                if kind in (19, 22):
                    refresh = True
            attributes = Attributes()
            if not x.XGetWindowAttributes(display, window, C.byref(attributes)):
                raise RuntimeError('Selected game window is unavailable')
            hidden = attributes.map_state != 2
            if refresh and not hidden:
                renew()
            state(hidden)
            if enabled and now >= next_frame:
                image = x.XGetImage(display, pixmap, 0, 0, width, height, C.c_ulong(-1).value, 2)
                if not image:
                    refresh = True
                    time.sleep(.02)
                    continue
                try:
                    value = image.contents
                    visual = C.cast(attributes.visual, C.POINTER(Visual)).contents
                    masks = (value.red_mask, value.green_mask, value.blue_mask)
                    if masks == (0, 0, 0):
                        masks = (visual.red_mask, visual.green_mask, visual.blue_mask)
                    if value.bits_per_pixel != 32 or value.byte_order != 0 or masks != (0xff0000, 0xff00, 0xff):
                        raise RuntimeError('Unsupported X11 pixel format: '+str((value.bits_per_pixel,value.byte_order,hex(value.red_mask),hex(value.green_mask),hex(value.blue_mask))))
                    raw = C.string_at(value.data, value.bytes_per_line * height)
                    picture = Image.frombytes('RGB', (width, height), raw, 'raw', 'BGRX', value.bytes_per_line, 1)
                finally:
                    x.XDestroyImage(image)
                if draw_mouse and not hidden:
                    focus, revert = W(), C.c_int()
                    x.XGetInputFocus(display, C.byref(focus), C.byref(revert))
                    if focus.value == window:
                        cursor = fix.XFixesGetCursorImage(display)
                        if cursor:
                            try:
                                c = cursor.contents
                                px, py, child = C.c_int(), C.c_int(), W()
                                if 0 < c.width <= 256 and 0 < c.height <= 256 and x.XTranslateCoordinates(display, window, root, 0, 0, C.byref(px), C.byref(py), C.byref(child)):
                                    values = array.array('I', (c.pixels[i] & 0xffffffff for i in range(c.width * c.height)))
                                    overlay = Image.frombytes('RGBa', (c.width, c.height), values.tobytes(), 'raw', 'BGRa').convert('RGBA')
                                    picture.paste(overlay, (c.x - px.value - c.xhot, c.y - py.value - c.yhot), overlay)
                            finally:
                                x.XFree(cursor)
                picture.save(sys.stdout.buffer, format='BMP')
                sys.stdout.buffer.flush()
                next_frame = max(next_frame + 1 / fps, time.monotonic())
            delay = min(.1, max(0, next_frame - time.monotonic())) if enabled else .1
            if select.select([sys.stdin.buffer], [], [], delay)[0]:
                chunk = os.read(sys.stdin.fileno(), 4096)
                if not chunk:
                    break
                command_buffer += chunk
                if len(command_buffer) > 16384:
                    raise RuntimeError('Invalid capture command queue')
                while b'\n' in command_buffer:
                    line, command_buffer = command_buffer.split(b'\n', 1)
                    command = json.loads(line)
                    enabled = bool(command.get('enabled', enabled))
                    fps = max(1, min(60, int(command.get('fps', fps))))
    finally:
        if pixmap:
            x.XFreePixmap(display, pixmap)
        if redirected:
            co.XCompositeUnredirectWindow(display, window, 0)
        x.XCloseDisplay(display)


if __name__ == '__main__':
    try:
        main(int(sys.argv[1]), int(sys.argv[2]), sys.argv[3], sys.argv[4] == '1')
    except (BrokenPipeError, KeyboardInterrupt):
        pass
    except Exception as error:
        print(str(error), file=sys.stderr, flush=True)
        raise SystemExit(1)
