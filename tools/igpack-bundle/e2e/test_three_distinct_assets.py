"""
Integration tests for igpack-bundle, three-distinct-assets plan.

Plan file : test-definitions/three-distinct-assets.igpack-bundle.json
Inputs    : prep_igassets_dir/{simple-wgsl,glass-etc1s,glass-rgba8}.igasset
Output    : three-distinct-assets.igpack

Asserts that a pack with one WgslSource and two Image2D assets (one ETC1S,
one RGBA8Unorm) is produced with the expected per-asset metadata.
"""

from __future__ import annotations

from pathlib import Path
from typing import Optional

import pytest

from test_base import (
    EnumerateResult,
    SingleAsset,
    assert_hash_if_known,
    run_enumerate_igpack,
    run_igpack_bundle,
)

#
# Constants drawn from the plan JSON + expected values captured offline.

_PLAN_FILENAME = "three-distinct-assets.igpack-bundle.json"
_OUTPUT_FILENAME = "three-distinct-assets.igpack"

_NAME_WGSL = "simple-wgsl"
_NAME_ETC1S = "glass-etc1"
_NAME_RGBA8 = "glass-rgba8"
_EXPECTED_NAMES = frozenset({_NAME_WGSL, _NAME_ETC1S, _NAME_RGBA8})

_EXPECTED_VERTEX_EP = "vertex-main"
_EXPECTED_FRAGMENT_EP = "fragment-main"
_EXPECTED_COMPUTE_EP = "compute-main"

# Same input .wgsl as test_single_wgsl, so the source hash must match.
# Asset is pinned to LF via .gitattributes; see test_single_wgsl.py for
# why this matters.
_EXPECTED_WGSL_SOURCE_BYTELENGTH = 171
_EXPECTED_WGSL_SOURCE_HASH: Optional[str] = (
    "c12d065a259869334bacdd58f74f7dfee49f63adc37dc448a292957fab1dd5c6"
)

_EXPECTED_IMAGE_WIDTH = 512
_EXPECTED_IMAGE_HEIGHT = 512

# RGBA8Unorm is uncompressed: bytes = width * height * 4.  The image data
# itself comes from stb_image_resize, which is deterministic for a fixed
# library version, but pinning the hash makes this fragile across upgrades.
_EXPECTED_RGBA8_DATA_BYTELENGTH = _EXPECTED_IMAGE_WIDTH * _EXPECTED_IMAGE_HEIGHT * 4
_EXPECTED_RGBA8_DATA_HASH: Optional[str] = None

# ETC1S is a lossy compressed encoding; basisu encoder output may vary
# across CPU/encoder versions, so the hash is not asserted by default.
_EXPECTED_ETC1S_DATA_HASH: Optional[str] = None

_EXPECTED_PACK_SHA256: Optional[str] = None


#
# Module-scoped fixtures

@pytest.fixture(scope="module")
def bundle_result(
    igpack_bundle_bin: Path,
    igpack_bundle_schema_path: Path,
    prep_igassets_dir: Path,
    test_definitions_dir: Path,
    tmp_path_factory: pytest.TempPathFactory,
) -> tuple[int, Path, str, str]:
    output_dir = tmp_path_factory.mktemp("three_distinct_out")
    plan = test_definitions_dir / _PLAN_FILENAME
    proc = run_igpack_bundle(
        bin_path=igpack_bundle_bin,
        plan_json=plan,
        asset_root=prep_igassets_dir,
        output_dir=output_dir,
        schema=igpack_bundle_schema_path,
        clean_build=True,
    )
    return proc.returncode, output_dir, proc.stdout, proc.stderr


@pytest.fixture(scope="module")
def enumerate_result(
    bundle_result: tuple[int, Path, str, str],
    enumerate_igpack_bin: Path,
) -> EnumerateResult:
    returncode, output_dir, _, _ = bundle_result
    if returncode != 0:
        pytest.skip("igpack-bundle exited non-zero; skipping enumerate step")
    igpack_file = output_dir / _OUTPUT_FILENAME
    if not igpack_file.exists():
        pytest.skip(f"Expected output file not found: {igpack_file}")
    return run_enumerate_igpack(enumerate_igpack_bin, igpack_file)


def _find(enumerate_result: EnumerateResult, name: str) -> SingleAsset:
    matches = [a for a in enumerate_result.assets if a.name == name]
    assert len(matches) == 1, (
        f"Expected exactly one asset named {name!r}, found {len(matches)}"
    )
    return matches[0]


#
# Tests

def test_exits_zero(bundle_result: tuple[int, Path, str, str]) -> None:
    returncode, _, stdout, stderr = bundle_result
    assert returncode == 0, (
        f"igpack-bundle exited with a non-zero return code.\n"
        f"stdout:\n{stdout}\nstderr:\n{stderr}"
    )


def test_output_file_exists(bundle_result: tuple[int, Path, str, str]) -> None:
    _, output_dir, _, _ = bundle_result
    assert (output_dir / _OUTPUT_FILENAME).exists(), (
        f"Expected output file was not created: {_OUTPUT_FILENAME}"
    )


def test_summary_asset_count_is_three(enumerate_result: EnumerateResult) -> None:
    assert enumerate_result.summary.asset_count == 3


def test_summary_file_size_nonzero(enumerate_result: EnumerateResult) -> None:
    assert enumerate_result.summary.file_size > 0


def test_summary_sha256(enumerate_result: EnumerateResult) -> None:
    assert_hash_if_known(enumerate_result.summary.sha256, _EXPECTED_PACK_SHA256)


def test_assets_present_by_name(enumerate_result: EnumerateResult) -> None:
    actual = {a.name for a in enumerate_result.assets}
    assert actual == _EXPECTED_NAMES, (
        f"Asset name set mismatch.\n  expected: {sorted(_EXPECTED_NAMES)}\n"
        f"  actual:   {sorted(actual)}"
    )


def test_wgsl_asset_details(enumerate_result: EnumerateResult) -> None:
    asset = _find(enumerate_result, _NAME_WGSL)
    assert asset.asset_type == "WgslSource"
    wgsl = asset.wgsl
    assert wgsl is not None, "WgslContents section was not parsed"
    assert wgsl.vertex_entry_point == _EXPECTED_VERTEX_EP
    assert wgsl.fragment_entry_point == _EXPECTED_FRAGMENT_EP
    assert wgsl.compute_entry_point == _EXPECTED_COMPUTE_EP
    assert wgsl.source_bytelength == _EXPECTED_WGSL_SOURCE_BYTELENGTH
    assert_hash_if_known(wgsl.source_hash, _EXPECTED_WGSL_SOURCE_HASH)


def test_etc1s_asset_details(enumerate_result: EnumerateResult) -> None:
    asset = _find(enumerate_result, _NAME_ETC1S)
    assert asset.asset_type == "Image2D"
    img = asset.image2d
    assert img is not None, "Image2D section was not parsed"
    assert img.encoding == "ETC1S"
    assert img.width == _EXPECTED_IMAGE_WIDTH
    assert img.height == _EXPECTED_IMAGE_HEIGHT
    assert img.data_bytelength > 0, "ETC1S image had zero-length data block"
    # ETC1S compresses ~4 bits/texel; observed ~1.7 bpp for glass.png at 512x512.
    assert 0 < img.bits_per_pixel < 8.0
    assert_hash_if_known(img.data_hash, _EXPECTED_ETC1S_DATA_HASH)


def test_rgba8_asset_details(enumerate_result: EnumerateResult) -> None:
    asset = _find(enumerate_result, _NAME_RGBA8)
    assert asset.asset_type == "Image2D"
    img = asset.image2d
    assert img is not None, "Image2D section was not parsed"
    assert img.encoding == "RGBA8Unorm"
    assert img.width == _EXPECTED_IMAGE_WIDTH
    assert img.height == _EXPECTED_IMAGE_HEIGHT
    assert img.data_bytelength == _EXPECTED_RGBA8_DATA_BYTELENGTH
    assert img.bits_per_pixel == pytest.approx(32.0)
    assert_hash_if_known(img.data_hash, _EXPECTED_RGBA8_DATA_HASH)
