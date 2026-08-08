from __future__ import annotations

import base64
import struct
import time
import unittest
from unittest.mock import patch

from .ovd_vnc import RFBClient, VNCError


class FakeSocket:
    def __init__(self, incoming: bytes):
        self.incoming = bytearray(incoming)
        self.sent: list[bytes] = []
        self.timeout = "unset"

    def recv(self, size: int) -> bytes:
        if not self.incoming:
            return b""
        chunk = bytes(self.incoming[:size])
        del self.incoming[:size]
        return chunk

    def sendall(self, data: bytes) -> None: self.sent.append(data)
    def shutdown(self, _how) -> None: pass
    def close(self) -> None: pass
    def settimeout(self, value) -> None: self.timeout = value


class VNCProtocolTests(unittest.TestCase):
    def test_rfb_security_handshake_and_server_init(self):
        pixel_format = b"\x00" * 16
        incoming = b"RFB 003.008\n" + b"RFB 003.008\n" + b"\x01\x01" + struct.pack(">I", 0)
        incoming += struct.pack(">HH", 1, 1) + pixel_format + struct.pack(">I", 0)
        fake = FakeSocket(incoming)
        with patch("emulator.ovd_vnc.socket.create_connection", return_value=fake):
            client = RFBClient("127.0.0.1", 5901, lambda *_: None, lambda *_: None)
            client.connect()
            time.sleep(0.02)
            client.close()
        self.assertEqual(fake.sent[0], b"RFB 003.008\n")
        self.assertEqual(fake.sent[1], b"\x01")  # security type: None
        self.assertEqual(fake.sent[2], b"\x01")  # shared session
        self.assertIn(b"\x02\x00\x00\x01\x00\x00\x00\x00", fake.sent)
        self.assertIsNone(fake.timeout)

    def test_bell_does_not_consume_following_clipboard_message(self):
        received = []
        fake = FakeSocket(b"\x02\x03\x00\x00\x00" + struct.pack(">I", 5) + b"hello")
        client = RFBClient("127.0.0.1", 5901, lambda *_: None, received.append)
        client.sock = fake
        client._reader()
        self.assertEqual(received, ["hello"])

    def test_raw_frame_updates_are_composited_and_converted_to_rgb(self):
        frames = []
        fake = FakeSocket(b"\x00\x00\x01" + struct.pack(">HHHHi", 0, 0, 1, 1, 0) + b"\x00\x00\xff\x00")
        client = RFBClient("127.0.0.1", 5901, lambda *args: frames.append(args), lambda *_: None)
        client.sock = fake
        client.width, client.height = 1, 1
        client.framebuffer = bytearray(3)
        client._framebuffer_update()
        self.assertEqual(len(frames), 1)
        ppm = base64.b64decode(frames[0][1])
        self.assertTrue(ppm.endswith(b"\xff\x00\x00"))
        self.assertIn(b"\x03\x01\x00\x00\x00\x00\x00\x01\x00\x01", fake.sent)

    def test_input_messages_are_encoded_and_coordinates_are_clamped(self):
        fake = FakeSocket(b"")
        client = RFBClient("127.0.0.1", 5901, lambda *_: None, lambda *_: None)
        client.sock = fake; client.width, client.height = 100, 50
        client.key(True, 0xff0d); client.mouse(500, -3, 1); client.clipboard("hello")
        self.assertEqual(fake.sent[0], b"\x04\x01\x00\x00\x00\x00\xff\x0d")
        self.assertEqual(fake.sent[1], b"\x05\x01\x00\x63\x00\x00")
        self.assertEqual(fake.sent[2], b"\x06\x00\x00\x00\x00\x00\x00\x05hello")

    def test_oversized_clipboard_is_rejected(self):
        client = RFBClient("127.0.0.1", 5901, lambda *_: None, lambda *_: None)
        client.sock = FakeSocket(b"")
        with self.assertRaises(VNCError):
            client.clipboard("x" * (client.MAX_CLIPBOARD + 1))


if __name__ == "__main__": unittest.main()
