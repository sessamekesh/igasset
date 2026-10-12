"""
--deep tests: full Assimp imports (the opt-in attack surface), mesh
inventories, journal-refined dependency statuses and exit code 3 for
models that Assimp rejects.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from test_base import StagingDir, dependency, run_metadata, run_metadata_json


def test_deep_direct_obj_inventory(
    extract_asset_metadata_bin: Path, asset_root: Path
) -> None:
    report = run_metadata_json(
        extract_asset_metadata_bin, "--deep", asset_root / "triangle.obj"
    )
    assert report["model"]["deep_scanned"] is True
    meshes = report["model"]["meshes"]
    assert len(meshes) == 1
    assert meshes[0]["name"] == "triangle"
    assert meshes[0]["vertices"] == 3
    assert meshes[0]["faces"] == 1
    assert meshes[0]["has_texcoords"] is True
    # The mtl was both text-scanned AND opened by Assimp: one merged entry
    # with the journal (ground-truth) status.
    mtl = dependency(report, "triangle.mtl")
    assert mtl is not None
    assert mtl["status"] == "resolved"
    assert "obj:mtllib" in mtl["source"]
    assert "assimp:journal" in mtl["source"]
    # The material texture is not opened during import; it is reported via
    # the material's texture path.
    tex = dependency(report, "tex.png")
    assert tex is not None
    assert tex["source"] == "assimp:material-texture"
    assert tex["status"] == "resolved"


def test_deep_direct_gltf(
    extract_asset_metadata_bin: Path, asset_root: Path
) -> None:
    report = run_metadata_json(
        extract_asset_metadata_bin, "--deep", asset_root / "scene.gltf"
    )
    meshes = report["model"]["meshes"]
    assert len(meshes) == 1
    assert meshes[0]["name"] == "triangle-mesh"
    dep = dependency(report, "scene.bin")
    assert dep is not None
    assert dep["status"] == "resolved"
    assert "gltf:uri" in dep["source"]
    assert "assimp:journal" in dep["source"]


def test_deep_direct_fbx(
    extract_asset_metadata_bin: Path, asset_root: Path
) -> None:
    report = run_metadata_json(
        extract_asset_metadata_bin, "--deep", asset_root / "box.fbx"
    )
    assert report["model"]["deep_scanned"] is True
    assert report["model"]["light_scan_limited"] is False
    meshes = report["model"]["meshes"]
    assert len(meshes) == 1
    assert meshes[0]["vertices"] == 24
    assert meshes[0]["faces"] == 12
    assert report["model"]["importer_name"]


def test_deep_staged_obj_complete_manifest(
    extract_asset_metadata_bin: Path, asset_root: Path, tmp_path: Path
) -> None:
    staging = StagingDir(tmp_path / "staging")
    staging.stage("models/triangle.obj", asset_root / "triangle.obj", "uuid-obj")
    staging.stage("models/triangle.mtl", asset_root / "triangle.mtl", "uuid-mtl")
    staging.stage("models/tex.png", asset_root / "tex.png", "uuid-png")
    report = run_metadata_json(
        extract_asset_metadata_bin,
        "--deep",
        "--manifest", staging.manifest_path(),
        "--logical-name", "models/triangle.obj",
        staging.root / "uuid-obj",
    )
    assert report["model"]["deep_scanned"] is True
    assert report["model"]["meshes"][0]["name"] == "triangle"
    mtl = dependency(report, "models/triangle.mtl")
    assert mtl is not None
    # Journal status (actual successful open) refines the plain manifest
    # lookup.
    assert mtl["status"] == "resolved"
    tex = dependency(report, "models/tex.png")
    assert tex is not None
    assert tex["status"] == "in-manifest"


def test_deep_staged_obj_missing_mtl(
    extract_asset_metadata_bin: Path, asset_root: Path, tmp_path: Path
) -> None:
    staging = StagingDir(tmp_path / "staging")
    staging.stage("models/triangle.obj", asset_root / "triangle.obj", "uuid-obj")
    # Assimp tolerates a missing mtl (imports geometry with a placeholder
    # material), so the inspection still succeeds - and the orchestrator
    # sees exactly which logical name it failed to map.
    result = run_metadata(
        extract_asset_metadata_bin,
        "--deep", "--json",
        "--manifest", staging.manifest_path(),
        "--logical-name", "models/triangle.obj",
        staging.root / "uuid-obj",
    )
    assert result.exit_code == 0
    report = json.loads(result.stdout)
    mtl = dependency(report, "models/triangle.mtl")
    assert mtl is not None
    assert mtl["status"] == "unmapped"
    assert "assimp:journal" in mtl["source"]


def test_deep_staged_gltf_missing_bin_is_exit_3(
    extract_asset_metadata_bin: Path, asset_root: Path, tmp_path: Path
) -> None:
    staging = StagingDir(tmp_path / "staging")
    staging.stage("models/scene.gltf", asset_root / "scene.gltf", "uuid-gltf")
    result = run_metadata(
        extract_asset_metadata_bin,
        "--deep", "--json",
        "--manifest", staging.manifest_path(),
        "--logical-name", "models/scene.gltf",
        staging.root / "uuid-gltf",
    )
    # A glTF whose buffer cannot be opened is rejected by Assimp: exit 3,
    # with the light-scan dependencies still reported.
    assert result.exit_code == 3
    assert any("failed" in w.lower() for w in json.loads(result.stdout)["warnings"])


def test_deep_rejection_is_exit_3(
    extract_asset_metadata_bin: Path, tmp_path: Path
) -> None:
    # Detected as an OBJ by the light scan (v + usemtl + f), rejected by
    # the full import (out-of-range face index).
    corrupt = tmp_path / "corrupt.obj"
    corrupt.write_text("v 0 0 0\nv 1 0 0\nusemtl x\nf 1 999999 2\n")
    light = run_metadata_json(extract_asset_metadata_bin, corrupt)
    assert light["kind"] == "obj"
    result = run_metadata(extract_asset_metadata_bin, "--deep", corrupt)
    assert result.exit_code == 3


def test_deep_mtl_is_a_noop_note(
    extract_asset_metadata_bin: Path, asset_root: Path
) -> None:
    result = run_metadata(
        extract_asset_metadata_bin, "--deep", asset_root / "triangle.mtl"
    )
    assert result.exit_code == 0
    assert "--deep is not applicable to MTL" in result.stdout