"""Small dependency-free RFB/VNC viewer for an OVD QEMU instance.

The viewer intentionally implements the parts needed for local QEMU sessions:
raw framebuffer rectangles, keyboard and mouse events, and the RFB clipboard
cut-text messages. It is embedded in Tkinter and uses no external VNC client.
"""

from __future__ import annotations

import base64
import socket
import struct
import threading
import tkinter as tk
from tkinter import messagebox


class VNCError(RuntimeError):
    pass


class RFBClient:
    MAX_CLIPBOARD = 16 * 1024 * 1024
    MAX_FRAMEBUFFER_PIXELS = 64 * 1024 * 1024

    def __init__(self, host: str, port: int, on_frame, on_clipboard):
        self.host, self.port = host, port
        self.on_frame, self.on_clipboard = on_frame, on_clipboard
        self.sock: socket.socket | None = None
        self.width = self.height = 0
        self.framebuffer = bytearray()
        self._stop = threading.Event()
        self._write_lock = threading.Lock()
        self._thread: threading.Thread | None = None

    def _read(self, size: int) -> bytes:
        if size < 0 or size > 256 * 1024 * 1024:
            raise VNCError("VNC read size is unsafe")
        if not self.sock:
            raise VNCError("VNC socket is not connected")
        result = bytearray()
        while len(result) < size:
            chunk = self.sock.recv(size - len(result))
            if not chunk:
                raise VNCError("VNC server closed the connection")
            result.extend(chunk)
        return bytes(result)

    def _write(self, data: bytes) -> None:
        if not self.sock:
            raise VNCError("VNC socket is not connected")
        with self._write_lock:
            self.sock.sendall(data)

    def _notify_error(self, message: str) -> None:
        try:
            self.on_frame(None, None, message)
        except Exception:
            pass

    def connect(self) -> None:
        self._stop.clear()
        self._close_socket()
        try:
            self.sock = socket.create_connection((self.host, self.port), timeout=5)
            # The timeout is only for establishing the connection. RFB is an
            # interactive long-lived protocol and must remain blocking while
            # the guest is idle.
            self.sock.settimeout(None)
            version = self._read(12)
            if not version.startswith(b"RFB "):
                raise VNCError("Not an RFB/VNC server")
            self._write(b"RFB 003.008\n")
            # RFB sends the server protocol version once. After the client
            # responds, the next bytes are the security-type count; waiting
            # for another version here deadlocks against QEMU.
            security_count = self._read(1)[0]
            if security_count == 0:
                reason_length = struct.unpack(">I", self._read(4))[0]
                reason = self._read(reason_length).decode("utf-8", "replace")
                raise VNCError(reason or "VNC server rejected the connection")
            security_types = self._read(security_count)
            if 1 not in security_types:
                raise VNCError("QEMU VNC requires an unsupported security type")
            self._write(b"\x01")
            if struct.unpack(">I", self._read(4))[0] != 0:
                raise VNCError("QEMU VNC security negotiation failed")
            self._write(b"\x01")  # shared session
            width, height = struct.unpack(">HH", self._read(4))
            self._set_dimensions(width, height)
            self._read(16)  # server pixel format; we request our own below
            name_len = struct.unpack(">I", self._read(4))[0]
            if name_len > 1024 * 1024: raise VNCError("VNC desktop name is too large")
            self._read(name_len)
            # 32-bit little-endian true colour, RGB byte order in each pixel.
            pixel_format = struct.pack(">BBBBHHHBBBxxx", 32, 24, 0, 1, 255, 255, 255, 16, 8, 0)
            self._write(b"\x00" + b"\x00\x00\x00" + pixel_format)
            # Request raw rectangles only; CopyRect is handled defensively if
            # a server sends it despite the negotiation.
            self._write(b"\x02\x00\x00\x01" + struct.pack(">i", 0))
            self._request_update(incremental=False)
            self._thread = threading.Thread(target=self._reader, name="omega-vnc-reader", daemon=True)
            self._thread.start()
        except (OSError, VNCError, struct.error):
            self._close_socket()
            raise

    def _set_dimensions(self, width: int, height: int) -> None:
        if not width or not height or width * height > self.MAX_FRAMEBUFFER_PIXELS:
            raise VNCError("VNC framebuffer dimensions are unsafe")
        self.width, self.height = width, height
        self.framebuffer = bytearray(width * height * 3)

    def _request_update(self, incremental: bool = True) -> None:
        self._write(b"\x03" + bytes([1 if incremental else 0]) + struct.pack(">HHHH", 0, 0, self.width, self.height))

    def _reader(self) -> None:
        try:
            while not self._stop.is_set():
                message = self._read(1)[0]
                if message == 0:
                    self._framebuffer_update()
                elif message == 2:
                    pass  # Bell has no payload.
                elif message == 1:
                    _padding, _first_color, count = struct.unpack(">BHH", self._read(5))
                    if count > 4096: raise VNCError("VNC colour-map update is unsafe")
                    self._read(count * 6)
                elif message == 3:
                    self._read(3)
                    length = struct.unpack(">I", self._read(4))[0]
                    if length > self.MAX_CLIPBOARD: raise VNCError("VNC clipboard payload is too large")
                    self.on_clipboard(self._read(length).decode("utf-8", "replace"))
                else:
                    raise VNCError(f"Unsupported RFB server message {message}")
        except (OSError, VNCError, struct.error, ValueError) as exc:
            if not self._stop.is_set():
                self._notify_error(f"VNC connection closed: {exc}")
        finally:
            if not self._stop.is_set(): self._close_socket()

    def _framebuffer_update(self) -> None:
        self._read(1)
        count = struct.unpack(">H", self._read(2))[0]
        changed = False
        for _ in range(count):
            x, y, width, height, encoding = struct.unpack(">HHHHi", self._read(12))
            if encoding == -223:  # DesktopSize pseudo-encoding
                self._set_dimensions(width, height); changed = True; continue
            if encoding == -224:  # LastRect pseudo-encoding
                break
            if encoding == 1:  # Defensive CopyRect support
                src_x, src_y = struct.unpack(">HH", self._read(4))
                if src_x + width > self.width or src_y + height > self.height or x + width > self.width or y + height > self.height:
                    raise VNCError("VNC CopyRect is unsafe")
                rows = [bytes(self.framebuffer[((src_y + row) * self.width + src_x) * 3:((src_y + row) * self.width + src_x + width) * 3]) for row in range(height)]
                for row, pixels in enumerate(rows):
                    target = ((y + row) * self.width + x) * 3
                    self.framebuffer[target:target + len(pixels)] = pixels
                changed = True; continue
            if encoding != 0: raise VNCError(f"Unsupported QEMU VNC encoding {encoding}")
            if x + width > self.width or y + height > self.height or width * height > 64 * 1024 * 1024:
                raise VNCError("VNC framebuffer rectangle is unsafe")
            pixels = self._read(width * height * 4)
            for row in range(height):
                source = row * width * 4
                target = ((y + row) * self.width + x) * 3
                for column in range(width):
                    offset = source + column * 4
                    self.framebuffer[target + column * 3:target + column * 3 + 3] = pixels[offset + 2:offset + 3] + pixels[offset + 1:offset + 2] + pixels[offset:offset + 1]
            changed = True
        if changed:
            ppm = b"P6\n" + str(self.width).encode() + b" " + str(self.height).encode() + b"\n255\n" + bytes(self.framebuffer)
            self.on_frame((0, 0, self.width, self.height), base64.b64encode(ppm).decode("ascii"), None)
        self._request_update(incremental=True)

    def key(self, down: bool, key: int) -> None:
        if not self._stop.is_set():
            try: self._write(b"\x04" + bytes([1 if down else 0]) + b"\x00\x00" + struct.pack(">I", key))
            except (OSError, VNCError): pass

    def mouse(self, x: int, y: int, buttons: int) -> None:
        if not self._stop.is_set():
            x = min(max(0, x), max(0, self.width - 1)); y = min(max(0, y), max(0, self.height - 1))
            try: self._write(b"\x05" + bytes([buttons & 0xff]) + struct.pack(">HH", x, y))
            except (OSError, VNCError): pass

    def clipboard(self, text: str) -> None:
        data = text.encode("utf-8")
        if len(data) > self.MAX_CLIPBOARD: raise VNCError("VNC clipboard payload is too large")
        try: self._write(b"\x06\x00\x00\x00" + struct.pack(">I", len(data)) + data)
        except (OSError, VNCError): pass

    def close(self) -> None:
        self._stop.set()
        self._close_socket()
        if self._thread and self._thread is not threading.current_thread(): self._thread.join(timeout=1)

    def _close_socket(self) -> None:
        sock, self.sock = self.sock, None
        if sock:
            try: sock.shutdown(socket.SHUT_RDWR)
            except OSError: pass
            try: sock.close()
            except OSError: pass


class VNCViewer(tk.Toplevel):
    """Interactive Tkinter VNC window with keyboard, pointer, and clipboard."""

    def __init__(self, parent, host: str = "127.0.0.1", port: int = 5901):
        super().__init__(parent)
        self.title(f"Omega VNC — {host}:{port}")
        self.canvas = tk.Canvas(self, background="black", highlightthickness=0)
        self.canvas.configure(takefocus=True)
        self.canvas.pack(fill="both", expand=True)
        self.client = RFBClient(host, port, self._frame, self._server_clipboard)
        self._image = None
        self._buttons = 0
        self._closed = False
        self.canvas.bind("<Motion>", self._motion)
        self.canvas.bind("<ButtonPress>", self._button)
        self.canvas.bind("<ButtonRelease>", self._button)
        self.canvas.bind("<KeyPress>", lambda event: self._key(event, True))
        self.canvas.bind("<KeyRelease>", lambda event: self._key(event, False))
        self.bind("<Control-v>", self._paste)
        self.bind("<Command-v>", self._paste)
        self.protocol("WM_DELETE_WINDOW", self.close)
        self.after(0, self._connect)

    def _connect(self, attempt=0):
        if self._closed: return
        def worker():
            try:
                self.client.connect()
                result = (True, "")
            except (OSError, VNCError, struct.error) as exc:
                result = (False, str(exc))
            try: self.after(0, self._connection_result, attempt, result)
            except tk.TclError: pass
        threading.Thread(target=worker, name="omega-vnc-connect", daemon=True).start()

    def _connection_result(self, attempt, result):
        if self._closed: return
        connected, error = result
        if connected:
            self.focus_force()
            self.canvas.focus_set()
        elif attempt < 5:
            self.after(300, lambda: self._connect(attempt + 1))
        else:
            messagebox.showerror("Omega VNC", error, parent=self)
            self.close()

    def _frame(self, rectangle, encoded, error):
        if error:
            try: self.after(0, lambda: self.title(error))
            except tk.TclError: pass
            return
        if rectangle and encoded:
            try:
                x, y, width, height = rectangle
                self.after(0, self._draw, x, y, width, height, encoded)
            except tk.TclError: pass

    def _draw(self, x, y, width, height, encoded):
        self._image = tk.PhotoImage(data=encoded)
        if not getattr(self, "_canvas_item", None):
            self._canvas_item = self.canvas.create_image(x, y, image=self._image, anchor="nw")
        else:
            self.canvas.itemconfigure(self._canvas_item, image=self._image)
        self.canvas.configure(width=max(self.canvas.winfo_width(), x + width), height=max(self.canvas.winfo_height(), y + height))

    def _key(self, event, down):
        if self._closed: return
        special = {"Return": 0xff0d, "BackSpace": 0xff08, "Tab": 0xff09, "Escape": 0xff1b, "Left": 0xff51, "Up": 0xff52, "Right": 0xff53, "Down": 0xff54, "Home": 0xff50, "End": 0xff57, "Delete": 0xffff,
                   "Shift_L": 0xffe1, "Shift_R": 0xffe2, "Control_L": 0xffe3, "Control_R": 0xffe4, "Alt_L": 0xffe9, "Alt_R": 0xfe03,
                   "F1": 0xffbe, "F2": 0xffbf, "F3": 0xffc0, "F4": 0xffc1, "F5": 0xffc2, "F6": 0xffc3, "F7": 0xffc4, "F8": 0xffc5, "F9": 0xffc6, "F10": 0xffc7, "F11": 0xffc8, "F12": 0xffc9}
        key = special.get(event.keysym, event.keysym_num or (ord(event.char) if event.char else 0))
        if key:
            self.client.key(down, key)

    def _paste(self, _event):
        try:
            self.client.clipboard(self.clipboard_get())
        except (tk.TclError, VNCError):
            pass
        return "break"

    def _server_clipboard(self, text):
        if self._closed: return
        try: self.after(0, self._set_clipboard, text)
        except tk.TclError: pass

    def _set_clipboard(self, text):
        if self._closed: return
        try:
            self.clipboard_clear(); self.clipboard_append(text); self.update()
        except tk.TclError:
            pass

    def _motion(self, event):
        if not self._closed: self.client.mouse(event.x, event.y, self._buttons)
    def _button(self, event):
        if self._closed: return
        self.canvas.focus_set()
        button = event.num
        if button <= 3: self._buttons = (self._buttons | (1 << (button - 1))) if event.type == tk.EventType.ButtonPress else (self._buttons & ~(1 << (button - 1)))
        elif button in {4, 5} and event.type == tk.EventType.ButtonPress:
            self.client.mouse(event.x, event.y, 8 if button == 4 else 16)
            self.client.mouse(event.x, event.y, self._buttons)
            return
        self.client.mouse(event.x, event.y, self._buttons)

    def close(self):
        if self._closed: return
        self._closed = True
        self.client.close()
        self.destroy()
