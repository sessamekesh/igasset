"""
pytest configuration for the igpack-bundle integration test suite.

Binary paths are resolved via CLI options (highest priority) or environment
variables (fallback). The asset root is auto-detected from the repository
layout but can be overridden the same way.

Usage examples
--------------
# Via CLI options:
  pytest tools/igpack-bundle/e2e/ \
    --igasset-gen-bin=<build>/igasset-gen \
    --igpack-bundle-bin=<build>/igpack-bundle \
    --enumerate-igpack-bin=<build>/enumerate-igpack

# Via environment variables:
  IGASSET_GEN_BIN=... IGPACK_BUNDLE_BIN=... ENUERATE_IGPACK_BIN=... pytest tools/igasset-gen/e2e/
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

from test_base import run_igasset_gen

#
# Repository root detection

# e2e/ lives at <repo>/tools/igpack-bundle/e2e
_E2E_DIR = Path(__file__).resolve().parent
_REPO_ROOT = _E2E_DIR.parents[2]

#
# pytest CLI option registration

def pytest_addoption(parser: pytest.Parser) -> None:
  parser.addoption(
    "--igasset-gen-bin",
    default=None,
    help="Path to the igasset-gen binary (overrides IGASSET_GEN_BIN env var).",
  )
  parser.addoption(
    "--igpack-bundle-bin",
    default=None,
    help="Path to the igpack-bundle binary (overrides IGPACK_BUNDLE_BIN env var).",
  )
  parser.addoption(
    "--asset-root",
    default=None,
    help=(
      "Root directory containing raw test assets (overrides "
      "IGASSET_TEST_ASSET_ROOT env var). Defaults to <repo>/test_assets."
    ),
  )
  parser.addoption(
    "--enumerate-igpack-bin",
    default=None,
    help="Path to the enumerate-igpack binary (overrides ENUMERATE_IGPACK_BIN env var).",
  )

#
# Session-scoped fixtures

def _resolve_path(
  request: pytest.FixtureRequest,
  cli_opt: str,
  env_var: str,
  default: Path | None = None,
  *,
  must_exist: bool = True,
  description: str = "",
) -> Path:
  """Resolve a path from CLI option -> env var -> default, with existence check"""
  raw: str | None = request.config.getoption(f"--{cli_opt}") or os.environ.get(env_var)
  if raw is not None:
    p = Path(raw)
  elif default is not None:
    p = default
  else:
    stripped = cli_opt.lstrip("-")
    pytest.fail(
      f"Required path not provided. "
      f"Set --{stripped} or {env_var}. {description}"
    )

  if must_exist and not p.exists():
    pytest.fail(f"Path does not exist: {p} ({description or cli_opt})")

  return p.resolve()

@pytest.fixture(scope="session")
def igasset_gen_bin(request: pytest.FixtureRequest) -> Path:
  """Absolute path to the igasset-gen binary."""
  return _resolve_path(
    request,
    cli_opt="igasset-gen-bin",
    env_var="IGASSET_GEN_BIN",
    description="igasset-gen binary"
  )

@pytest.fixture(scope="session")
def igpack_bundle_bin(request: pytest.FixtureRequest) -> Path:
  """Absolute path to the igpack-bundle binary."""
  return _resolve_path(
    request,
    cli_opt="igpack-bundle-bin",
    env_var="IGPACK_BUNDLE_BIN",
    description="igpack-bin binary"
  )

@pytest.fixture(scope="session")
def enumerate_igpack_bin(request: pytest.FixtureRequest) -> Path:
  """Absolute path to the enumerate-igpack binary."""
  return _resolve_path(
    request,
    cli_opt="enumerate-igpack-bin",
    env_var="ENUMERATE_IGPACK_BIN",
    description="enumerate-igpack binary"
  )

@pytest.fixture(scope="session")
def asset_root(request: pytest.FixtureRequest) -> Path:
  """Root directory that contains raw test input assets."""
  return _resolve_path(
    request,
    cli_opt="asset-root",
    env_var="IGASSET_TEST_ASSET_ROOT",
    default=_REPO_ROOT / "test_assets",
    description="test asset root directory"
  )

@pytest.fixture(scope="session")
def test_definitions_dir() -> Path:
  """Directory containing the .igasset-gen.json / .igpack-bundle.json plan files shipped with the tests."""
  p = _E2E_DIR / "test-definitions"
  if not p.is_dir():
    pytest.fail(f"test-definitions directory not found: {p}")
  return p

@pytest.fixture(scope="session")
def prep_igassets_dir(
  igasset_gen_bin: Path,
  asset_root: Path,
  test_definitions_dir: Path,
  tmp_path_factory: pytest.TempPathFactory,
) -> Path:
  """
  Run igasset-gen once per session against test-prep.igasset-gen.json and
  return the directory containing the generated .igasset files.

  These are the inputs that every igpack-bundle test below consumes as its
  '-i' root.  The output directory is created by tmp_path_factory and is
  auto-cleaned by pytest after the run.
  """
  out = tmp_path_factory.mktemp("prep_igassets")
  plan = test_definitions_dir / "test-prep.igasset-gen.json"
  if not plan.is_file():
    pytest.fail(f"prep plan not found: {plan}")
  proc = run_igasset_gen(
    bin_path=igasset_gen_bin,
    plan_json=plan,
    asset_root=asset_root,
    output_dir=out,
  )
  if proc.returncode != 0:
    pytest.fail(
      f"igasset-gen prep failed (exit {proc.returncode}).\n"
      f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
    )
  return out