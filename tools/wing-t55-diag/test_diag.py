import os
import pty
import select
import signal
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import record


HERE = Path(__file__).resolve().parent
BOOT = '137a154e-3810-4fd4-9c53-fdae29e3f196'
BUILD = '8b' * 32
SPI_DT = '/soc/bus@2000000/spba-bus@2000000/spi@200c000'


def put(root, path, data):
    target = root / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)


def report(root, *args):
    return subprocess.check_output(['/bin/sh', HERE / 'wing-t55-diag', *args],
                                   env=dict(os.environ, WING_DIAG_ROOT=str(root)))


class Diagnostics(unittest.TestCase):
    def fixture(self, root):
        dt = 'sys/firmware/devicetree/base'
        put(root, f'{dt}/aliases/spi1', SPI_DT.encode() + b'\0')
        put(root, f'{dt}{SPI_DT}/status', b'okay\0')
        put(root, f'{dt}/soc/bus@2100000/usb@2184000/dr_mode', b'peripheral\0')
        put(root, 'proc/uptime', b'12.34 0.55\n')
        put(root, 'proc/sys/kernel/random/boot_id', f'{BOOT}\n'.encode())
        put(root, 'etc/wing-diag-build', f'{BUILD}\n'.encode())
        put(root, 'run/wing-diag-early', b'WING_DIAG_V1 event=init stage=entry state=ok\n')
        put(root, 'sys/class/udc/ci_hdrc.0/state', b'configured\n')
        put(root, 'sys/class/udc/ci_hdrc.0/function', b'Gadget Serial\n')
        for device, driver in [('2184000.usb', 'imx_usb'), ('200c000.spi', 'spi_imx')]:
            path = root / 'sys/bus/platform/devices' / device
            path.mkdir(parents=True)
            (path / 'driver').symlink_to(f'../../drivers/{driver}')

    def test_missing_devices_are_not_an_fpga_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            out = report(root, '--passive')
            for code in (b'dt-unavailable', b'udc-missing', b'tty-node-missing'):
                self.assertIn(b'event=error code=' + code, out)
            self.assertIn(b'name=ecspi2 state=absent driver=none expected=present', out)
            self.assertIn(b'name=spi1.1 state=absent driver=none expected=absent', out)
            self.assertNotIn(b'event=error code=unexpected-device-state name=spi1.1', out)
            self.assertIn(b'event=fabric state=not-probed dsp_state=not-probed', out)

    def test_reusable_reporter_does_not_claim_bench_target_offline(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            target = root / 'sys/bus/spi/devices/spi1.1'
            target.mkdir(parents=True)
            (target / 'driver').symlink_to('drivers/spibone')
            self.assertIn(b'event=error code=unexpected-device-state name=spi1.1', report(root, '--passive'))
            bench = report(root)
            self.assertNotIn(b'event=error code=unexpected-device-state name=spi1.1', bench)
            self.assertIn(b'name=spi1.1 state=present driver=spibone expected=observe-only', bench)

    def test_response_through_pty_is_logged_and_stops_on_unplug(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            master, slave = pty.openpty()
            try:
                (root / 'dev').mkdir()
                (root / 'dev/ttyGS0').symlink_to(os.ttyname(slave))
                out = report(root, '--passive')
                self.assertIn(f'begin boot={BOOT} build={BUILD} uptime_s=12.34'.encode(), out)
                self.assertIn(b'otg_mode=peripheral ecspi2_status=okay', out)
                self.assertIn(b'event=init stage=entry state=ok', out)
                self.assertIn(b'name=ttyGS0 class=absent node=present', out)
                self.assertIn(b'name=ci_hdrc.0 state=configured function=Gadget_Serial', out)
                self.assertIn(b'name=ecspi2 state=present driver=spi_imx expected=present', out)
                out = out.replace(b'\n', b'\r\n')
                child = subprocess.Popen([sys.executable, '-B', '-c',
                                          'import record,sys; from pathlib import Path; '
                                          'sys.exit(record.record(sys.argv[1],Path(sys.argv[2])))',
                                          os.ttyname(slave), tmp], cwd=HERE,
                                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                try:
                    self.assertTrue(select.select([child.stderr], [], [], 5)[0], 'recorder did not open')
                    ready = child.stderr.readline()
                    self.assertIn(b'Mitschnitt:', ready)
                    os.write(master, out)
                    consumed = b''
                    while len(consumed) < len(out) and select.select([child.stdout], [], [], 5)[0]:
                        data = os.read(child.stdout.fileno(), 4096)
                        if not data:
                            break
                        consumed += data
                    self.assertEqual(consumed, out)
                    self.assertFalse(select.select([master], [], [], 0)[0], 'recorder transmitted bytes')
                    os.close(master)
                    master = None
                    child.communicate(timeout=5)
                    self.assertEqual(child.returncode, 1)
                    log, = root.glob('cdc-*.log')
                    self.assertEqual(log.stat().st_mode & 0o777, 0o600)
                    self.assertIn(out, log.read_bytes())
                    self.assertTrue(log.read_bytes().endswith(b'event=close reason=disconnected\n'))
                finally:
                    if child.poll() is None:
                        child.send_signal(signal.SIGINT)
                        child.communicate(timeout=5)
            finally:
                if master is not None:
                    os.close(master)
                os.close(slave)

    def test_port_filter_and_missing_path(self):
        port = '/dev/cu.usbmodemWING'
        devices = [
            {'idVendor': 0x0525, 'idProduct': pid, 'IORegistryEntryChildren': [
                {'IOCalloutDevice': name}, {'IOCalloutDevice': '/dev/cu.debug-console'},
            ]} for pid, name in [(0xA4A5, '/dev/cu.usbmodemUBOOT'), (0xA4A7, port)]
        ]
        self.assertEqual(record.gadget_ports(devices), [port])
        done = subprocess.run([sys.executable, HERE / 'record.py', '--port',
                               '/dev/cu.usbmodem-wing-test-does-not-exist'], capture_output=True)
        self.assertEqual(done.returncode, 2)
        self.assertIn(b'Mit --list', done.stderr)


if __name__ == '__main__':
    unittest.main()
