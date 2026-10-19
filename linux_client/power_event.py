"""Lifecycle hook client. OFF_CONFIRMED is intentionally unavailable here."""

import argparse
import socket

from power_protocol import RUNTIME_EVENTS


def send_event(event, socket_path="/run/bc250-monitor/events.sock"):
    if event not in RUNTIME_EVENTS:
        raise ValueError("Not an allowed runtime lifecycle event")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(2)
        connection.connect(socket_path)
        connection.sendall((event + "\n").encode("ascii"))
        if connection.recv(32) != b"OK\n":
            raise RuntimeError("Power agent did not accept lifecycle notification")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("event", choices=sorted(RUNTIME_EVENTS))
    parser.add_argument("--socket", default="/run/bc250-monitor/events.sock")
    args = parser.parse_args()
    try:
        send_event(args.event, args.socket)
    except (OSError, RuntimeError) as error:
        parser.exit(1, f"BC250 event not delivered: {error}\n")


if __name__ == "__main__":
    main()
