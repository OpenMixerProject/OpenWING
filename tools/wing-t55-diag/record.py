#!/usr/bin/env python3
"""Manually capture the Linux WING CDC ACM callout device; never send bytes."""

import argparse
import errno
import fcntl
import os
import plistlib
import select
import subprocess
import sys
import termios
import tty
from datetime import datetime, timezone
from pathlib import Path


LOG_DIR = Path(__file__).resolve().parents[2] / '.amp/in/wing-t55-diag'


def gadget_ports(roots):
    ports = set()

    def walk(node, gadget=False):
        if 'idVendor' in node and 'idProduct' in node:
            # Linux g_serial CDC ACM; not the U-Boot ACM incarnation (0525:a4a5).
            gadget = (node['idVendor'], node['idProduct']) == (0x0525, 0xA4A7)
        port = node.get('IOCalloutDevice', '')
        if gadget and port.startswith('/dev/cu.usbmodem'):
            ports.add(port)
        for child in node.get('IORegistryEntryChildren', []):
            walk(child, gadget)

    for node in roots:
        walk(node)
    return sorted(ports)


def mac_ports():
    data = subprocess.check_output(['/usr/sbin/ioreg', '-r', '-c', 'IOUSBHostDevice', '-l', '-a'])
    return gadget_ports(plistlib.loads(data) if data.strip() else [])


def record(port, log_dir):
    fd = os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        fcntl.ioctl(fd, termios.TIOCEXCL)
        saved = termios.tcgetattr(fd)
        tty.setraw(fd, termios.TCSANOW)
        config = termios.tcgetattr(fd)
        config[2] |= termios.CLOCAL | termios.CREAD
        config[4:6] = [termios.B115200, termios.B115200]
        termios.tcsetattr(fd, termios.TCSANOW, config)
        log_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
        path = log_dir / f'cdc-{stamp}.log'
        with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'wb', buffering=0) as log:
            log.write(f'WING_CAPTURE_V1 event=open utc={stamp} port={port}\n'.encode())
            print(f'Mitschnitt: {path}', file=sys.stderr, flush=True)
            reason = 'io-error'
            result = 1
            try:
                while True:
                    select.select([fd], [], [])
                    try:
                        data = os.read(fd, 65536)
                    except BlockingIOError:
                        continue
                    except OSError as exc:
                        if exc.errno not in (errno.EIO, errno.ENXIO, errno.ENODEV):
                            raise
                        data = b''
                    if not data:
                        reason = 'disconnected'
                        print('CDC getrennt. Port erneut mit --list prüfen; Aufnahme manuell neu starten.', file=sys.stderr)
                        break
                    log.write(data)
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
            except KeyboardInterrupt:
                reason, result = 'manual-stop', 0
            finally:
                log.write(f'\nWING_CAPTURE_V1 event=close reason={reason}\n'.encode())
                os.fsync(log.fileno())
                try:
                    termios.tcsetattr(fd, termios.TCSANOW, saved)
                except termios.error:
                    pass  # disconnected port
            return result
    finally:
        os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--list', action='store_true', help='list Linux USB serial gadgets on this Mac')
    mode.add_argument('--port', help='explicit /dev/cu.usbmodem… from --list')
    args = parser.parse_args()
    if args.port and (not args.port.startswith('/dev/cu.usbmodem') or not Path(args.port).exists()):
        parser.exit(2, 'Kein gültiger /dev/cu.usbmodem…-Port. Mit --list prüfen.\n')
    try:
        ports = mac_ports()
        if args.list:
            if not ports:
                parser.exit(2, 'Kein Linux-CDC-ACM-Gadget (0525:a4a7) gefunden.\n')
            print('\n'.join(ports))
            return 0
        if args.port not in ports:
            parser.exit(2, 'Port gehört nicht zu einem Linux-CDC-ACM-Gadget (0525:a4a7).\n')
        return record(args.port, LOG_DIR)
    except (OSError, termios.error, subprocess.SubprocessError, plistlib.InvalidFileException) as exc:
        parser.exit(1, f'CDC-Mitschnitt fehlgeschlagen: {exc}\n')


if __name__ == '__main__':
    sys.exit(main())
