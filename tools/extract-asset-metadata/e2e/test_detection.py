"""
Direct-mode detection tests over the repository's test assets.

Each test inspects one fixture with the default (direct) policy and asserts
the reported kind, category and key metadata.  Dependency status resolution
in direct mode is a filesystem existence probe next to the input file.
"""

from __future__ import annotations

from pathlib import Path

from test_base import dependency, run_metadata_json


def test_png_detection(extract_asset_metadata_bin: Path, asset_root: Path) -> None:
    report = run_metadata_json(extract_asset_metadata_bin, asset_root / "glass.png")
    assert report["kind"] == "png"
    assert report["category"] == "image"
    assert report["mode"] == "direct"
    assert report["image"]["width"] == 1096
    assert report["image"]["height"] == 1096
    assert report["image"]["channels"] == 4
    assert report["image"]["has_dims"] is True
    actions = [a["action"] for a in report["actions"]]
    assert "ImageToTexture2DAction" in actions
    assert "GenerateSpritesheetAction" in actions
    assert report["warnings"] == []


def test_generated_png_detection(extract_asset_metadata_bin: Path, asset_root: Path) -> None:
    report = run_metadata_json(extract_asset_metadata_bin, asset_root / "tex.png")
    assert report["kind"] == "png"
    assert report["image"]["width"] == 4
    assert report["image"]["height"] == 4
    assert report["image"]["channels"] == 3


def test_sprite_png_detection(extract_asset_metadata_bin: Path, asset_root: Path) -> None:
    report = run_metadata_json(extract_asset_metadata_bin, asset_root / "sprite_a.png")
    assert report["kind"] == "png"
    assert report["category"] == "image"
    assert report["image"]["has_dims"] is True


def test_wgsl_entry_points(extract_asset_metadata_bin: Path, asset_root: Path) -> None:
    report = run_metadata_json(extract_asset_metadata_bin, asset_root / "entry-points.wgsl")
    assert report["kind"] == "wgsl"
    assert report["category"] == "shader"
    entry_points = {
        (e["stage"], e["name"]) for e in report["shader"]["entry_points"]
    }
    assert ("vertex", "vs_main") in entry_points
    assert ("fragment", "fs_main") in entry_points
    actions = [a["action"] for a in report["actions"]]
    assert actions == ["CopyWgslSourceAction"]


def test_obj_detection_and_deps(extract_asset_metadata_bin: Path, asset_root: Path) -> None:
    report = run_metadata_json(extract_asset_metadata_bin, asset_root / "triangle.obj")
    assert report["kind"] == "obj"
    assert report["category"] == "model"
    assert report["model"]["format"] == "obj"
    assert report["model"]["light_scan_limited"] is False
    mtl = dependency(report, "triangle.mtl")
    assert mtl is not None
    assert mtl["source"] == "obj:mtllib"
    assert mtl["status"] == "resolved"


def test_mtl_detection_and_deps(extract_asset_metadata_bin: Path, asset_root: Path) -> None:
    report = run_metadata_json(extract_asset_metadata_bin, asset_root / "triangle.mtl")
    assert report["kind"] == "mtl"
    assert report["category"] == "model"
    tex = dependency(report, "tex.png")
    assert tex is not None
    assert tex["source"] == "mtl:map"
    assert tex["status"] == "resolved"


def test_gltf_detection_and_deps(extract_asset_metadata_bin: Path, asset_root: Path) -> None:
    report = run_metadata_json(extract_asset_metadata_bin, asset_root / "scene.gltf")
    assert report["kind"] == "gltf"
    assert report["category"] == "model"
    for name in ("scene.bin", "tex.png"):
        dep = dependency(report, name)
        assert dep is not None, f"missing dependency {name}"
        assert dep["source"] == "gltf:uri"
        assert dep["status"] == "resolved"


def test_binary_fbx_light_scan(extract_asset_metadata_bin: Path, asset_root: Path) -> None:
    report = run_metadata_json(extract_asset_metadata_bin, asset_root / "box.fbx")
    assert report["kind"] == "fbx"
    assert report["category"] == "model"
    # The light scan cannot extract anything from a binary FBX; --deep is
    # authoritative there.
    assert report["model"]["light_scan_limited"] is True
    assert report["model"]["deep_scanned"] is False
    actions = [a["action"] for a in report["actions"]]
    assert actions == ["AssimpToDracoAction"]


def test_unknown_kind_is_success(extract_asset_metadata_bin: Path, tmp_path: Path) -> None:
    random_file = tmp_path / "random.bin"
    random_file.write_bytes(bytes(range(256)) * 8)
    report = run_metadata_json(extract_asset_metadata_bin, random_file)
    assert report["kind"] == "unknown"
    assert report["category"] == "unknown"
    assert report["actions"] == []
    assert report["dependencies"] == []