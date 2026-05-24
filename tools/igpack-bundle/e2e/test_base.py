"""
Shared helpers for the igpack-bundle integration test suite.

This module provides:
  - Dataclasses mirroring the structured output of enumerate-igpack
  - run_igasset_gen()
  - run_igpack_bundle()
  - run_enumerate_igpack()
  - assert_hash_if_known()
"""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

#
# Output dataclasses

@dataclass
class AssetPackSummary:
  """Top-level asset pack metadata printed by enumerate-igpack."""
  asset_pack_path: str
  sha256: str
  asset_count: int
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
class SpritesheetContents:
    """Fields from the '------- Spritesheet Contents -------' section."""
    encoding: str
    width: int
    height: int
    data_bytelength: int
    data_hash: str
    bits_per_pixel: float
    sprite_count: int


@dataclass
class SingleAsset:
  """Metadata and contents for a single asset in the asset pack."""
  name: str
  asset_type: str
  wgsl: Optional[WgslContents] = None
  image2d: Optional[Image2DContents] = None
  spritesheet: Optional[SpritesheetContents] = None

@dataclass
class EnumerateResult:
  """Complete parsed output from a single enumerate-igpack run."""
  summary: AssetPackSummary
  assets: list[SingleAsset]

#
# Process runners

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
    plan_json:        Path to the *.igasset-gen.json plan file (positional).
    asset_root:       Directory used to resolve relative input_file_path values (-w).
    output_dir:       Directory where .igasset files are written (-o).
    schema:           Path to igasset-gen-plan.fbs (-s).
    single_threaded:  Pass --single-threaded to keep test output deterministic.
    extra_args:       Any additional CLI arguments to append.
    """
    cmd: list[str] = [
        str(bin_path),
        "-i", str(asset_root),
        "-o", str(output_dir),
        "-s", str(schema),
    ]
    if single_threaded:
        cmd.append("--single-threaded")
    if extra_args:
        cmd.extend(extra_args)
    cmd.append(str(plan_json))

    return subprocess.run(
        cmd,
        capture_output=True,
        text=True,
    )

_SUMMARY_KEYS = frozenset({"Asset Pack", "SHA256", "Asset Count", "File size"})


def run_igpack_bundle(
    bin_path: Path,
    plan_json: Path,
    asset_root: Path,
    output_dir: Path,
    schema: Path,
    *,
    clean_build: bool = False,
    extra_args: list[str] | None = None,
) -> subprocess.CompletedProcess[str]:
    """
    Run igpack-bundle with the given plan and return the CompletedProcess.

    Parameters
    ----------
    bin_path:         Path to the igpack-bundle executable.
    plan_json:        Path to the *.igpack-bundle.json plan file (positional).
    asset_root:       Directory used to resolve relative input_file_path values (-i).
    output_dir:       Directory where .igpack files are written (-o).
    schema:           Path to igpack-bundle-plan.fbs (-s).
    extra_args:       Any additional CLI arguments to append.
    """
    cmd: list[str] = [
        str(bin_path),
        "-i", str(asset_root),
        "-o", str(output_dir),
        "-s", str(schema),
    ]

    if clean_build:
        cmd.append("-c")
    if extra_args:
        cmd.extend(extra_args)
    cmd.append(str(plan_json))

    return subprocess.run(
        cmd,
        capture_output=True,
        text=True,
    )

#
# enumerate-igpack output parser

def _parse_kv(line: str) -> tuple[str, str]:
  """Split 'Key: value' into (key, value). Raise ValueError on bad format."""
  colon = line.index(":")
  return line[:colon].strip(), line[colon + 1:].strip()

def run_enumerate_igpack(
  bin_path: Path,
  igpack_path: Path,
) -> EnumerateResult:
  """
  Run enumerate-igpack on *igpack_path* and return a parsed EnumerateResult.

  Raises subprocess.CalledProcessError if the binary exits non-zero.
  """
  result = subprocess.run(
    [str(bin_path), str(igpack_path)],
    capture_output=True,
    text=True,
    check=True
  )
  return _parse_enumerate_output(result.stdout)

def _parse_enumerate_output(stdout: str) -> EnumerateResult:
  """Parse the raw stdout of enumerate-igpack into an EnumerateResult."""
  lines = stdout.splitlines()

  summary_kv: dict[str, str] = {}
  assets_out: list[SingleAsset] = []

  section = ""
  inner_kv: dict[str, str] = {}
  wgsl_kv: dict[str, str] = {}
  image2d_kv: dict[str, str] = {}
  spritesheet_kv: dict[str, str] = {}

  # In-progress asset: name/type come before optional Wgsl/Image2D subsections.
  pending_name: str = ""
  pending_type: str = ""

  known_top_headers = frozenset({"Asset Pack Summary"})
  known_sub_headers = frozenset({"WgslSource Contents", "Image2D Contents",
                                  "Spritesheet Contents"})

  def flush_pending_asset() -> None:
    """Turn pending_* + subsection dicts into a SingleAsset and append."""
    nonlocal pending_name, pending_type
    if not pending_name:
      return
    wgsl: Optional[WgslContents] = None
    if wgsl_kv:
      wgsl = WgslContents(
          source_bytelength=int(wgsl_kv.get("Source bytelength", "0")),
          source_hash=wgsl_kv.get("Source hash", ""),
          vertex_entry_point=wgsl_kv.get("Vertex entry point", ""),
          fragment_entry_point=wgsl_kv.get("Fragment entry point", ""),
          compute_entry_point=wgsl_kv.get("Compute entry point", ""),
      )
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
    spritesheet: Optional[SpritesheetContents] = None
    if spritesheet_kv:
      spritesheet = SpritesheetContents(
          encoding=spritesheet_kv.get("Encoding", ""),
          width=int(spritesheet_kv.get("Width", "0")),
          height=int(spritesheet_kv.get("Height", "0")),
          data_bytelength=int(spritesheet_kv.get("Data bytelength", "0")),
          data_hash=spritesheet_kv.get("Data hash", ""),
          bits_per_pixel=float(spritesheet_kv.get("Bits per pixel", "0")),
          sprite_count=int(spritesheet_kv.get("Sprite count", "0")),
      )
    assets_out.append(
        SingleAsset(
            name=pending_name,
            asset_type=pending_type,
            wgsl=wgsl,
            image2d=image2d,
            spritesheet=spritesheet,
        )
    )
    pending_name = ""
    pending_type = ""
    wgsl_kv.clear()
    image2d_kv.clear()
    spritesheet_kv.clear()

  def is_section_header(stripped: str) -> bool:
    return stripped.startswith("------- ") and stripped.endswith(" -------")

  for line in lines:
    stripped = line.strip()
    if not stripped:
      continue

    # Separator between summary/listing and per-asset dumps (see enumerate-igpack/main.cc).
    if set(stripped) == {"-"} and len(stripped) >= 5:
      continue

    if is_section_header(stripped):
      label = stripped.removeprefix("------- ").removesuffix(" -------").strip()

      if label in known_top_headers:
        section = "summary"
        inner_kv = summary_kv
        continue

      if label in known_sub_headers:
        if label == "WgslSource Contents":
          wgsl_kv.clear()
          section = "wgsl"
          inner_kv = wgsl_kv
        elif label == "Image2D Contents":
          image2d_kv.clear()
          section = "image2d"
          inner_kv = image2d_kv
        else:
          spritesheet_kv.clear()
          section = "spritesheet"
          inner_kv = spritesheet_kv
        continue

      # Outer per-asset header: ------- <asset name> -------
      flush_pending_asset()
      pending_name = label
      pending_type = ""
      section = "asset_outer"
      inner_kv = {}
      continue

    if stripped.startswith("- ") and "(" in stripped and pending_name == "":
      # Listing line: " - name (Type)"  — ignore.
      continue

    if section in ("wgsl", "image2d", "spritesheet") and ":" in stripped:
      k, v = _parse_kv(stripped)
      inner_kv[k] = v
      continue

    if section == "asset_outer" and stripped.startswith("Type:"):
      _, v = _parse_kv(stripped)
      pending_type = v
      continue

    if section == "summary" and ":" in stripped:
      k, v = _parse_kv(stripped)
      if k in _SUMMARY_KEYS:
        summary_kv[k] = v
      continue

  flush_pending_asset()

  summary = AssetPackSummary(
      asset_pack_path=summary_kv.get("Asset Pack", ""),
      sha256=summary_kv.get("SHA256", ""),
      asset_count=int(summary_kv.get("Asset Count", "0")),
      file_size=int(summary_kv.get("File size", "0")),
  )
  return EnumerateResult(summary=summary, assets=assets_out)

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
