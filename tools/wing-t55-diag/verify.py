#!/usr/bin/env python3
"""Verify the actual .wingfw bytes offline; do not print packaging key material."""

import gzip
import hashlib
import re
import stat
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / 'lib/private/tools'))
from wingfw_common import compute_ngc_prefix
from wingfw_unpack import unpack_firmware


def check(ok, reason):
    if not ok:
        raise SystemExit(f'FAIL: {reason}')


def command(*args):
    return subprocess.check_output(args, text=True).strip()


def archive_entries(blob):
    pos, entries = 0, {}
    while True:
        header = blob[pos:pos + 110]
        check(header.startswith(b'070701'), 'newc header')
        _, mode, uid, gid, _, _, size, _, _, major, minor, namesize, _ = (
            int(header[i:i+8], 16) for i in range(6, 110, 8)
        )
        pos += 110
        name = blob[pos:pos + namesize - 1].decode('ascii').removeprefix('./')
        pos = (pos + namesize + 3) & ~3
        if name == 'TRAILER!!!':
            check(not any(blob[pos:]), 'no second initramfs archive or trailing data')
            return entries
        check(name not in entries and uid == gid == 0, f'unique root-owned entry: {name}')
        entries[name] = (mode, major, minor, blob[pos:pos + size])
        pos = (pos + size + 3) & ~3


def verify_initramfs(blob):
    entries = archive_entries(gzip.decompress(blob))
    files = {'init': HERE / 'init', 'bin/wing-t55-diag': HERE / 'wing-t55-diag'}
    applets = set('sh cat mount readlink sleep timeout tr uname'.split())
    directories = set('bin dev etc proc run sys'.split())
    devices = {'dev/console': (5, 1), 'dev/null': (1, 3)}
    check(set(entries) == (set(files) | {f'bin/{a}' for a in applets} | directories |
                           set(devices) | {'bin/busybox', 'etc/wing-diag-build'}),
          'initramfs strict allowlist (no payload, loader, SSH or keys)')
    for name in directories:
        check(stat.S_ISDIR(entries[name][0]), f'directory {name}')
    for name, source in files.items():
        mode, _, _, data = entries[name]
        check(mode == stat.S_IFREG | 0o755 and data == source.read_bytes(), f'packaged {name}')
    for applet in applets:
        mode, _, _, target = entries[f'bin/{applet}']
        check(stat.S_ISLNK(mode) and target.rstrip(b'\0') == b'busybox', f'applet {applet}')
    for name, expected in devices.items():
        mode, major, minor, _ = entries[name]
        check(stat.S_ISCHR(mode) and (major, minor) == expected, f'character device {name}')
    mode, _, _, busybox = entries['bin/busybox']
    check(mode == stat.S_IFREG | 0o755 and busybox[:6] == b'\x7fELF\x01\x01' and
          int.from_bytes(busybox[18:20], 'little') == 40, 'ARM ELF32 BusyBox')
    # BusyBox's NUL-separated applet table is present in its stripped ELF.
    table = b'\0'.join(name.encode() for name in sorted(applets)) + b'\0'
    check(table in busybox, 'BusyBox applet table matches rootfs')
    build = entries['etc/wing-diag-build'][-1]
    check(re.fullmatch(rb'[0-9a-f]{64}\n', build), 'build ID')
    return build.decode().strip()


def verify_dtb(path):
    def prop(node, name):
        return command('fdtget', '-t', 's', str(path), node, name)

    def properties(node):
        return command('fdtget', '-p', str(path), node).splitlines()

    spi = prop('/aliases', 'spi1')
    otg = '/soc/bus@2100000/usb@2184000'
    check(spi.endswith('/spi@200c000') and prop(spi, 'status') == 'okay', 'ECSPI2 controller')
    check(not (set(properties(spi)) & {'cs-gpios', 'pinctrl-0', 'dmas'}), 'ECSPI2 has no pads, CS or DMA')
    check(not command('fdtget', '-l', str(path), spi), 'ECSPI2 has no children')
    check(prop(otg, 'dr_mode') == 'peripheral' and prop(otg, 'status') == 'okay', 'OTG peripheral')
    check(prop('/chosen', 'stdout-path') == 'serial1:115200n8' and
          prop(prop('/aliases', 'serial1'), 'status') == 'okay', 'UART2 early console')
    check('pinctrl-0' not in properties('/'), 'no board-level FPGA/DSP pinmux')
    for alias in ('spi0', 'spi2', 'spi3', 'i2c0', 'i2c1', 'i2c2', 'i2c3', 'ethernet0',
                  'usb1', 'usb2', 'usb3', 'mmc0', 'mmc1', 'mmc2', 'mmc3'):
        check(prop(prop('/aliases', alias), 'status') == 'disabled', f'{alias} disabled')


def verify_kernel(blob):
    check(blob[36:40] == bytes.fromhex('18286f01'), 'ARM zImage magic')
    # Linux/scripts/extract-ikconfig does this via shell tools; use zlib to
    # stop at the end of the embedded gzip stream rather than at EOF.
    for match in re.finditer(b'\x1f\x8b\x08', blob):
        try:
            raw = zlib.decompress(blob[match.start():], wbits=31)
        except zlib.error:
            continue
        marker = raw.find(b'IKCFG_ST\x1f\x8b\x08')
        if marker >= 0:
            config = set(zlib.decompress(raw[marker + 8:], wbits=31).decode().splitlines())
            break
    else:
        raise SystemExit('FAIL: embedded kernel config not found')
    required = ('BLK_DEV_INITRD', 'DEVTMPFS', 'PROC_FS', 'SYSFS', 'SPI_IMX', 'USB_G_SERIAL',
                'USB_CHIPIDEA_IMX', 'USB_CHIPIDEA_UDC', 'SERIAL_IMX_EARLYCON')
    check(all(f'CONFIG_{key}=y' in config for key in required), 'kernel initramfs/SPI/USB/UART support')
    forbidden = ('SPIBONE', 'SPI_SPIDEV', 'DEVMEM', 'DEVKMEM', 'MODULES')
    check(not any(f'CONFIG_{key}={v}' in config for key in forbidden for v in 'ym'),
          'no SPI userspace driver, /dev/mem or module loading')


def verify(firmware, work):
    meta = unpack_firmware(firmware, work)
    prefix, body = (work / 'header.ngc.txt').read_text().split('~', 1)
    check(prefix == compute_ngc_prefix(body), 'outer manifest CRC')
    check('wing-compact' in meta['devtype'].split(',') and meta['version'] == 'linux-t55-diag', 'model/profile')
    check((meta['app_addr'], meta['entrypoint']) == ('0x10004000', '0x10008000'), 'RAM layout')
    check(not any(Path(meta['rsrc_output']).read_bytes()), 'empty resource payload')
    app = Path(meta['app_output']).read_bytes()
    check(b'rdinit=/init rw; printenv bootargs; iminfo 0x100C9000; bootm 0x100C9000\0' in
          app[0x5000:0xC5000], 'U-Boot default bootcmd with FIT address and init')
    fit = work / 'packaged.itb'
    magic, size = struct.unpack_from('>II', app, 0xC5000)
    check(magic == 0xD00DFEED and size <= len(app) - 0xC5000, 'FIT header at package boot address')
    fit.write_bytes(app[0xC5000:0xC5000 + size])
    sidecar = firmware.with_name('wing-t55-diag.itb')
    if sidecar.is_file():
        check(sidecar.read_bytes() == fit.read_bytes(), 'exported FIT matches package')
    check(command('fdtget', str(fit), '/configurations', 'default') == 'wing', 'FIT default config')
    for i, (node, key, load, kind) in enumerate((('kernel', 'kernel', 0x12000000, 'kernel'),
                                                ('fdt', 'fdt', 0x13000000, 'flat_dt'),
                                                ('ramdisk', 'ramdisk', 0x13800000, 'ramdisk'))):
        output = work / node
        base = f'/images/{node}'
        check(command('fdtget', str(fit), '/configurations/wing', key) == node, f'FIT {node} binding')
        check(command('fdtget', str(fit), base, 'type') == kind, f'FIT {node} type')
        check(int(command('fdtget', '-tx', str(fit), base, 'load'), 16) == load, f'FIT {node} load')
        subprocess.run(['dumpimage', '-T', 'flat_dt', '-p', str(i), '-o', output, fit],
                       check=True, stdout=subprocess.DEVNULL)
        crc = int(command('fdtget', '-tx', str(fit), base + '/hash-1', 'value'), 16)
        check(command('fdtget', str(fit), base + '/hash-1', 'algo') == 'crc32' and
              crc == zlib.crc32(output.read_bytes()), f'FIT {node} CRC32')
    verify_dtb(work / 'fdt')
    verify_kernel((work / 'kernel').read_bytes())
    initramfs = (work / 'ramdisk').read_bytes()
    sidecar = firmware.with_name('wing-t55-diag-initramfs.cpio.gz')
    if sidecar.is_file():
        check(sidecar.read_bytes() == initramfs, 'exported initramfs matches package')
    build = verify_initramfs(initramfs)
    print(f'PASS: wing-compact wrapper, embedded FIT/DTB/kernel/initramfs; build={build}')
    print(f'SHA256: {hashlib.sha256(firmware.read_bytes()).hexdigest()}  {firmware.name}')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='wing-diag-verify-') as tmp:
        verify(Path(sys.argv[1]), Path(tmp))
