"""
pytest configuration for the igasset-gen integration test suite.

Binary paths are resolved via CLI options (highest priority) or environment
variables (fallback).  The asset root and schema file are auto-detected from
the repository layout but can be overridden the same way.

Usage examples
--------------
# Via CLI options:
  pytest tools/igasset-gen/e2e/ \
      --igasset-gen-bin=<build>/igasset-gen \
      --enumerate-igasset-bin=<build>/enumerate-igasset

# Via environment variables:
  IGASSET_GEN_BIN=... ENUMERATE_IGASSET_BIN=... pytest tools/igasset-gen/e2e/
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

# ---------------------------------------------------------------------------
# Repository root detection
# ---------------------------------------------------------------------------

# e2e/ lives at  <repo>/tools/igasset-gen/e2e/
_E2E_DIR = Path(__file__).resolve().parent
_REPO_ROOT = _E2E_DIR.parents[2]  # tools/igasset-gen/e2e -> tools/igasset-gen -> tools -> repo


# ---------------------------------------------------------------------------
# pytest CLI option registration
# ---------------------------------------------------------------------------

def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption(
        "--igasset-gen-bin",
        default=None,
        help="Path to the igasset-gen binary (overrides IGASSET_GEN_BIN env var).",
    )
    parser.addoption(
        "--enumerate-igasset-bin",
        default=None,
        help="Path to the enumerate-igasset binary (overrides ENUMERATE_IGASSET_BIN env var).",
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
        "--schema",
        default=None,
        help=(
            "Path to igasset-gen-plan.fbs (overrides IGASSET_SCHEMA env var). "
            "Defaults to <repo>/schema/igasset-gen-plan.fbs."
        ),
    )


# ---------------------------------------------------------------------------
# Session-scoped fixtures
# ---------------------------------------------------------------------------

def _resolve_path(
    request: pytest.FixtureRequest,
    cli_opt: str,
    env_var: str,
    default: Path | None = None,
    *,
    must_exist: bool = True,
    description: str = "",
) -> Path:
    """Resolve a path from CLI option → env var → default, with existence check."""
    raw: str | None = request.config.getoption(f"--{cli_opt}") or os.environ.get(env_var)
    if raw is not None:
        p = Path(raw)
    elif default is not None:
        p = default
    else:
        pytest.fail(
            f"Required path not provided. "
            f"Set --{cli_opt.lstrip('-')} or {env_var}. {description}"
        )

    if must_exist and not p.exists():
        pytest.fail(f"Path does not exist: {p}  ({description or cli_opt})")

    return p.resolve()


@pytest.fixture(scope="session")
def igasset_gen_bin(request: pytest.FixtureRequest) -> Path:
    """Absolute path to the igasset-gen binary."""
    return _resolve_path(
        request,
        cli_opt="igasset-gen-bin",
        env_var="IGASSET_GEN_BIN",
        description="igasset-gen binary",
    )


@pytest.fixture(scope="session")
def enumerate_igasset_bin(request: pytest.FixtureRequest) -> Path:
    """Absolute path to the enumerate-igasset binary."""
    return _resolve_path(
        request,
        cli_opt="enumerate-igasset-bin",
        env_var="ENUMERATE_IGASSET_BIN",
        description="enumerate-igasset binary",
    )


@pytest.fixture(scope="session")
def asset_root(request: pytest.FixtureRequest) -> Path:
    """Root directory that contains raw test input assets (e.g. .wgsl, .png)."""
    return _resolve_path(
        request,
        cli_opt="asset-root",
        env_var="IGASSET_TEST_ASSET_ROOT",
        default=_REPO_ROOT / "test_assets",
        description="test asset root directory",
    )


@pytest.fixture(scope="session")
def schema_path(request: pytest.FixtureRequest) -> Path:
    """Absolute path to igasset-gen-plan.fbs (passed as -s to igasset-gen)."""
    return _resolve_path(
        request,
        cli_opt="schema",
        env_var="IGASSET_SCHEMA",
        default=_REPO_ROOT / "schema" / "igasset-gen-plan.fbs",
        description="igasset-gen-plan.fbs schema file",
    )


@pytest.fixture(scope="session")
def test_definitions_dir() -> Path:
    """Directory containing the .igasset-gen.json plan files shipped with the tests."""
    p = _E2E_DIR.parent / "test-definitions"
    if not p.is_dir():
        pytest.fail(f"test-definitions directory not found: {p}")
    return p
