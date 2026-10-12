"""
Staged-mode integration tests (plan section 6.6).

Runs the committed staged-mode plan (logical input names + logical output
paths in subdirectories) against a per-test staging directory of UUID-named
copies of the fixtures, through a manifest - the orchestrator's deployment
model.  Verifies the generated .igassets with enumerate-igasset.

The direct-mode behavior contract (unchanged outputs when --manifest is
absent) is covered by every other test module in this suite; these tests
pin the staged policy on top.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pytest

from test_base import (
    run_enumerate_igasset,
    run_igasset_gen,
)

_STAGING_SUBDIR = "staging"
_OUTPUT_SUBDIR = "out"


def _make_staging_dir(root: Path, asset_root: Path) -> Path:
    staging = root / _STAGING_SUBDIR
    staging.mkdir(parents=True, exist_ok=True)
    for logical, physical, source in [
        ("models/copy-wgsl-source.wgsl", "uuid-wgsl", "copy-wgsl-source.wgsl"),
        ("models/triangle.obj", "uuid-obj", "triangle.obj"),
        ("models/triangle.mtl", "uuid-mtl", "triangle.mtl"),
    ]:
        dest = staging / physical
        dest.write_bytes((asset_root / source).read_bytes())
    return staging


def _write_manifest(staging: Path, entries: list[dict]) -> Path:
    manifest = staging / "manifest.json"
    manifest.write_text(json.dumps({"entries": entries}), encoding="utf-8")
    return manifest


def _run_staged(
    igasset_gen_bin: Path,
    test_definitions_dir: Path,
    staging: Path,
    manifest: Path,
    output_dir: Path,
):
    return run_igasset_gen(
        bin_path=igasset_gen_bin,
        plan_json=test_definitions_dir / "staged-mode.igasset-gen.json",
        asset_root=staging,
        output_dir=output_dir,
        extra_args=["--manifest", str(manifest)],
    )


@pytest.fixture()
def staged_result(
    igasset_gen_bin: Path,
    asset_root: Path,
    test_definitions_dir: Path,
    tmp_path: Path,
):
    staging = _make_staging_dir(tmp_path, asset_root)
    manifest = _write_manifest(
        staging,
        [
            {
                "logical_path": "models/copy-wgsl-source.wgsl",
                "physical_path": "uuid-wgsl",
            },
            {
                "logical_path": "models/triangle.obj",
                "physical_path": "uuid-obj",
            },
            {
                "logical_path": "models/triangle.mtl",
                "physical_path": "uuid-mtl",
            },
        ],
    )
    output_dir = tmp_path / _OUTPUT_SUBDIR
    proc = _run_staged(igasset_gen_bin, test_definitions_dir, staging, manifest, output_dir)
    return proc, output_dir, staging


def test_staged_generation_succeeds(staged_result) -> None:
    proc, output_dir, _ = staged_result
    assert proc.returncode == 0, proc.stderr


def test_staged_wgsl_asset(
    staged_result, enumerate_igasset_bin: Path, asset_root: Path
) -> None:
    _, output_dir, _ = staged_result
    igasset_file = output_dir / "wgsl" / "staged-copy-wgsl.igasset"
    assert igasset_file.exists(), "expected staged WGSL output in a logical subdirectory"

    result = run_enumerate_igasset(enumerate_igasset_bin, igasset_file)
    assert result.summary.asset_type == "WgslSource"
    assert result.wgsl is not None
    assert result.wgsl.vertex_entry_point == "vertex-main"
    assert result.wgsl.fragment_entry_point == "fragment-main"

    source_sha = hashlib.sha256(
        (asset_root / "copy-wgsl-source.wgsl").read_bytes()
    ).hexdigest()
    assert result.wgsl.source_hash == source_sha


def test_staged_draco_asset(
    staged_result, enumerate_igasset_bin: Path
) -> None:
    _, output_dir, _ = staged_result
    igasset_file = output_dir / "geo" / "staged-triangle.igasset"
    assert igasset_file.exists(), "expected staged Draco output in a logical subdirectory"

    result = run_enumerate_igasset(enumerate_igasset_bin, igasset_file)
    assert result.summary.asset_type == "DracoGeometry"


def test_staged_unmapped_input_fails(
    igasset_gen_bin: Path,
    asset_root: Path,
    test_definitions_dir: Path,
    tmp_path: Path,
) -> None:
    # Deny-by-default: a manifest that omits the OBJ mapping must fail the
    # generation (the tool never falls back to the staging directory).
    staging = _make_staging_dir(tmp_path, asset_root)
    manifest = _write_manifest(
        staging,
        [
            {
                "logical_path": "models/copy-wgsl-source.wgsl",
                "physical_path": "uuid-wgsl",
            },
        ],
    )
    output_dir = tmp_path / _OUTPUT_SUBDIR
    proc = _run_staged(igasset_gen_bin, test_definitions_dir, staging, manifest, output_dir)
    assert proc.returncode != 0
    assert not (output_dir / "geo" / "staged-triangle.igasset").exists()


def test_staged_traversal_output_is_denied(
    igasset_gen_bin: Path,
    asset_root: Path,
    tmp_path: Path,
) -> None:
    # An escaping logical output path must never be written, even when the
    # action itself requests it (the plan is authored by the caller, but the
    # staged policy contains outputs regardless).
    staging = _make_staging_dir(tmp_path, asset_root)
    manifest = _write_manifest(
        staging,
        [
            {
                "logical_path": "models/copy-wgsl-source.wgsl",
                "physical_path": "uuid-wgsl",
            },
        ],
    )
    evil_plan = tmp_path / "evil.igasset-gen.json"
    evil_plan.write_text(
        json.dumps(
            {
                "actions": [
                    {
                        "action_type": "CopyWgslSourceAction",
                        "action": {
                            "input_file_path": "models/copy-wgsl-source.wgsl",
                            "output_file_path": "../escaped.igasset",
                        },
                    }
                ]
            }
        ),
        encoding="utf-8",
    )
    output_dir = tmp_path / _OUTPUT_SUBDIR
    proc = run_igasset_gen(
        bin_path=igasset_gen_bin,
        plan_json=evil_plan,
        asset_root=staging,
        output_dir=output_dir,
        extra_args=["--manifest", str(manifest)],
    )
    assert proc.returncode != 0
    # The escaping output would resolve to tmp_path/escaped.igasset.
    assert not (tmp_path / "escaped.igasset").exists()


def test_require_manifest_tripwire_blocks_direct(
    igasset_gen_bin: Path,
    asset_root: Path,
    test_definitions_dir: Path,
    tmp_path: Path,
) -> None:
    # IGASSET_REQUIRE_MANIFEST (any value other than "0") refuses a
    # direct-mode run: a service-misconfiguration tripwire.
    output_dir = tmp_path / _OUTPUT_SUBDIR
    proc = run_igasset_gen(
        bin_path=igasset_gen_bin,
        plan_json=test_definitions_dir / "copy-wgsl-source.igasset-gen.json",
        asset_root=asset_root,
        output_dir=output_dir,
        env={"IGASSET_REQUIRE_MANIFEST": "1"},
    )
    assert proc.returncode != 0


def test_require_manifest_tripwire_allows_staged(
    igasset_gen_bin: Path,
    asset_root: Path,
    test_definitions_dir: Path,
    tmp_path: Path,
) -> None:
    # The tripwire only requires that --manifest is present; staged runs
    # proceed normally.
    staging = _make_staging_dir(tmp_path, asset_root)
    manifest = _write_manifest(
        staging,
        [
            {
                "logical_path": "models/copy-wgsl-source.wgsl",
                "physical_path": "uuid-wgsl",
            },
            {
                "logical_path": "models/triangle.obj",
                "physical_path": "uuid-obj",
            },
            {
                "logical_path": "models/triangle.mtl",
                "physical_path": "uuid-mtl",
            },
        ],
    )
    output_dir = tmp_path / _OUTPUT_SUBDIR
    proc = _run_staged(
        igasset_gen_bin, test_definitions_dir, staging, manifest, output_dir
    )
    assert proc.returncode == 0, proc.stderr