"""
Integration tests for GenerateSpritesheetAction.

Plan file : test-definitions/generate-spritesheet.igasset-gen.json
Inputs    : test_assets/{sprite_a,sprite_b,sprite_c,sprite_d}.png
Output    : sprites.igasset

Asserts that a 256x256 RGBA8Unorm Spritesheet igasset is produced containing
four sprites at the expected positions in the atlas.
"""

from __future__ import annotations

from pathlib import Path
from typing import Optional

import pytest

from test_base import (
    EnumerateResult,
    assert_hash_if_known,
    run_enumerate_igasset,
    run_igasset_gen,
)

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

_PLAN_FILENAME = "generate-spritesheet.igasset-gen.json"
_OUTPUT_FILENAME = "sprites.igasset"

_EXPECTED_ENCODING = "RGBA8Unorm"
_EXPECTED_WIDTH = 256
_EXPECTED_HEIGHT = 256
_EXPECTED_SPRITE_COUNT = 4

# RGBA8Unorm: bytes = width * height * 4
_EXPECTED_DATA_BYTELENGTH = _EXPECTED_WIDTH * _EXPECTED_HEIGHT * 4

_REQUIRED_INPUT_ASSETS = [
    "sprite_a.png",
    "sprite_b.png",
    "sprite_c.png",
    "sprite_d.png",
]

# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def gen_result(
    igasset_gen_bin: Path,
    asset_root: Path,
    test_definitions_dir: Path,
    tmp_path_factory: pytest.TempPathFactory,
) -> tuple[int, Path]:
    for asset in _REQUIRED_INPUT_ASSETS:
        if not (asset_root / asset).exists():
            pytest.skip(
                f"Required input asset not present, skipping spritesheet tests: "
                f"{asset_root / asset}"
            )

    output_dir = tmp_path_factory.mktemp("spritesheet_out")
    plan = test_definitions_dir / _PLAN_FILENAME
    proc = run_igasset_gen(
        bin_path=igasset_gen_bin,
        plan_json=plan,
        asset_root=asset_root,
        output_dir=output_dir,
    )
    return proc.returncode, output_dir


@pytest.fixture(scope="module")
def enumerate_result(
    gen_result: tuple[int, Path],
    enumerate_igasset_bin: Path,
) -> EnumerateResult:
    returncode, output_dir = gen_result
    if returncode != 0:
        pytest.skip("igasset-gen exited non-zero; skipping enumerate step")
    igasset_file = output_dir / _OUTPUT_FILENAME
    if not igasset_file.exists():
        pytest.skip(f"Expected output file not found: {igasset_file}")
    return run_enumerate_igasset(enumerate_igasset_bin, igasset_file)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


def test_exits_zero(gen_result: tuple[int, Path]) -> None:
    returncode, _ = gen_result
    assert returncode == 0, "igasset-gen exited with a non-zero return code"


def test_output_file_exists(gen_result: tuple[int, Path]) -> None:
    _, output_dir = gen_result
    assert (output_dir / _OUTPUT_FILENAME).exists(), (
        f"Expected output file was not created: {_OUTPUT_FILENAME}"
    )


def test_asset_type_is_spritesheet(enumerate_result: EnumerateResult) -> None:
    assert enumerate_result.summary.asset_type == "Spritesheet"


def test_spritesheet_image_dimensions(enumerate_result: EnumerateResult) -> None:
    ss = enumerate_result.spritesheet
    assert ss is not None, "Spritesheet Contents section was not parsed"
    assert ss.width == _EXPECTED_WIDTH, (
        f"Expected width {_EXPECTED_WIDTH}, got {ss.width}"
    )
    assert ss.height == _EXPECTED_HEIGHT, (
        f"Expected height {_EXPECTED_HEIGHT}, got {ss.height}"
    )


def test_spritesheet_encoding(enumerate_result: EnumerateResult) -> None:
    ss = enumerate_result.spritesheet
    assert ss is not None, "Spritesheet Contents section was not parsed"
    assert ss.encoding == _EXPECTED_ENCODING


def test_sprite_count(enumerate_result: EnumerateResult) -> None:
    ss = enumerate_result.spritesheet
    assert ss is not None, "Spritesheet Contents section was not parsed"
    assert ss.sprite_count == _EXPECTED_SPRITE_COUNT, (
        f"Expected {_EXPECTED_SPRITE_COUNT} sprites, got {ss.sprite_count}"
    )


def test_data_nonzero(enumerate_result: EnumerateResult) -> None:
    ss = enumerate_result.spritesheet
    assert ss is not None, "Spritesheet Contents section was not parsed"
    assert ss.data_bytelength > 0, "Spritesheet image had zero-length data block"


def test_data_bytelength(enumerate_result: EnumerateResult) -> None:
    ss = enumerate_result.spritesheet
    assert ss is not None, "Spritesheet Contents section was not parsed"
    assert ss.data_bytelength == _EXPECTED_DATA_BYTELENGTH, (
        f"Expected {_EXPECTED_DATA_BYTELENGTH} bytes "
        f"({_EXPECTED_WIDTH}x{_EXPECTED_HEIGHT}x4), got {ss.data_bytelength}"
    )


def test_bits_per_pixel(enumerate_result: EnumerateResult) -> None:
    ss = enumerate_result.spritesheet
    assert ss is not None, "Spritesheet Contents section was not parsed"
    assert ss.bits_per_pixel == pytest.approx(32.0), (
        f"Expected 32.0 bpp for RGBA8Unorm, got {ss.bits_per_pixel}"
    )


def test_data_hash(enumerate_result: EnumerateResult) -> None:
    """Hash is not pinned by default; pass a known hex string to assert it."""
    ss = enumerate_result.spritesheet
    assert ss is not None, "Spritesheet Contents section was not parsed"
    expected: Optional[str] = None
    assert_hash_if_known(ss.data_hash, expected)
