#!/usr/bin/env python3
"""Read the desktop Codex pet and encode a bounded Passport companion asset."""

from __future__ import annotations

import base64
import dataclasses
import hashlib
import json
import pathlib
import re
import time
from typing import Any

COMPANION_SIZE = 32
COMPANION_PALETTE_BYTES = 32
COMPANION_FRAME_BYTES = COMPANION_SIZE * COMPANION_SIZE // 2
COMPANION_CHUNK_BYTES = 128
CONVERSION_VERSION = 1

# Verified against the local Codex pet v2 atlas. Unknown revisions are not
# guessed because selecting the wrong cell silently changes the pet identity.
_ATLAS_GRIDS = {2: (8, 11)}
_SELECTED_PET_RE = re.compile(
    r'^\s*selected-avatar-id\s*=\s*"([^"\\]+)"\s*(?:#.*)?$')


@dataclasses.dataclass(frozen=True)
class CompanionSource:
    pet_id: str
    name: str
    version: int
    spritesheet: pathlib.Path
    revision: str


@dataclasses.dataclass(frozen=True)
class CompanionAsset:
    source: CompanionSource
    payload: bytes
    digest: str
    frames: int = 1


def discover_codex_companion(codex_home: pathlib.Path | None = None
                             ) -> CompanionSource | None:
    home = (codex_home or pathlib.Path.home() / ".codex").expanduser().resolve()
    config_path = home / "config.toml"
    try:
        lines = config_path.read_text(encoding="utf-8").splitlines()
    except OSError:
        return None
    in_features = False
    pet_id: str | None = None
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            in_features = stripped == "[features]"
            continue
        if not in_features:
            continue
        match = _SELECTED_PET_RE.match(line)
        if match:
            pet_id = match.group(1)
            break
    if not pet_id:
        return None
    pet_dir = (home / "pets" / pet_id).resolve()
    try:
        pet_dir.relative_to((home / "pets").resolve())
        metadata_path = pet_dir / "pet.json"
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    except (OSError, ValueError, json.JSONDecodeError):
        return None
    if metadata.get("id") != pet_id:
        return None
    version = metadata.get("spriteVersionNumber")
    relative_sheet = metadata.get("spritesheetPath")
    if not isinstance(version, int) or version not in _ATLAS_GRIDS:
        return None
    if not isinstance(relative_sheet, str) or not relative_sheet:
        return None
    spritesheet = (pet_dir / relative_sheet).resolve()
    try:
        spritesheet.relative_to(pet_dir)
        stat = spritesheet.stat()
    except (OSError, ValueError):
        return None
    revision_seed = (
        f"{pet_id}\0{version}\0{relative_sheet}\0{stat.st_size}\0"
        f"{stat.st_mtime_ns}\0{CONVERSION_VERSION}"
    ).encode()
    return CompanionSource(
        pet_id=pet_id,
        name=str(metadata.get("displayName") or pet_id),
        version=version,
        spritesheet=spritesheet,
        revision=hashlib.sha256(revision_seed).hexdigest(),
    )


def _rgb565(red: int, green: int, blue: int) -> int:
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def convert_companion(source: CompanionSource) -> CompanionAsset:
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError("Pillow is required to decode Codex pet assets") from exc

    columns, rows = _ATLAS_GRIDS[source.version]
    with Image.open(source.spritesheet) as atlas:
        atlas = atlas.convert("RGBA")
        if atlas.width % columns or atlas.height % rows:
            raise ValueError("pet atlas dimensions do not match its version")
        cell_width = atlas.width // columns
        cell_height = atlas.height // rows
        frame = atlas.crop((0, 0, cell_width, cell_height))
        alpha_bbox = frame.getchannel("A").getbbox()
        if alpha_bbox is None:
            raise ValueError("pet atlas representative frame is transparent")
        frame = frame.crop(alpha_bbox)
        frame.thumbnail((COMPANION_SIZE - 2, COMPANION_SIZE - 2),
                        Image.Resampling.LANCZOS)
        canvas = Image.new("RGBA", (COMPANION_SIZE, COMPANION_SIZE), (0, 0, 0, 0))
        canvas.alpha_composite(
            frame,
            ((COMPANION_SIZE - frame.width) // 2,
             COMPANION_SIZE - frame.height - 1),
        )

        opaque = Image.new("RGB", canvas.size, (255, 255, 255))
        opaque.paste(canvas.convert("RGB"), mask=canvas.getchannel("A"))
        quantized = opaque.quantize(
            colors=15,
            method=Image.Quantize.MEDIANCUT,
            dither=Image.Dither.NONE,
        )
        raw_palette = list(quantized.getpalette() or [])
        raw_palette.extend([0] * (15 * 3 - len(raw_palette)))
        palette = bytearray(COMPANION_PALETTE_BYTES)
        for index in range(15):
            base = index * 3
            color = _rgb565(*raw_palette[base:base + 3])
            palette[(index + 1) * 2:(index + 2) * 2] = color.to_bytes(
                2, "little")

        alpha = canvas.getchannel("A").tobytes()
        colors = quantized.tobytes()
        indices = bytearray(COMPANION_FRAME_BYTES)
        for pixel in range(COMPANION_SIZE * COMPANION_SIZE):
            value = 0 if alpha[pixel] < 32 else colors[pixel] + 1
            slot = pixel // 2
            if pixel % 2:
                indices[slot] |= value
            else:
                indices[slot] = value << 4

    payload = bytes(palette + indices)
    return CompanionAsset(
        source=source,
        payload=payload,
        digest=hashlib.sha256(payload).hexdigest(),
    )


class CompanionPoller:
    """Debounce desktop selection changes and convert only the latest source."""

    def __init__(self, codex_home: pathlib.Path | None = None,
                 debounce_seconds: float = 0.5) -> None:
        self._codex_home = codex_home
        self._debounce = debounce_seconds
        self._candidate: CompanionSource | None = None
        self._candidate_since = 0.0
        self._published_revision: str | None = None

    def poll(self, now: float | None = None) -> CompanionAsset | None:
        current_time = time.monotonic() if now is None else now
        source = discover_codex_companion(self._codex_home)
        if source is None:
            self._candidate = None
            return None
        if self._candidate is None or source.revision != self._candidate.revision:
            self._candidate = source
            self._candidate_since = current_time
            return None
        if source.revision == self._published_revision:
            return None
        if current_time - self._candidate_since < self._debounce:
            return None
        asset = convert_companion(source)
        self._published_revision = source.revision
        return asset


class CompanionTransfer:
    """Stop-and-wait frame producer driven by device companion.ack offsets."""

    def __init__(self) -> None:
        self._asset: CompanionAsset | None = None
        self._offset = 0
        self._begun = False
        self._committed = False
        self._last_sent_at = 0.0

    @property
    def active(self) -> bool:
        return self._asset is not None

    def start(self, asset: CompanionAsset) -> None:
        self._asset = asset
        self._offset = 0
        self._begun = False
        self._committed = False
        self._last_sent_at = 0.0

    def next_frame(self, route: dict[str, Any]) -> dict[str, Any] | None:
        asset = self._asset
        if asset is None:
            return None
        now = time.monotonic()
        if self._last_sent_at and now - self._last_sent_at < 0.5:
            return None
        self._last_sent_at = now
        identity = {
            "bridge": route["bridge"],
            "sid": route["sid"],
            "epoch": route["epoch"],
            "asset": asset.digest,
        }
        if not self._begun:
            self._begun = True
            return {
                "type": "companion.begin",
                **identity,
                "bytes": len(asset.payload),
                "frames": asset.frames,
                "name": asset.source.name,
            }
        if self._offset < len(asset.payload):
            chunk = asset.payload[self._offset:self._offset + COMPANION_CHUNK_BYTES]
            return {
                "type": "companion.chunk",
                **identity,
                "offset": self._offset,
                "data": base64.b64encode(chunk).decode("ascii"),
            }
        self._committed = True
        return {"type": "companion.commit", **identity}

    def acknowledge(self, frame: dict[str, Any]) -> None:
        asset = self._asset
        if asset is None or frame.get("asset") != asset.digest:
            return
        status = frame.get("status")
        offset = frame.get("offset")
        if status in ("receiving", "retry") and isinstance(offset, int):
            self._offset = max(0, min(offset, len(asset.payload)))
            self._committed = False
            self._last_sent_at = 0.0
        elif status == "ready":
            self._asset = None
        elif status == "rejected":
            self._asset = None
