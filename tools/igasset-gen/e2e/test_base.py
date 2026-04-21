"""
Shared helpers for the igasset-gen integration test suite.

This module provides:
  - Dataclasses mirroring the structured output of enumerate-igasset.
  - run_igasset_gen()       — invoke igasset-gen and capture the result.
  - run_enumerate_igasset() — invoke enumerate-igasset and parse its stdout.
  - assert_hash_if_known()  — opt-in hash equality assertion.
"""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# Output dataclasses
# ---------------------------------------------------------------------------

@dataclass
class AssetSummary:
    """Top-level asset metadata printed by enumerate-igasset."""
    asset_path: str
    asset_type: str   # e.g. "WgslSource", "Image2D", "DracoGeometry"
    sha256: str
    file_size: int


@dataclass
class WgslContents:
    """Fields from the '------- WgslSource Contents -------' section."""
    source_bytelength: int
    source_hash: str
    vertex_entry_point: str
    fragment_entry_point: str
    compute_entry_point: str


@dataclass
class Image2DContents:
    """Fields from the '------- Image2D Contents -------' section."""
    encoding: str           # "ETC1S" | "RGBA8Unorm" | "UASTC_LDR_4_4"
    width: int
    height: int
    data_bytelength: int
    data_hash: str
    bits_per_pixel: float


@dataclass
class EnumerateResult:
    """Complete parsed output from a single enumerate-igasset run."""
    summary: AssetSummary
    wgsl: Optional[WgslContents] = None
    image2d: Optional[Image2DContents] = None


# ---------------------------------------------------------------------------
# Process runners
# ---------------------------------------------------------------------------

def run_igasset_gen(
    bin_path: Path,
    plan_json: Path,
    asset_root: Path,
    output_dir: Path,
    schema: Path,
    *,
    single_threaded: bool = True,
    extra_args: list[str] | None = None,
) -> subprocess.CompletedProcess[str]:
    """
    Run igasset-gen with the given plan and return the CompletedProcess.

    Does NOT raise on non-zero exit so callers can assert the exit code
    themselves.  Both stdout and stderr are captured as text.

    Parameters
    ----------
    bin_path:         Path to the igasset-gen executable.
    plan_json:        Path to the *.igasset-gen.json plan file.
    asset_root:       Directory used to resolve relative input_file_path values (-w).
    output_dir:       Directory where .igasset files are written (-o).
    schema:           Path to igasset-gen-plan.fbs (-s).
    single_threaded:  Pass --single-threaded to keep test output deterministic.
    extra_args:       Any additional CLI arguments to append.
    """
    cmd: list[str] = [
        str(bin_path),
        "-i", str(plan_json),
        "-w", str(asset_root),
        "-o", str(output_dir),
        "-s", str(schema),
    ]
    if single_threaded:
        cmd.append("--single-threaded")
    if extra_args:
        cmd.extend(extra_args)

    return subprocess.run(
        cmd,
        capture_output=True,
        text=True,
    )


# ---------------------------------------------------------------------------
# enumerate-igasset output parser
# ---------------------------------------------------------------------------

def _parse_kv(line: str) -> tuple[str, str]:
    """Split 'Key: value' into (key, value). Raises ValueError on bad format."""
    colon = line.index(":")
    return line[:colon].strip(), line[colon + 1:].strip()


def run_enumerate_igasset(
    bin_path: Path,
    igasset_path: Path,
) -> EnumerateResult:
    """
    Run enumerate-igasset on *igasset_path* and return a parsed EnumerateResult.

    Raises subprocess.CalledProcessError if the binary exits non-zero.

    enumerate-igasset stdout format (from tools/enumerate-igasset/main.cc):

        ------- Asset Summary -------
        Asset: <path>
        Type: <type>
        SHA256: <hex>
        File size: <n>

        [------- WgslSource Contents -------]
        Source bytelength: <n>
        Source hash: <hex>
        Vertex entry point: <str>
        Fragment entry point: <str>
        Compute entry point: <str>

        [------- Image2D Contents -------]
        Encoding: <str>
        Width: <n>
        Height: <n>
        Data bytelength: <n>
        Data hash: <hex>
        Bits per pixel: <f>
    """
    result = subprocess.run(
        [str(bin_path), str(igasset_path)],
        capture_output=True,
        text=True,
        check=True,
    )
    return _parse_enumerate_output(result.stdout)


def _parse_enumerate_output(stdout: str) -> EnumerateResult:
    """Parse the raw stdout of enumerate-igasset into an EnumerateResult."""
    lines = stdout.splitlines()

    section = ""
    kv: dict[str, str] = {}

    summary_kv: dict[str, str] = {}
    wgsl_kv: dict[str, str] = {}
    image2d_kv: dict[str, str] = {}

    for line in lines:
        stripped = line.strip()
        if not stripped:
            continue

        if stripped.startswith("------- ") and stripped.endswith(" -------"):
            # Section header — normalise to a stable key
            label = stripped.removeprefix("------- ").removesuffix(" -------").strip()
            if label == "Asset Summary":
                section = "summary"
                kv = summary_kv
            elif label == "WgslSource Contents":
                section = "wgsl"
                kv = wgsl_kv
            elif label == "Image2D Contents":
                section = "image2d"
                kv = image2d_kv
            else:
                section = "unknown"
                kv = {}
            continue

        if section and ":" in stripped:
            k, v = _parse_kv(stripped)
            kv[k] = v

    # Build summary (always required)
    summary = AssetSummary(
        asset_path=summary_kv.get("Asset", ""),
        asset_type=summary_kv.get("Type", ""),
        sha256=summary_kv.get("SHA256", ""),
        file_size=int(summary_kv.get("File size", "0")),
    )

    # Optional WGSL section
    wgsl: Optional[WgslContents] = None
    if wgsl_kv:
        wgsl = WgslContents(
            source_bytelength=int(wgsl_kv.get("Source bytelength", "0")),
            source_hash=wgsl_kv.get("Source hash", ""),
            vertex_entry_point=wgsl_kv.get("Vertex entry point", ""),
            fragment_entry_point=wgsl_kv.get("Fragment entry point", ""),
            compute_entry_point=wgsl_kv.get("Compute entry point", ""),
        )

    # Optional Image2D section
    image2d: Optional[Image2DContents] = None
    if image2d_kv:
        image2d = Image2DContents(
            encoding=image2d_kv.get("Encoding", ""),
            width=int(image2d_kv.get("Width", "0")),
            height=int(image2d_kv.get("Height", "0")),
            data_bytelength=int(image2d_kv.get("Data bytelength", "0")),
            data_hash=image2d_kv.get("Data hash", ""),
            bits_per_pixel=float(image2d_kv.get("Bits per pixel", "0")),
        )

    return EnumerateResult(summary=summary, wgsl=wgsl, image2d=image2d)


# ---------------------------------------------------------------------------
# Hash assertion helper
# ---------------------------------------------------------------------------

def assert_hash_if_known(actual: str, expected: Optional[str]) -> None:
    """
    Assert hash equality only when an expected value is provided.

    Pass expected=None to skip the hash check (useful for compressed image
    outputs where encode results may vary across hardware).
    """
    if expected is not None:
        assert actual == expected, (
            f"Hash mismatch.\n  expected: {expected}\n  actual:   {actual}"
        )
