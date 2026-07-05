"""
Integration tests for igpack-bundle, dup-name-failure plan.

Plan file : test-definitions/dup-name-failure.igpack-bundle.json

The plan declares two asset sources with the same igasset_name ("glass"),
which igpack-bundle must reject before writing any output.  The expected
behaviour is:

  - non-zero exit code,
  - no <output_path> file produced under the output directory,
  - a clear error mentioning the duplicate name on stderr.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from test_base import run_igpack_bundle

_PLAN_FILENAME = "dup-name-failure.igpack-bundle.json"
_OUTPUT_FILENAME = "dup-name-failure.igpack"

# Substring of the spdlog error emitted from tools/igpack-bundle/main.cc when
# duplicate asset names are detected ("Cannot encode two assets with the same
# name <name>, aborting!"); kept loose so a punctuation tweak does not break
# this test.
_EXPECTED_STDERR_SUBSTR = "Cannot encode two assets with the same name"


@pytest.fixture(scope="module")
def bundle_result(
    igpack_bundle_bin: Path,
    prep_igassets_dir: Path,
    test_definitions_dir: Path,
    tmp_path_factory: pytest.TempPathFactory,
) -> tuple[int, Path, str, str]:
    """Run igpack-bundle once; do NOT raise on non-zero exit."""
    output_dir = tmp_path_factory.mktemp("dup_name_failure_out")
    plan = test_definitions_dir / _PLAN_FILENAME
    proc = run_igpack_bundle(
        bin_path=igpack_bundle_bin,
        plan_json=plan,
        asset_root=prep_igassets_dir,
        output_dir=output_dir,
        clean_build=True,
    )
    return proc.returncode, output_dir, proc.stdout, proc.stderr


def test_exits_nonzero(bundle_result: tuple[int, Path, str, str]) -> None:
    returncode, _, _, _ = bundle_result
    assert returncode != 0, (
        "igpack-bundle exited zero even though the plan declares duplicate "
        "asset names; expected failure."
    )


def test_output_file_not_created(bundle_result: tuple[int, Path, str, str]) -> None:
    _, output_dir, _, _ = bundle_result
    out = output_dir / _OUTPUT_FILENAME
    assert not out.exists(), (
        f"igpack-bundle wrote {_OUTPUT_FILENAME} despite the duplicate-name "
        f"error; no output should be produced for failed bundles."
    )


def test_error_mentions_duplicate(bundle_result: tuple[int, Path, str, str]) -> None:
    """
    spdlog typically routes errors to stdout via the default stdout color sink,
    but accept either stream to stay resilient against logger reconfiguration.
    """
    _, _, stdout, stderr = bundle_result
    combined = stdout + stderr
    assert _EXPECTED_STDERR_SUBSTR in combined, (
        f"Expected substring {_EXPECTED_STDERR_SUBSTR!r} in igpack-bundle "
        f"output.\nstdout:\n{stdout}\nstderr:\n{stderr}"
    )
