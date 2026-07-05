"""
Integration tests for CopyWgslSourceAction.

Plan file : test-definitions/copy-wgsl-source.igasset-gen.json
Input     : test_assets/copy-wgsl-source.wgsl
Output    : copy-wgsl-rsl.igasset

igasset-gen is invoked once per test session (module scope).  Individual
tests call enumerate-igasset directly so each assertion is independent.
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
# Constants drawn from the plan JSON
# ---------------------------------------------------------------------------

_PLAN_FILENAME = "copy-wgsl-source.igasset-gen.json"
_OUTPUT_FILENAME = "copy-wgsl-rsl.igasset"

_EXPECTED_VERTEX_EP = "vertex-main"
_EXPECTED_FRAGMENT_EP = "fragment-main"
_EXPECTED_COMPUTE_EP = "compute-main"

# Fill in once a reference run has been performed on a known-good build.
# Set to None to skip the hash assertion.
_EXPECTED_SOURCE_HASH: Optional[str] = None


# ---------------------------------------------------------------------------
# Module-scoped fixture: run igasset-gen once for the whole module
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def gen_result(
    igasset_gen_bin: Path,
    asset_root: Path,
    test_definitions_dir: Path,
    tmp_path_factory: pytest.TempPathFactory,
) -> tuple[int, Path]:
    """
    Run igasset-gen with the copy-wgsl plan and return (returncode, output_dir).

    Uses a module-scoped temporary directory so all tests in this file share
    the same generated output without re-running the generator.
    """
    output_dir = tmp_path_factory.mktemp("copy_wgsl_out")
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
    """
    Run enumerate-igasset on the generated WGSL asset and return parsed output.

    Skips if igasset-gen did not exit zero (individual test_exits_zero will
    catch and report that failure explicitly).
    """
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


def test_asset_type_is_wgsl_source(enumerate_result: EnumerateResult) -> None:
    assert enumerate_result.summary.asset_type == "WgslSource"


def test_entry_points(enumerate_result: EnumerateResult) -> None:
    assert enumerate_result.wgsl is not None, "WgslContents section was not parsed"
    wgsl = enumerate_result.wgsl
    assert wgsl.vertex_entry_point == _EXPECTED_VERTEX_EP
    assert wgsl.fragment_entry_point == _EXPECTED_FRAGMENT_EP
    assert wgsl.compute_entry_point == _EXPECTED_COMPUTE_EP


def test_source_bytelength_nonzero(enumerate_result: EnumerateResult) -> None:
    assert enumerate_result.wgsl is not None, "WgslContents section was not parsed"
    assert enumerate_result.wgsl.source_bytelength > 0


def test_source_hash(enumerate_result: EnumerateResult) -> None:
    """
    Assert the source SHA-256 matches a known value.

    The CopyWgslSourceAction is byte-for-byte deterministic, so this hash
    should be stable across platforms.  Set _EXPECTED_SOURCE_HASH at the top
    of this file once a reference build is available; leave it as None to skip.
    """
    assert enumerate_result.wgsl is not None, "WgslContents section was not parsed"
    assert_hash_if_known(enumerate_result.wgsl.source_hash, _EXPECTED_SOURCE_HASH)
