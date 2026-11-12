# SPDX-License-Identifier: MIT
"""Short-lived Wayland clipboard selection for a TOTP code."""

import array
import ctypes
from collections import deque
import os
from pathlib import Path
import re
import socket
import struct
import time
from typing import Callable

from PyQt5.QtCore import QCoreApplication, QObject, pyqtSlot
from PyQt5.QtDBus import QDBusConnection, QDBusInterface, QDBusMessage, QDBusVariant


HINT_MIME = b"x-kde-passwordManagerHint"
TEXT_MIMES = (b"text/plain;charset=utf-8", b"text/plain")
MANAGER = b"zwlr_data_control_manager_v1"
CHECK_INTERVAL = 0.20
SYNC_TIMEOUT = 1.0
HEADER = struct.Struct("=II")
UINT = struct.Struct("=I")
INT_SIZE = array.array("i").itemsize


def uints(*values):
    return struct.pack("=" + "I" * len(values), *values)


def string(value):
    return uints(len(value) + 1) + value + bytes(4 - len(value) % 4)


def read_string(payload, offset=0):
    size = UINT.unpack_from(payload, offset)[0]
    start = offset + UINT.size
    end = start + size
    if not size or end > len(payload) or payload[end - 1] != 0:
        raise ValueError("Invalid Wayland string")
    return payload[start:end - 1], (end + 3) & ~3


class Session(QObject):
    """The desktop and logind must independently report an unlocked seat."""

    def __init__(self):
        application = QCoreApplication.instance() or QCoreApplication([])
        super().__init__()
        self.application = application
        self.disarmed = False
        self.systemd = ctypes.CDLL("libsystemd.so.0")
        self.libc = ctypes.CDLL(None)
        self.libc.free.argtypes = [ctypes.c_void_p]
        self.libc.free.restype = None
        self.systemd.sd_seat_get_active.argtypes = [
            ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p),
            ctypes.POINTER(ctypes.c_uint),
        ]
        self.systemd.sd_session_get_type.argtypes = [
            ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p),
        ]
        self.systemd.sd_session_is_remote.argtypes = [ctypes.c_char_p]
        self.systemd.sd_bus_path_encode.argtypes = [
            ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p),
        ]
        self.session_id = self.active_session()
        if not self.session_id or self.systemd.sd_session_is_remote(self.session_id) != 0:
            raise ValueError("No active local session")
        if self.string_result(self.systemd.sd_session_get_type, self.session_id) != b"wayland":
            raise ValueError("Not a Wayland session")
        path = self.string_result(
            self.systemd.sd_bus_path_encode, b"/org/freedesktop/login1/session",
            self.session_id,
        )
        if path is None:
            raise ValueError("No logind session path")
        logind_service = "org.freedesktop.login1"
        logind_path = path.decode("ascii")
        screen_saver = "org.freedesktop.ScreenSaver"
        screen_path = "/ScreenSaver"
        system_bus = QDBusConnection.systemBus()
        session_bus = QDBusConnection.sessionBus()
        self.logind = QDBusInterface(logind_service, logind_path,
                                     "org.freedesktop.DBus.Properties", system_bus)
        self.kde = QDBusInterface(screen_saver, screen_path, screen_saver, session_bus)
        self.logind.setTimeout(150)
        self.kde.setTimeout(150)
        if not (
            session_bus.connect(screen_saver, screen_path, "org.kde.screensaver",
                                "AboutToLock", self.disarm)
            and session_bus.connect(screen_saver, screen_path, screen_saver,
                                    "ActiveChanged", self.screen_changed)
            and system_bus.connect(logind_service, logind_path, "org.freedesktop.login1.Session",
                                   "Lock", self.disarm)
        ):
            raise ValueError("Cannot watch the session lock")

    @pyqtSlot()
    def disarm(self):
        self.disarmed = True

    @pyqtSlot(bool)
    def screen_changed(self, active):
        if active:
            self.disarm()

    def string_result(self, function, *arguments):
        result = ctypes.c_void_p()
        try:
            if function(*arguments, ctypes.byref(result)) >= 0 and result.value:
                return ctypes.string_at(result)
            return None
        finally:
            self.libc.free(result)

    def active_session(self):
        result = ctypes.c_void_p()
        uid = ctypes.c_uint()
        try:
            found = self.systemd.sd_seat_get_active(b"seat0", ctypes.byref(result), ctypes.byref(uid))
            if found >= 0 and result.value and uid.value == os.getuid() and uid.value != 0:
                return ctypes.string_at(result)
            return None
        finally:
            self.libc.free(result)

    @staticmethod
    def reports_unlocked(reply):
        if reply.type() != QDBusMessage.ReplyMessage:
            return False
        arguments = reply.arguments()
        if len(arguments) != 1:
            return False
        value = arguments[0]
        if isinstance(value, QDBusVariant):
            value = value.variant()
        return value is False

    def unlocked(self):
        self.application.processEvents()
        unlocked = (
            not self.disarmed
            and self.active_session() == self.session_id
            and self.reports_unlocked(self.logind.call("Get", "org.freedesktop.login1.Session", "LockedHint"))
            and self.reports_unlocked(self.kde.call("GetActive"))
        )
        self.application.processEvents()
        return unlocked and not self.disarmed


class Selection:
    """A write-only client of Wayland's wlr-data-control v1 clipboard."""

    DISPLAY, REGISTRY, REGISTRY_SYNC, SEAT, CONTROL, DEVICE, SOURCE, DEVICE_SYNC, COPY_SYNC = range(1, 10)

    def __init__(self, code, allowed):
        self.code = code
        self.allowed = allowed
        self.incoming = bytearray()
        self.fds = deque()
        self.synchronized = set()
        self.offers = set()
        self.globals = {}
        self.seat_name = None
        self.active = True
        name = os.environ.get("WAYLAND_DISPLAY", "wayland-0")
        path = Path(name)
        if not path.is_absolute():
            path = Path(os.environ["XDG_RUNTIME_DIR"]) / path
        self.connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            self.connection.settimeout(CHECK_INTERVAL)
            self.connection.connect(str(path))
            credentials = self.connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12)
            _, uid, _ = struct.unpack("=iii", credentials)
            if uid != os.getuid():
                raise ValueError("Unexpected compositor identity")
        except BaseException:
            self.connection.close()
            raise

    def close(self):
        # Destroying the connection releases only this connection's selection.
        # A newer owner is never replaced with a global clear request.
        self.connection.close()
        while self.fds:
            os.close(self.fds.popleft())

    def send(self, object_id, opcode, payload=b""):
        self.connection.sendall(HEADER.pack(object_id, (len(payload) + HEADER.size) << 16 | opcode) + payload)

    def bind(self, interface, version, object_id):
        name, maximum = self.globals[interface]
        if maximum < version:
            raise ValueError("Required Wayland protocol unavailable")
        self.send(self.REGISTRY, 0, uints(name) + string(interface) + uints(version, object_id))

    def sync(self, object_id):
        self.send(self.DISPLAY, 0, uints(object_id))
        deadline = time.monotonic() + SYNC_TIMEOUT
        while self.active and object_id not in self.synchronized and time.monotonic() < deadline:
            self.receive()
        return self.active and object_id in self.synchronized

    def publish(self):
        self.send(self.DISPLAY, 1, uints(self.REGISTRY))
        if not self.sync(self.REGISTRY_SYNC):
            return False
        self.bind(b"wl_seat", 2, self.SEAT)
        self.bind(MANAGER, 1, self.CONTROL)
        self.send(self.CONTROL, 1, uints(self.DEVICE, self.SEAT))
        self.send(self.CONTROL, 0, uints(self.SOURCE))
        for mime in (*TEXT_MIMES, HINT_MIME):
            self.send(self.SOURCE, 0, string(mime))
        if not self.sync(self.DEVICE_SYNC) or self.seat_name != b"seat0" or not self.allowed():
            return False
        self.send(self.DEVICE, 0, uints(self.SOURCE))
        return self.sync(self.COPY_SYNC) and self.allowed()

    def receive(self):
        try:
            data, ancillary, flags, _ = self.connection.recvmsg(
                65536, socket.CMSG_SPACE(32 * INT_SIZE), socket.MSG_CMSG_CLOEXEC,
            )
        except TimeoutError:
            return
        for level, kind, payload in ancillary:
            if level == socket.SOL_SOCKET and kind == socket.SCM_RIGHTS:
                descriptors = array.array("i")
                descriptors.frombytes(payload[:len(payload) // INT_SIZE * INT_SIZE])
                self.fds.extend(descriptors)
        if not data or flags & socket.MSG_CTRUNC:
            raise OSError("Compositor connection lost")
        self.incoming.extend(data)
        while len(self.incoming) >= HEADER.size and self.active:
            object_id, info = HEADER.unpack_from(self.incoming)
            size, opcode = info >> 16, info & 0xFFFF
            if size < HEADER.size or size % 4:
                raise ValueError("Invalid Wayland event length")
            if len(self.incoming) < size:
                break
            payload = bytes(self.incoming[HEADER.size:size])
            del self.incoming[:size]
            self.event(object_id, opcode, payload)

    def event(self, object_id, opcode, payload):
        if object_id == self.DISPLAY and opcode == 0:
            raise OSError("Compositor rejected selection")
        if object_id == self.REGISTRY and opcode == 0:
            name = UINT.unpack_from(payload)[0]
            interface, offset = read_string(payload, UINT.size)
            if interface in (b"wl_seat", MANAGER):
                if interface in self.globals:
                    raise ValueError("Ambiguous Wayland seat or data-control manager")
                self.globals[interface] = (name, UINT.unpack_from(payload, offset)[0])
        if object_id in (self.REGISTRY_SYNC, self.DEVICE_SYNC, self.COPY_SYNC) and opcode == 0:
            self.synchronized.add(object_id)
        if object_id == self.SEAT and opcode == 1:
            self.seat_name, _ = read_string(payload)
            if self.seat_name != b"seat0":
                self.active = False
        if object_id == self.DEVICE:
            if opcode == 0:
                self.offers.add(UINT.unpack_from(payload)[0])
            elif opcode == 1:
                offer = UINT.unpack_from(payload)[0]
                if offer in self.offers:
                    self.send(offer, 1)  # Destroy the offer without asking for data.
                    self.offers.remove(offer)
            elif opcode == 2:
                self.active = False
        if object_id == self.SOURCE:
            if opcode == 0:
                self.provide(payload)
            elif opcode == 1:
                self.active = False

    def provide(self, payload):
        fd = self.fds.popleft()
        try:
            mime, _ = read_string(payload)
            if self.seat_name != b"seat0" or not self.allowed():
                self.active = False
                return
            data = b"secret" if mime == HINT_MIME else self.code if mime in TEXT_MIMES else b""
            os.set_blocking(fd, False)
            os.write(fd, data)
        except OSError:
            pass
        finally:
            os.close(fd)


def copy_sensitive(
    code: str, expires_at: float, session: Session, on_copied: Callable[[], None],
) -> int:
    """Publish a six-digit code until its deadline, screen lock or replacement."""
    if re.fullmatch(r"[0-9]{6}", code) is None:
        return 2
    seconds = min(expires_at - time.time(), 15)
    if seconds <= 0:
        return 1
    deadline = time.monotonic() + seconds
    selection = None
    try:
        def allowed():
            return (time.monotonic() < deadline and time.time() < expires_at
                    and session.unlocked() and time.monotonic() < deadline
                    and time.time() < expires_at)

        if not allowed():
            return 1
        selection = Selection(code.encode("ascii"), allowed)
        if not selection.publish():
            return 1
        on_copied()
        while selection.active and allowed():
            selection.receive()
        return 0
    except (IndexError, KeyError, OSError, RuntimeError, ValueError, struct.error):
        return 1
    finally:
        if selection is not None:
            selection.close()
