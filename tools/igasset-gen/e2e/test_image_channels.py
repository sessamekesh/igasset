"""
Regression tests for 1- and 2-channel image sources through
ImageToTexture2DAction (plan section 6.5).

History: the RGBA8 conversion and the basisu pixel reads both indexed
channels 0-2 unconditionally, reading past the end of grayscale and
gray+alpha decode buffers (heap info-leak into generated .igassets).
These tests pin the exact converted pixel values via the output data
hash, which the out-of-bounds reads would corrupt.

The source fixtures are generated with deterministic pixel formulas
(mirrored below), so the expected RGBA expansions can be computed here
without depending on the fixture files' bytes.
"""

from __future__ import annotations

import hashlib
from pathlib import Path

import pytest

from test_base import (
    EnumerateResult,
    assert_hash_if_known,
    run_enumerate_igasset,
    run_igasset_gen,
)

_W = 4
_H = 4


def _gray(x: int, y: int) -> int:
    return (x * 40 + y * 50 + 30) % 256


def _alpha(x: int, y: int) -> int:
    return (x * 30 + y * 20 + 100) % 256


def _expected_gray_rgba_bytes() -> bytes:
    out = bytearray()
    for y in range(_H):
        for x in range(_W):
            g = _gray(x, y)
            out += bytes([g, g, g, 255])
    return bytes(out)


def _expected_gray_alpha_rgba_bytes() -> bytes:
    out = bytearray()
    for y in range(_H):
        for x in range(_W):
            g = _gray(x, y)
            out += bytes([g, g, g, _alpha(x, y)])
    return bytes(out)


@pytest.fixture(scope="module")
def gen_gray(
    igasset_gen_bin: Path,
    asset_root: Path,
    test_definitions_dir: Path,
    tmp_path_factory: pytest.TempPathFactory,
) -> Path:
    output_dir = tmp_path_factory.mktemp("gray_out")
    proc = run_igasset_gen(
        bin_path=igasset_gen_bin,
        plan_json=test_definitions_dir / "rgba8unorm-encode-gray.igasset-gen.json",
        asset_root=asset_root,
        output_dir=output_dir,
    )
    assert proc.returncode == 0, proc.stderr
    return output_dir


@pytest.fixture(scope="module")
def gen_gray_alpha(
    igasset_gen_bin: Path,
    asset_root: Path,
    test_definitions_dir: Path,
    tmp_path_factory: pytest.TempPathFactory,
) -> Path:
    output_dir = tmp_path_factory.mktemp("gray_alpha_out")
    proc = run_igasset_gen(
        bin_path=igasset_gen_bin,
        plan_json=test_definitions_dir / "rgba8unorm-encode-gray-alpha.igasset-gen.json",
        asset_root=asset_root,
        output_dir=output_dir,
    )
    assert proc.returncode == 0, proc.stderr
    return output_dir


def test_gray_rgba8_pixels(
    enumerate_igasset_bin: Path, gen_gray: Path
) -> None:
    result = run_enumerate_igasset(enumerate_igasset_bin, gen_gray / "gray-rgba8.igasset")
    assert result.summary.asset_type == "Image2D"
    assert result.image2d is not None
    assert result.image2d.encoding == "RGBA8Unorm"
    assert result.image2d.width == _W
    assert result.image2d.height == _H
    assert result.image2d.data_bytelength == _W * _H * 4

    expected = hashlib.sha256(_expected_gray_rgba_bytes()).hexdigest()
    assert result.image2d.data_hash == expected


def test_gray_etc1s_runs_clean(
    enumerate_igasset_bin: Path, gen_gray: Path
) -> None:
    # Compressed output hashes vary by hardware; this pins that the
    # 1-channel basisu pixel-read path completes with sane dimensions.
    result = run_enumerate_igasset(enumerate_igasset_bin, gen_gray / "gray-etc1s.igasset")
    assert result.summary.asset_type == "Image2D"
    assert result.image2d is not None
    assert result.image2d.encoding == "ETC1S"
    assert result.image2d.width == _W
    assert result.image2d.height == _H
    assert result.image2d.data_bytelength > 0
    assert_hash_if_known(result.image2d.data_hash, None)


def test_gray_alpha_rgba8_pixels(
    enumerate_igasset_bin: Path, gen_gray_alpha: Path
) -> None:
    result = run_enumerate_igasset(
        enumerate_igasset_bin, gen_gray_alpha / "gray-alpha-rgba8.igasset"
    )
    assert result.summary.asset_type == "Image2D"
    assert result.image2d is not None
    assert result.image2d.encoding == "RGBA8Unorm"
    assert result.image2d.width == _W
    assert result.image2d.height == _H
    assert result.image2d.data_bytelength == _W * _H * 4

    expected = hashlib.sha256(_expected_gray_alpha_rgba_bytes()).hexdigest()
    assert result.image2d.data_hash == expected