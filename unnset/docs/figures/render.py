#!/usr/bin/env python3
"""Render the holistic unnesting TikZ figures as SVG assets."""

from __future__ import annotations

import fnmatch
import os
from pathlib import Path
import subprocess
import sys
import tempfile


SOURCE_GLOB = "holistic-[0-9][0-9]-*.tex"
FIGURES_DIR = Path(__file__).resolve().parent
ASSETS_DIR = FIGURES_DIR.parent / "assets"
BUILD_ROOT = FIGURES_DIR / "build"


def _matches_source_name(name: str) -> bool:
    return Path(name).name == name and fnmatch.fnmatchcase(name, SOURCE_GLOB)


def _atomic_write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None

    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as temporary_file:
            temporary_path = Path(temporary_file.name)
            temporary_file.write(content)

        os.replace(temporary_path, path)
    except BaseException:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
        raise


def render(tex_path: Path, assets_dir: Path, build_root: Path) -> Path:
    """Compile one TikZ source and atomically publish its single-page SVG."""
    build_dir = build_root / tex_path.stem

    try:
        if not _matches_source_name(tex_path.name):
            raise ValueError(f"source name does not match {SOURCE_GLOB}")
        if not tex_path.is_file():
            raise FileNotFoundError(f"source file does not exist: {tex_path}")

        build_dir.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                "latexmk",
                "-pdf",
                "-halt-on-error",
                "-interaction=nonstopmode",
                f"-outdir={build_dir.resolve()}",
                tex_path.name,
            ],
            cwd=tex_path.parent,
            check=True,
        )

        pdf_path = build_dir / f"{tex_path.stem}.pdf"

        import fitz

        with fitz.open(pdf_path) as document:
            if document.page_count != 1:
                raise ValueError(
                    "expected exactly one PDF page, "
                    f"found {document.page_count}"
                )
            svg = document[0].get_svg_image(text_as_path=True)

        svg_path = assets_dir / f"{tex_path.stem}.svg"
        _atomic_write_text(svg_path, svg)
        return svg_path
    except Exception as error:
        raise RuntimeError(
            f"{tex_path.name}: render failed; build directory: {build_dir}: {error}"
        ) from error


def main(argv: list[str]) -> int:
    """Render one requested source, or every matching source when none is named."""
    if len(argv) > 1:
        print(
            f"error: expected at most one filename matching {SOURCE_GLOB}; "
            f"build directory: {BUILD_ROOT}",
            file=sys.stderr,
        )
        return 2

    if argv:
        source_name = argv[0]
        build_dir = BUILD_ROOT / Path(source_name).stem
        tex_path = FIGURES_DIR / source_name
        if not _matches_source_name(source_name) or not tex_path.is_file():
            print(
                f"error: {source_name}: expected an existing filename matching "
                f"{SOURCE_GLOB}; build directory: {build_dir}",
                file=sys.stderr,
            )
            return 2
        tex_paths = [tex_path]
    else:
        tex_paths = sorted(FIGURES_DIR.glob(SOURCE_GLOB))

    for tex_path in tex_paths:
        try:
            render(tex_path, ASSETS_DIR, BUILD_ROOT)
        except Exception as error:
            print(f"error: {error}", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
