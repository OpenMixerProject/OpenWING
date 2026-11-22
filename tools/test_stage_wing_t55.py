import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from stage_wing_t55 import DSP_BOARD_PROFILE_SHA256
from wing_spibone_header import generate


class T55StagingTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.fpga, self.dsp, self.omc, self.output = (
            self.root / name for name in ("fpga", "dsp", "omc", "staged")
        )
        for directory in (self.fpga, self.dsp, self.omc):
            directory.mkdir()
        self.image = bytes(range(250)) + b"not a header\x00\xff"
        (self.fpga / "wing-fpga.hex.bin").write_bytes(self.image)
        self.manifest = {
            "schema_version": 1,
            "target": "wing-compact-t55f484",
            "abi_version": 65536,
            "build_id": 305419896,
            "feature_flags": 3,
            "bitstream": {
                "file": "wing-fpga.hex.bin",
                "format": "efinity-passive-spi-1x-hex-bin",
                "sha256": hashlib.sha256(self.image).hexdigest(),
            },
        }
        self.dsp_images = {f"dsp{i}.ldr": b"\x00\xff" + bytes([i]) for i in range(1, 5)}
        self.make_payload(self.dsp, self.dsp_images)
        self.dsp_provenance = "target=ADSP-21489\nboot=spi-slave\nformat=raw-binary\n"
        (self.dsp / "provenance.txt").write_text(self.dsp_provenance)
        self.make_payload(self.omc, {"omc": b"synthetic fixture, not an ARM executable"})

    def make_payload(self, directory, files):
        for name, data in files.items():
            (directory / name).write_bytes(data)
        (directory / "SHA256SUMS").write_text("".join(
            f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in files.items()
        ))
        (directory / "provenance.txt").write_text("test fixture\n")

    def run_stage(self, *options):
        (self.fpga / "manifest.json").write_text(json.dumps(self.manifest))
        return subprocess.run(
            [sys.executable, str(Path(__file__).with_name("stage_wing_t55.py")),
             self.fpga, self.dsp, self.omc, self.output, *(options or ("--recovery",))],
            capture_output=True, text=True,
        )

    def assert_rejected(self, message):
        result = self.run_stage()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(message, result.stderr)
        self.assertFalse(self.output.exists())

    def test_stages_the_original_image_and_four_distinct_dsp_streams(self):
        result = self.run_stage()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.output / "wing-fpga.hex.bin").read_bytes(), self.image)
        for name, data in self.dsp_images.items():
            self.assertEqual((self.output / name).read_bytes(), data)
        self.assertEqual((self.output / "manifest.json").read_bytes(),
                         (self.fpga / "manifest.json").read_bytes())

    def test_rejects_manifest_for_different_bytes_with_same_basename(self):
        (self.fpga / "wing-fpga.hex.bin").write_bytes(self.image[::-1])
        self.assert_rejected("manifest SHA-256 does not match")

    def test_rejects_a_hash_matched_non_raw_format(self):
        self.manifest["bitstream"]["format"] = "bit"
        self.assert_rejected("in raw Efinity passive SPI 1x format")

    def test_recovery_rejects_the_spibone_schema(self):
        self.manifest.update(schema_version=2, target="wing-compact-t55f484-fork", abi_version=0x20000)
        self.assert_rejected("schema 1 / ABI 1")

    def test_spibone_rejects_an_aes50_manifest_for_physical_port_a(self):
        self.manifest.update(benchmark_only=False, num_aes50_ports=1, channels={"sources": [{"aes50": "A"}]})
        (self.fpga / "manifest.json").write_text(json.dumps(self.manifest))
        with self.assertRaisesRegex(ValueError, "physical AES50 port B"):
            generate(self.fpga)

    def test_spibone_rejects_channels_outside_the_shared_profile(self):
        channels = [{"dsp": 1}, {"dsp": 1}, {"aes50": "B"}]
        self.manifest.update(benchmark_only=False, num_aes50_ports=1,
                             channels={"sources": channels, "destinations": channels})
        (self.fpga / "manifest.json").write_text(json.dumps(self.manifest))
        (self.fpga / "dsp_board_profile.json").write_text('{"dsp":[{"id":1,"slots_used":1}]}')
        with self.assertRaisesRegex(ValueError, "pinned SHARC profile slots"):
            generate(self.fpga)

    def test_spibone_rejects_a_stale_fabric_clock_in_the_manifest(self):
        channels = [{"dsp": 1}, {"aes50": "B"}]
        audio = {"bclk_hz": 24_576_000}
        self.manifest.update(
            benchmark_only=False, num_aes50_ports=1,
            channels={"sources": channels, "destinations": channels}, audio=audio,
            transport={"device": "/dev/spibone0", "compatible": "openmixer,wing-spibone", "chip_select": 1,
                       "mode": 0, "max_hz": 2_000_000, "protocol": "litex-spibone-burst", "address_unit": "byte"},
            fabric_clock_hz=98_304_000, rmii_clock_hz=50_000_000,
        )
        (self.fpga / "manifest.json").write_text(json.dumps(self.manifest))
        (self.fpga / "dsp_board_profile.json").write_text(json.dumps({**audio, "dsp": [{"id": 1, "slots_used": 1}]}))
        (self.fpga / "csr.csv").write_text("constant,config_clock_frequency,49152000,,\n")
        with self.assertRaisesRegex(ValueError, "generated fabric clock"):
            generate(self.fpga)

    def test_rejects_missing_dsp3(self):
        (self.dsp / "dsp3.ldr").unlink()
        self.assert_rejected("missing or empty input:")

    def test_rejects_a_checksummed_bundle_from_the_21371_loader(self):
        (self.dsp / "provenance.txt").write_text(self.dsp_provenance.replace("21489", "21371"))
        self.assert_rejected("DSP provenance must declare exactly one target=ADSP-21489")

    def test_rejects_swapped_dsps_and_tampered_omc(self):
        (self.dsp / "dsp2.ldr").write_bytes(self.dsp_images["dsp4.ldr"])
        self.assert_rejected("dsp2.ldr")
        (self.dsp / "dsp2.ldr").write_bytes(self.dsp_images["dsp2.ldr"])
        (self.omc / "omc").write_bytes(b"wrong OMC")
        self.assert_rejected("SHA-256 mismatch:")


if __name__ == "__main__":
    unittest.main()
