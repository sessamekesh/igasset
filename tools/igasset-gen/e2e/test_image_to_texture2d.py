"""
Integration tests for ImageToTexture2DAction.

Tests are grouped by encoding variant, one class per encoding.  Each class
has its own module-scoped fixture that:
  1. Checks whether the required input asset exists in the asset root.
  2. Skips the whole class if it does not (the asset is optional / not yet
     added to the repo).
  3. Runs igasset-gen once and caches the result for all tests in the class.

Adding a new encoding
---------------------
1. Add a plan JSON under test-definitions/ (e.g. rgba8unorm-encode-image.igasset-gen.json).
2. Add the required source asset to test_assets/.
3. Copy the TestEtc1s class below, update the _CFG dataclass constants, and
   adjust any encoding-specific assertions.

Hash policy
-----------
Image data hashes are NOT asserted by default.  Compressed image encoding
may produce different bit-exact outputs across hardware due to floating-point
differences in resize or encode paths.  To pin a hash for a specific
platform, pass the known hex string as `expected` to assert_hash_if_known().
"""

from __future__ import annotations

from dataclasses import dataclass
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
# Internal helpers
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class _EncodingConfig:
    """Static configuration for one ImageToTexture2DAction test class."""
    plan_filename: str        # filename inside test-definitions/
    input_asset: str          # filename inside asset_root that must exist
    output_filename: str      # expected .igasset output filename
    expected_encoding: str    # e.g. "ETC1S"
    expected_width: int
    expected_height: int


def _make_gen_result(
    cfg: _EncodingConfig,
    igasset_gen_bin: Path,
    schema_path: Path,
    asset_root: Path,
    test_definitions_dir: Path,
    tmp_path_factory: pytest.TempPathFactory,
    scope_name: str,
) -> tuple[int, Path]:
    """Shared logic: skip-or-run igasset-gen for a given encoding config."""
    input_asset = asset_root / cfg.input_asset
    if not input_asset.exists():
        pytest.skip(
            f"Input asset not present, skipping {scope_name} tests: {input_asset}"
        )

    output_dir = tmp_path_factory.mktemp(f"{scope_name}_out")
    plan = test_definitions_dir / cfg.plan_filename
    proc = run_igasset_gen(
        bin_path=igasset_gen_bin,
        plan_json=plan,
        asset_root=asset_root,
        output_dir=output_dir,
        schema=schema_path,
    )
    return proc.returncode, output_dir


def _make_enumerate_result(
    cfg: _EncodingConfig,
    gen_result: tuple[int, Path],
    enumerate_igasset_bin: Path,
) -> EnumerateResult:
    """Shared logic: skip-or-enumerate for a given encoding config."""
    returncode, output_dir = gen_result
    if returncode != 0:
        pytest.skip("igasset-gen exited non-zero; skipping enumerate step")
    igasset_file = output_dir / cfg.output_filename
    if not igasset_file.exists():
        pytest.skip(f"Expected output file not found: {igasset_file}")
    return run_enumerate_igasset(enumerate_igasset_bin, igasset_file)


# ---------------------------------------------------------------------------
# ETC1S
# ---------------------------------------------------------------------------

class TestEtc1s:
    """Tests for ImageToTexture2DAction with ETC1S encoding."""

    _CFG = _EncodingConfig(
        plan_filename="etc1s-encode-image.igasset-gen.json",
        input_asset="glass.png",
        output_filename="glass-etc1s.igasset",
        expected_encoding="ETC1S",
        expected_width=512,
        expected_height=512,
    )

    @pytest.fixture(scope="class")
    def gen_result(
        self,
        igasset_gen_bin: Path,
        schema_path: Path,
        asset_root: Path,
        test_definitions_dir: Path,
        tmp_path_factory: pytest.TempPathFactory,
    ) -> tuple[int, Path]:
        return _make_gen_result(
            self._CFG,
            igasset_gen_bin,
            schema_path,
            asset_root,
            test_definitions_dir,
            tmp_path_factory,
            scope_name="etc1s",
        )

    @pytest.fixture(scope="class")
    def enumerate_result(
        self,
        gen_result: tuple[int, Path],
        enumerate_igasset_bin: Path,
    ) -> EnumerateResult:
        return _make_enumerate_result(self._CFG, gen_result, enumerate_igasset_bin)

    # -- tests --

    def test_exits_zero(self, gen_result: tuple[int, Path]) -> None:
        returncode, _ = gen_result
        assert returncode == 0, "igasset-gen exited with a non-zero return code"

    def test_output_file_exists(self, gen_result: tuple[int, Path]) -> None:
        _, output_dir = gen_result
        assert (output_dir / self._CFG.output_filename).exists(), (
            f"Expected output file was not created: {self._CFG.output_filename}"
        )

    def test_asset_type_is_image2d(self, enumerate_result: EnumerateResult) -> None:
        assert enumerate_result.summary.asset_type == "Image2D"

    def test_encoding(self, enumerate_result: EnumerateResult) -> None:
        assert enumerate_result.image2d is not None, "Image2D section was not parsed"
        assert enumerate_result.image2d.encoding == self._CFG.expected_encoding

    def test_dimensions(self, enumerate_result: EnumerateResult) -> None:
        assert enumerate_result.image2d is not None, "Image2D section was not parsed"
        assert enumerate_result.image2d.width == self._CFG.expected_width
        assert enumerate_result.image2d.height == self._CFG.expected_height

    def test_data_nonzero(self, enumerate_result: EnumerateResult) -> None:
        assert enumerate_result.image2d is not None, "Image2D section was not parsed"
        assert enumerate_result.image2d.data_bytelength > 0

    def test_data_hash(self, enumerate_result: EnumerateResult) -> None:
        """
        Hash is not asserted by default (compressed output may vary by hardware).
        Set expected to a known hex string to pin it for a specific platform.
        """
        assert enumerate_result.image2d is not None, "Image2D section was not parsed"
        expected: Optional[str] = None
        assert_hash_if_known(enumerate_result.image2d.data_hash, expected)
