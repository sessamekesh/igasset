"""
pytest configuration for the extract-asset-metadata integration test suite.

Binary paths are resolved via CLI options (highest priority) or environment
variables (fallback).  The asset root is auto-detected from the repository
layout but can be overridden the same way.

Usage examples
--------------
# Via CLI options:
  pytest tools/extract-asset-metadata/e2e/ \
      --extract-asset-metadata-bin=<build>/extract-asset-metadata

# Via environment variables:
  EXTRACT_ASSET_METADATA_BIN=... pytest tools/extract-asset-metadata/e2e/
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

# ---------------------------------------------------------------------------
# Repository root detection
# ---------------------------------------------------------------------------

# e2e/ lives at  <repo>/tools/extract-asset-metadata/e2e/
_E2E_DIR = Path(__file__).resolve().parent
_REPO_ROOT = _E2E_DIR.parents[2]  # tools/extract-asset-metadata/e2e -> tools -> repo


# ---------------------------------------------------------------------------
# pytest CLI option registration
# ---------------------------------------------------------------------------

def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption(
        "--extract-asset-metadata-bin",
        default=None,
        help=(
            "Path to the extract-asset-metadata binary (overrides "
            "EXTRACT_ASSET_METADATA_BIN env var)."
        ),
    )
    parser.addoption(
        "--asset-root",
        default=None,
        help=(
            "Root directory containing raw test assets (overrides "
            "IGASSET_TEST_ASSET_ROOT env var). Defaults to <repo>/test_assets."
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
def extract_asset_metadata_bin(request: pytest.FixtureRequest) -> Path:
    """Absolute path to the extract-asset-metadata binary."""
    return _resolve_path(
        request,
        cli_opt="extract-asset-metadata-bin",
        env_var="EXTRACT_ASSET_METADATA_BIN",
        description="extract-asset-metadata binary",
    )


@pytest.fixture(scope="session")
def asset_root(request: pytest.FixtureRequest) -> Path:
    """Root directory that contains raw test input assets."""
    return _resolve_path(
        request,
        cli_opt="asset-root",
        env_var="IGASSET_TEST_ASSET_ROOT",
        default=_REPO_ROOT / "test_assets",
        description="test asset root directory",
    )