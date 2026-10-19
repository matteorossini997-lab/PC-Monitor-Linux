"""Bounded, transport-independent BC250 protocol; never constructs shell commands."""

import re
from collections import OrderedDict

_REQUEST = re.compile(r"PWR_REQUEST:([0-9]{1,10}):(STOP|SLEEP)\Z", re.ASCII)
_ACTIONS = {"STOP": "poweroff", "SLEEP": "suspend"}
RUNTIME_EVENTS = frozenset({"RUNNING", "RESUMED", "SUSPENDING", "SHUTTING_DOWN"})


class LineDecoder:
    def __init__(self, limit=2048):
        self.limit = limit
        self.buffer = bytearray()
        self.discarding = False

    def feed(self, data):
        lines = []
        for byte in data:
            if byte in (10, 13):
                if self.buffer and not self.discarding:
                    lines.append(self.buffer.decode("ascii"))
                self.buffer.clear()
                self.discarding = False
            elif not self.discarding:
                if byte > 127 or byte == 0 or len(self.buffer) >= self.limit:
                    self.buffer.clear()
                    self.discarding = True
                else:
                    self.buffer.append(byte)
        return lines


class PowerProtocol:
    def __init__(self, send, execute):
        self.send = send
        self.execute = execute
        self.history = OrderedDict()

    def handle(self, line):
        if not line.startswith("PWR_REQUEST:"):
            return False
        match = _REQUEST.fullmatch(line)
        if not match:
            return True
        request_id, command = match.groups()
        if int(request_id) > 0xFFFFFFFF:
            return True
        request_id = str(int(request_id))
        if request_id in self.history:
            previous, result = self.history[request_id]
            if previous != command:
                result = "ERROR"
        else:
            try:
                self.execute(_ACTIONS[command])
                result = "OK"
            except (OSError, RuntimeError):
                result = "ERROR"
            self.history[request_id] = (command, result)
            if len(self.history) > 64:
                self.history.popitem(last=False)
        self.send(f"PWR_ACK:{request_id}:{result}\n")
        return True

    def event(self, event):
        if event not in RUNTIME_EVENTS:
            raise ValueError("Not an allowed runtime lifecycle event")
        self.send(f"PWR_OS:{event}\n")
