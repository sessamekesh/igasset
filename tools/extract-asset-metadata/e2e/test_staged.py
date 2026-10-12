"""
Staged-mode tests: fixtures are copied into a per-job staging directory
under UUID names and inspected through a manifest (the orchestrator's
deployment model).  Logical names here are NESTED (e.g. "models/...") so
the Assimp IOSystem's two-step request resolution is exercised end to end.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from test_base import StagingDir, dependency, run_metadata_json


@pytest.fixture()
def staged_scene(extract_asset_metadata_bin: Path, asset_root: Path, tmp_path: Path):
    """Stage scene.gltf + scene.bin + tex.png under nested logical names."""
    staging = StagingDir(tmp_path / "staging")
    staging.stage("models/scene.gltf", asset_root / "scene.gltf", "uuid-gltf")
    staging.stage("models/scene.bin", asset_root / "scene.bin", "uuid-bin")
    staging.stage("models/tex.png", asset_root / "tex.png", "uuid-png")
    return staging


def test_staged_matches_direct_metadata(
    extract_asset_metadata_bin: Path, asset_root: Path, tmp_path: Path
) -> None:
    direct = run_metadata_json(extract_asset_metadata_bin, asset_root / "tex.png")

    staging = StagingDir(tmp_path / "staging")
    staging.stage("tex.png", asset_root / "tex.png", "uuid-png")
    staged = run_metadata_json(
        extract_asset_metadata_bin,
        "--manifest", staging.manifest_path(),
        "--logical-name", "tex.png",
        staging.root / "uuid-png",
    )

    assert staged["mode"] == "staged"
    assert staged["kind"] == direct["kind"]
    assert staged["image"] == direct["image"]
    assert staged["sha256"] == direct["sha256"]


def test_staged_dependencies_in_manifest(
    extract_asset_metadata_bin: Path, staged_scene: StagingDir
) -> None:
    report = run_metadata_json(
        extract_asset_metadata_bin,
        "--manifest", staged_scene.manifest_path(),
        "--logical-name", "models/scene.gltf",
        staged_scene.root / "uuid-gltf",
    )
    assert report["mode"] == "staged"
    assert report["kind"] == "gltf"
    for name in ("models/scene.bin", "models/tex.png"):
        dep = dependency(report, name)
        assert dep is not None, f"missing dependency {name}"
        assert dep["status"] == "in-manifest"


def test_staged_dependencies_not_in_manifest(
    extract_asset_metadata_bin: Path, asset_root: Path, tmp_path: Path
) -> None:
    staging = StagingDir(tmp_path / "staging")
    staging.stage("models/scene.gltf", asset_root / "scene.gltf", "uuid-gltf")
    report = run_metadata_json(
        extract_asset_metadata_bin,
        "--manifest", staging.manifest_path(),
        "--logical-name", "models/scene.gltf",
        staging.root / "uuid-gltf",
    )
    # Neither dependency is mapped: the orchestrator needs exactly this
    # signal to repair its manifest.
    assert dependency(report, "models/scene.bin")["status"] == "not-in-manifest"
    assert dependency(report, "models/tex.png")["status"] == "not-in-manifest"


def test_staged_mtl_reference_resolves_via_model_dir(
    extract_asset_metadata_bin: Path, asset_root: Path, tmp_path: Path
) -> None:
    # The OBJ importer opens the RAW mtllib reference; the IOSystem must
    # resolve it relative to the model's logical parent directory.
    staging = StagingDir(tmp_path / "staging")
    staging.stage("models/triangle.obj", asset_root / "triangle.obj", "uuid-obj")
    staging.stage("models/triangle.mtl", asset_root / "triangle.mtl", "uuid-mtl")
    report = run_metadata_json(
        extract_asset_metadata_bin,
        "--manifest", staging.manifest_path(),
        "--logical-name", "models/triangle.obj",
        staging.root / "uuid-obj",
    )
    dep = dependency(report, "models/triangle.mtl")
    assert dep is not None
    assert dep["status"] == "in-manifest"