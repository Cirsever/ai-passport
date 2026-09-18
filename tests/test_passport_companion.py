from __future__ import annotations

import importlib.util
import json
import pathlib
import sys
import tempfile
import unittest

try:
    from PIL import Image, ImageDraw
except ImportError:
    Image = None
    ImageDraw = None


ROOT = pathlib.Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "passport_companion", ROOT / "tools" / "passport_companion.py")
assert SPEC and SPEC.loader
passport_companion = importlib.util.module_from_spec(SPEC)
sys.modules["passport_companion"] = passport_companion
SPEC.loader.exec_module(passport_companion)


@unittest.skipUnless(Image is not None, "Pillow is an optional host dependency")
class CompanionFixture(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.home = pathlib.Path(self.temp.name)
        pet_dir = self.home / "pets" / "pixel-pet"
        pet_dir.mkdir(parents=True)
        (self.home / "config.toml").write_text(
            '[features]\nselected-avatar-id = "pixel-pet"\n',
            encoding="utf-8",
        )
        (pet_dir / "pet.json").write_text(json.dumps({
            "id": "pixel-pet",
            "displayName": "Pixel Pet",
            "spriteVersionNumber": 2,
            "spritesheetPath": "spritesheet.webp",
        }), encoding="utf-8")
        atlas = Image.new("RGBA", (8 * 24, 11 * 24), (0, 0, 0, 0))
        draw = ImageDraw.Draw(atlas)
        draw.rectangle((3, 3, 20, 20), fill=(220, 80, 40, 255))
        draw.rectangle((8, 8, 15, 15), fill=(30, 50, 70, 255))
        atlas.save(pet_dir / "spritesheet.webp", lossless=True)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_discovery_and_conversion_are_bounded(self) -> None:
        source = passport_companion.discover_codex_companion(self.home)
        self.assertIsNotNone(source)
        asset = passport_companion.convert_companion(source)
        self.assertEqual(len(asset.payload), 32 + 512)
        self.assertEqual(len(asset.digest), 64)
        self.assertNotEqual(set(asset.payload[32:]), {0})

    def test_poller_debounces_and_only_publishes_revision_once(self) -> None:
        poller = passport_companion.CompanionPoller(
            self.home, debounce_seconds=0.5)
        self.assertIsNone(poller.poll(now=10.0))
        self.assertIsNone(poller.poll(now=10.49))
        first = poller.poll(now=10.5)
        self.assertIsNotNone(first)
        self.assertIsNone(poller.poll(now=12.0))

    def test_transfer_waits_for_ack_offsets_and_commit_receipt(self) -> None:
        source = passport_companion.discover_codex_companion(self.home)
        asset = passport_companion.convert_companion(source)
        transfer = passport_companion.CompanionTransfer()
        transfer.start(asset)
        route = {"bridge": "0123456789abcdef", "sid": "s-one", "epoch": 2}

        begin = transfer.next_frame(route)
        self.assertEqual(begin["type"], "companion.begin")
        transfer.acknowledge({
            "type": "companion.ack",
            "asset": asset.digest,
            "offset": 0,
            "status": "receiving",
        })
        chunk = transfer.next_frame(route)
        self.assertEqual(chunk["type"], "companion.chunk")
        self.assertEqual(chunk["offset"], 0)
        transfer.acknowledge({
            "type": "companion.ack",
            "asset": asset.digest,
            "offset": len(asset.payload),
            "status": "receiving",
        })
        commit = transfer.next_frame(route)
        self.assertEqual(commit["type"], "companion.commit")
        transfer.acknowledge({
            "type": "companion.ack",
            "asset": asset.digest,
            "offset": len(asset.payload),
            "status": "ready",
        })
        self.assertFalse(transfer.active)

    def test_unknown_atlas_version_is_not_guessed(self) -> None:
        metadata = self.home / "pets" / "pixel-pet" / "pet.json"
        payload = json.loads(metadata.read_text(encoding="utf-8"))
        payload["spriteVersionNumber"] = 999
        metadata.write_text(json.dumps(payload), encoding="utf-8")
        self.assertIsNone(
            passport_companion.discover_codex_companion(self.home))


if __name__ == "__main__":
    unittest.main()
