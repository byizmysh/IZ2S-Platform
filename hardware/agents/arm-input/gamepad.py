"""Switch HORI 8-byte HID reports, matching the ADV/Pico interface."""
import fcntl
import os
import select
import threading
import time

BUTTONS = {name: 1 << index for index, name in enumerate(
    ('y', 'b', 'a', 'x', 'l', 'r', 'zl', 'zr', 'minus', 'plus', 'l3', 'r3', 'home', 'capture'))}
NEUTRAL = bytes((0, 0, 8, 128, 128, 128, 128, 0))


def report(button=None):
    mask = BUTTONS[button] if button is not None else 0
    return bytes((mask & 255, mask >> 8, 8, 128, 128, 128, 128, 0))


class Gamepad:
    def __init__(self, device):
        self.fd = os.open(device, os.O_RDWR | os.O_NONBLOCK)
        try:
            fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BaseException:
            os.close(self.fd)
            raise
        self.packet = NEUTRAL
        self.error = None
        self.lock = threading.Lock()
        self.stopped = threading.Event()
        self.thread = threading.Thread(target=self._pump, daemon=True)
        self.thread.start()

    def _write(self, packet, timeout=1):
        deadline = time.monotonic() + timeout
        while True:
            try:
                if os.write(self.fd, packet) != 8:
                    raise RuntimeError('Incomplete controller HID report')
                return
            except BlockingIOError:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError('USB host is not accepting controller reports')
                select.select([], [self.fd], [], remaining)

    def _pump(self):
        try:
            while not self.stopped.is_set():
                with self.lock:
                    self._write(self.packet)
                self.stopped.wait(.01)
        except BaseException as error:
            self.error = error

    def check(self):
        if self.error:
            raise RuntimeError('Controller connection lost: ' + str(self.error))

    def tap(self, button, hold_ms=50):
        if button not in BUTTONS or type(hold_ms) is not int or not 20 <= hold_ms <= 2000:
            raise ValueError('Invalid controller button or duration')
        self.check()
        try:
            with self.lock:
                self.packet = report(button)
                self._write(self.packet)
            time.sleep(hold_ms / 1000)
        finally:
            with self.lock:
                self.packet = NEUTRAL
                self._write(NEUTRAL)
        self.check()

    def close(self):
        self.stopped.set()
        self.thread.join(timeout=1.2)
        try:
            self._write(NEUTRAL, timeout=.3)
        finally:
            os.close(self.fd)
