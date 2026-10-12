"""
Shared helpers for the extract-asset-metadata integration test suite.

This module provides:
  - run_metadata()       — invoke extract-asset-metadata and capture the result.
  - run_metadata_json()  — invoke the tool with --json and parse its stdout.
  - dependency()          — look up a dependency entry in a JSON report.
  - StagingDir           — helper for staged-mode tests (UUID copies + manifest).
"""

from __future__ import annotations

import json
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional


# ---------------------------------------------------------------------------
# Result helpers
# ---------------------------------------------------------------------------

@dataclass
class MetadataResult:
    """Result of one extract-asset-metadata invocation."""
    exit_code: int
    stdout: str
    stderr: str


def run_metadata(extract_bin: Path, *args: Any) -> MetadataResult:
    """Run extract-asset-metadata and capture exit code + output."""
    argv = [str(extract_bin), *[str(a) for a in args]]
    proc = subprocess.run(argv, capture_output=True)
    return MetadataResult(
        exit_code=proc.returncode,
        stdout=proc.stdout.decode("utf-8", errors="replace"),
        stderr=proc.stderr.decode("utf-8", errors="replace"),
    )


def run_metadata_json(extract_bin: Path, *args: Any) -> dict:
    """Run extract-asset-metadata --json and return the parsed report."""
    result = run_metadata(extract_bin, "--json", *args)
    assert result.exit_code == 0, (
        f"expected exit 0, got {result.exit_code}; stderr:\n{result.stderr}"
    )
    return json.loads(result.stdout)


def dependency(report: dict, name: str) -> Optional[dict]:
    """Return the dependency entry with the given logical name, if any."""
    for dep in report.get("dependencies", []):
        if dep.get("name") == name:
            return dep
    return None


# ---------------------------------------------------------------------------
# Staged-mode helper
# ---------------------------------------------------------------------------

class StagingDir:
    """A per-job staging directory: fixture files copied under UUID names,
    plus a manifest mapping logical paths to those names."""

    def __init__(self, root: Path):
        self.root = root
        self.root.mkdir(parents=True, exist_ok=True)
        self._entries: dict[str, str] = {}  # logical -> physical

    def stage(self, logical_path: str, source_file: Path, physical_name: str) -> str:
        """Copy source_file into the staging dir under physical_name."""
        dest = self.root / physical_name
        shutil.copyfile(source_file, dest)
        self._entries[logical_path] = physical_name
        return physical_name

    def manifest_path(self) -> Path:
        path = self.root / "manifest.json"
        entries = [
            {"logical_path": logical, "physical_path": physical}
            for logical, physical in self._entries.items()
        ]
        path.write_text(json.dumps({"entries": entries}), encoding="utf-8")
        return path