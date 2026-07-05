"""
Integration tests for igpack-bundle, single-wgsl plan.

Plan file : test-definitions/single-wgsl.igpack-bundle.json
Input     : prep_igassets_dir/simple-wgsl.igasset (generated once per session
            from test-prep.igasset-gen.json)
Output    : single-wgsl.igpack

igpack-bundle is invoked once per test session (module scope).  Individual
tests call enumerate-igpack directly so each assertion is independent.
"""

from __future__ import annotations

from pathlib import Path
from typing import Optional

import pytest

from test_base import (
    EnumerateResult,
    assert_hash_if_known,
    run_enumerate_igpack,
    run_igpack_bundle,
)

#
# Constants drawn from the plan JSON + expected values captured offline.

_PLAN_FILENAME = "single-wgsl.igpack-bundle.json"
_OUTPUT_FILENAME = "single-wgsl.igpack"

_EXPECTED_ASSET_NAME = "simple-wgsl"
_EXPECTED_ASSET_TYPE = "WgslSource"

_EXPECTED_VERTEX_EP = "vertex-main"
_EXPECTED_FRAGMENT_EP = "fragment-main"
_EXPECTED_COMPUTE_EP = "compute-main"

# Byte-for-byte copy of test_assets/copy-wgsl-source.wgsl; stable across platforms.
# Hash and size reflect LF line endings (normalized via .gitattributes).
_EXPECTED_SOURCE_BYTELENGTH = 171
_EXPECTED_SOURCE_HASH: Optional[str] = (
    "c12d065a259869334bacdd58f74f7dfee49f63adc37dc448a292957fab1dd5c6"
)

# The full pack embeds a FlatBuffers-serialised SingleAsset.  Layout is
# implementation-defined; set to None to skip pack-level hash assertion.
_EXPECTED_PACK_SHA256: Optional[str] = None

# Observed pack size with the current FlatBuffers/igpack-bundle build.
# Treat as a soft upper bound (< 1 KiB) rather than an exact match.
_PACK_SIZE_UPPER_BOUND = 1024


#
# Module-scoped fixtures

@pytest.fixture(scope="module")
def bundle_result(
    igpack_bundle_bin: Path,
    prep_igassets_dir: Path,
    test_definitions_dir: Path,
    tmp_path_factory: pytest.TempPathFactory,
) -> tuple[int, Path, str, str]:
    """Run igpack-bundle once; return (returncode, output_dir, stdout, stderr)."""
    output_dir = tmp_path_factory.mktemp("single_wgsl_out")
    plan = test_definitions_dir / _PLAN_FILENAME
    proc = run_igpack_bundle(
        bin_path=igpack_bundle_bin,
        plan_json=plan,
        asset_root=prep_igassets_dir,
        output_dir=output_dir,
        clean_build=True,
    )
    return proc.returncode, output_dir, proc.stdout, proc.stderr


@pytest.fixture(scope="module")
def enumerate_result(
    bundle_result: tuple[int, Path, str, str],
    enumerate_igpack_bin: Path,
) -> EnumerateResult:
    """Parse the produced .igpack via enumerate-igpack."""
    returncode, output_dir, _, _ = bundle_result
    if returncode != 0:
        pytest.skip("igpack-bundle exited non-zero; skipping enumerate step")
    igpack_file = output_dir / _OUTPUT_FILENAME
    if not igpack_file.exists():
        pytest.skip(f"Expected output file not found: {igpack_file}")
    return run_enumerate_igpack(enumerate_igpack_bin, igpack_file)


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


def test_summary_file_size_under_1kib(enumerate_result: EnumerateResult) -> None:
    assert 0 < enumerate_result.summary.file_size < _PACK_SIZE_UPPER_BOUND, (
        f"Pack file size {enumerate_result.summary.file_size} is outside the "
        f"expected (0, {_PACK_SIZE_UPPER_BOUND}) byte range."
    )


def test_summary_asset_count_is_one(enumerate_result: EnumerateResult) -> None:
    assert enumerate_result.summary.asset_count == 1


def test_summary_sha256(enumerate_result: EnumerateResult) -> None:
    """
    Asserted only when _EXPECTED_PACK_SHA256 is set.  The full pack embeds
    FlatBuffers-serialised data whose byte layout may vary across builder
    versions, so the default is to skip this check.
    """
    assert_hash_if_known(enumerate_result.summary.sha256, _EXPECTED_PACK_SHA256)


def test_asset_name(enumerate_result: EnumerateResult) -> None:
    assert len(enumerate_result.assets) == 1
    assert enumerate_result.assets[0].name == _EXPECTED_ASSET_NAME


def test_asset_type_is_wgsl_source(enumerate_result: EnumerateResult) -> None:
    assert enumerate_result.assets[0].asset_type == _EXPECTED_ASSET_TYPE


def test_wgsl_entry_points(enumerate_result: EnumerateResult) -> None:
    wgsl = enumerate_result.assets[0].wgsl
    assert wgsl is not None, "WgslContents section was not parsed"
    assert wgsl.vertex_entry_point == _EXPECTED_VERTEX_EP
    assert wgsl.fragment_entry_point == _EXPECTED_FRAGMENT_EP
    assert wgsl.compute_entry_point == _EXPECTED_COMPUTE_EP


def test_wgsl_source_bytelength(
    enumerate_result: EnumerateResult,
    asset_root: Path,
) -> None:
    """
    Source bytelength must equal the raw size of the input .wgsl file,
    since CopyWgslSourceAction is a byte-for-byte copy.
    """
    wgsl = enumerate_result.assets[0].wgsl
    assert wgsl is not None, "WgslContents section was not parsed"
    assert wgsl.source_bytelength == _EXPECTED_SOURCE_BYTELENGTH

    src_path = asset_root / "copy-wgsl-source.wgsl"
    assert wgsl.source_bytelength == src_path.stat().st_size


def test_wgsl_source_hash(enumerate_result: EnumerateResult) -> None:
    """CopyWgslSourceAction is byte-for-byte; hash is stable across platforms."""
    wgsl = enumerate_result.assets[0].wgsl
    assert wgsl is not None, "WgslContents section was not parsed"
    assert_hash_if_known(wgsl.source_hash, _EXPECTED_SOURCE_HASH)
