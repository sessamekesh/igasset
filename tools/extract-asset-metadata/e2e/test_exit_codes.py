"""
Exit-code contract tests (plan section 5.8): distinct codes so the
orchestrator can drive policy per file.
"""

from __future__ import annotations

from pathlib import Path

from test_base import run_metadata


def test_exit_0_for_successful_inspection(
    extract_asset_metadata_bin: Path, asset_root: Path
) -> None:
    result = run_metadata(extract_asset_metadata_bin, asset_root / "glass.png")
    assert result.exit_code == 0


def test_exit_1_for_missing_file(extract_asset_metadata_bin: Path, tmp_path: Path) -> None:
    result = run_metadata(extract_asset_metadata_bin, tmp_path / "does-not-exist.bin")
    assert result.exit_code == 1


def test_exit_1_for_invalid_manifest(
    extract_asset_metadata_bin: Path, asset_root: Path, tmp_path: Path
) -> None:
    bad_manifest = tmp_path / "manifest.json"
    bad_manifest.write_text("{ not json", encoding="utf-8")
    result = run_metadata(
        extract_asset_metadata_bin,
        "--manifest", bad_manifest,
        asset_root / "glass.png",
    )
    assert result.exit_code == 1


def test_exit_2_for_oversized_file(
    extract_asset_metadata_bin: Path, asset_root: Path
) -> None:
    result = run_metadata(
        extract_asset_metadata_bin,
        "--max-file-size", "100",
        asset_root / "glass.png",
    )
    assert result.exit_code == 2


def test_exit_0_for_unknown_kind(extract_asset_metadata_bin: Path, tmp_path: Path) -> None:
    # All-zero bytes: no magic, not plausible text, no Assimp claim - a
    # successful inspection that simply reports kind: unknown.
    blob = tmp_path / "zeros.bin"
    blob.write_bytes(b"\x00" * 4096)
    result = run_metadata(extract_asset_metadata_bin, blob)
    assert result.exit_code == 0
    assert "Kind: unknown" in result.stdout


def test_exit_0_for_help(extract_asset_metadata_bin: Path) -> None:
    result = run_metadata(extract_asset_metadata_bin, "--help")
    assert result.exit_code == 0