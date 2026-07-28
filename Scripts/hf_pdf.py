"""Rasterises a PDF sheet set to one PNG per page.

AutoCAD sets almost always arrive as multi-page PDFs, and Unreal cannot read a PDF. The editor's
import action shells out to this script through the same local Scripts/.venv the drawing generator
uses, so the dependency stays inside the plugin and no system Python is touched.

Prints one produced path per line on stdout; the editor reads those back.
"""

from __future__ import annotations

import argparse
import os
import sys


def rasterise(pdf_path: str, out_dir: str, dpi: int) -> list[str]:
    try:
        import pymupdf  # PyMuPDF >= 1.24 exposes the package under this name
    except ImportError:  # pragma: no cover - older wheels only ship the legacy name
        try:
            import fitz as pymupdf
        except ImportError:
            raise SystemExit(
                "PyMuPDF is not installed in the HouseForge Python environment.\n"
                "Run Scripts/hf-drawings.ps1 once to provision it."
            )

    os.makedirs(out_dir, exist_ok=True)
    stem = os.path.splitext(os.path.basename(pdf_path))[0]

    produced: list[str] = []
    # 72 dpi is PDF user space, so this is the scale factor to reach the requested density.
    zoom = dpi / 72.0
    matrix = pymupdf.Matrix(zoom, zoom)

    with pymupdf.open(pdf_path) as document:
        if document.page_count == 0:
            raise SystemExit(f"'{pdf_path}' has no pages.")

        # Single-page sets keep the file's own name; multi-page ones get a page suffix, so a
        # sheet set lands as readable, individually-named drawings rather than one blob.
        for index, page in enumerate(document, start=1):
            suffix = "" if document.page_count == 1 else f"-p{index:02d}"
            target = os.path.join(out_dir, f"{stem}{suffix}.png")
            page.get_pixmap(matrix=matrix, alpha=False).save(target)
            produced.append(target)

    return produced


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pdf", required=True, help="Source PDF.")
    ap.add_argument("--out", required=True, help="Destination directory for the PNG pages.")
    ap.add_argument("--dpi", type=int, default=200,
                    help="Raster density. 200 keeps dimension strings legible without huge files.")
    args = ap.parse_args()

    if not os.path.isfile(args.pdf):
        print(f"No such file: {args.pdf}", file=sys.stderr)
        return 2

    for path in rasterise(args.pdf, args.out, args.dpi):
        print(path)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
