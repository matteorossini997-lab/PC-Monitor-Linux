#!/usr/bin/env python3
"""Create private local BC250 TLS material and token. Never prints credentials."""

import argparse
import ipaddress
import os
from pathlib import Path
import re
import secrets
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ip", required=True, help="Reserved IPv4 address of the ESP32 on your home LAN")
    parser.add_argument("--hostname", default="bc250-controller")
    args = parser.parse_args()
    try:
        address = ipaddress.IPv4Address(args.ip)
    except ipaddress.AddressValueError:
        parser.error("--ip must be an IPv4 address")
    if not any(address in ipaddress.IPv4Network(network) for network in
               ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16")):
        parser.error("Use an RFC1918 private home-LAN address, not a public address")
    if not re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?", args.hostname):
        parser.error("Invalid hostname")
    local = Path(__file__).resolve().parents[1] / "local"
    tls = local / "tls"
    if local.is_symlink() or tls.is_symlink():
        parser.error("Refusing symlinked local/TLS directories")
    targets = (tls / "servercert.pem", tls / "prvtkey.pem", local / "api-token")
    if any(path.exists() or path.is_symlink() for path in targets):
        parser.error("Local certificate/key/token already exists; refusing to overwrite")
    previous_umask = os.umask(0o077)
    try:
        tls.mkdir(mode=0o700, parents=True, exist_ok=True)
        for directory in (local, tls):
            info = directory.stat()
            if info.st_uid != os.getuid() or info.st_mode & 0o077:
                parser.error("Local/TLS directories must be owned by you and mode 0700")
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
            "-sha256", "-nodes", "-days", "3650", "-subj", f"/CN={args.hostname}",
            "-addext", f"subjectAltName=DNS:{args.hostname},IP:{address}",
            "-keyout", str(targets[1]), "-out", str(targets[0])
        ], check=True, capture_output=True)
        fd = os.open(targets[2], os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w") as token:
            token.write(secrets.token_hex(32) + "\n")
    finally:
        os.umask(previous_umask)
    print("Created local TLS certificate/key and local/api-token (not printed).")
    print("Configure BC250 Wi-Fi and token using idf.py menuconfig; see docs/BC250_POWER.md.")


if __name__ == "__main__":
    main()
