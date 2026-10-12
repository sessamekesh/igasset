"""
JSON output tests: the --json report must be machine-parseable and agree
with the text-mode report on the common fields.
"""

from __future__ import annotations

import hashlib
from pathlib import Path

from test_base import run_metadata, run_metadata_json


def test_json_round_trip_kind_and_width(
    extract_asset_metadata_bin: Path, asset_root: Path
) -> None:
    report = run_metadata_json(extract_asset_metadata_bin, asset_root / "glass.png")
    assert report["kind"] == "png"
    assert report["image"]["width"] == 1096
    assert report["mode"] == "direct"


def test_json_sha256_matches_file(
    extract_asset_metadata_bin: Path, asset_root: Path
) -> None:
    report = run_metadata_json(extract_asset_metadata_bin, asset_root / "tex.png")
    digest = hashlib.sha256((asset_root / "tex.png").read_bytes()).hexdigest()
    assert report["sha256"] == digest
    assert report["file_size"] == (asset_root / "tex.png").stat().st_size


def test_json_and_text_agree_on_kind(
    extract_asset_metadata_bin: Path, asset_root: Path
) -> None:
    json_report = run_metadata_json(extract_asset_metadata_bin, asset_root / "scene.gltf")
    text_result = run_metadata(extract_asset_metadata_bin, asset_root / "scene.gltf")
    assert text_result.exit_code == 0
    assert f"Kind: {json_report['kind']}" in text_result.stdout
    assert f"Category: {json_report['category']}" in text_result.stdout


def test_json_empty_sections_are_arrays(
    extract_asset_metadata_bin: Path, tmp_path: Path
) -> None:
    random_file = tmp_path / "random.bin"
    random_file.write_bytes(bytes(range(255, -1, -1)) * 16)
    report = run_metadata_json(extract_asset_metadata_bin, random_file)
    assert isinstance(report["actions"], list)
    assert isinstance(report["dependencies"], list)
    assert isinstance(report["notes"], list)
    assert isinstance(report["warnings"], list)
    # Per-kind sections are omitted entirely when not applicable.
    assert "image" not in report
    assert "model" not in report
    assert "shader" not in report