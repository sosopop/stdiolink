#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
from pathlib import Path

from PIL import Image


DEFAULT_ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert an SVG file into PNG and ICO assets."
    )
    parser.add_argument(
        "svg",
        type=Path,
        help="Path to the source SVG file.",
    )
    parser.add_argument(
        "--png",
        type=Path,
        help="Output PNG path. Defaults to the SVG path with a .png suffix.",
    )
    parser.add_argument(
        "--ico",
        type=Path,
        help="Output ICO path. Defaults to the SVG path with a .ico suffix.",
    )
    parser.add_argument(
        "--png-size",
        type=int,
        default=512,
        help="Square PNG output size in pixels. Default: 512.",
    )
    return parser.parse_args()


def render_svg_to_png(svg_path: Path, png_path: Path, png_size: int) -> None:
    png_path.parent.mkdir(parents=True, exist_ok=True)
    render_error: Exception | None = None

    try:
        import cairosvg

        cairosvg.svg2png(
            url=svg_path.resolve().as_uri(),
            write_to=str(png_path),
            output_width=png_size,
            output_height=png_size,
        )
        return
    except Exception as exc:
        render_error = exc

    try:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

        from PyQt6.QtCore import QByteArray, QRectF
        from PyQt6.QtGui import QGuiApplication, QImage, QPainter
        from PyQt6.QtSvg import QSvgRenderer

        app = QGuiApplication.instance() or QGuiApplication([])
        _ = app

        svg_bytes = svg_path.read_bytes()
        renderer = QSvgRenderer(QByteArray(svg_bytes))
        if not renderer.isValid():
            raise RuntimeError(f"Failed to load SVG: {svg_path}")

        image = QImage(png_size, png_size, QImage.Format.Format_ARGB32)
        image.fill(0)

        painter = QPainter(image)
        renderer.render(painter, QRectF(0, 0, png_size, png_size))
        painter.end()

        if not image.save(str(png_path), "PNG"):
            raise RuntimeError(f"Failed to save PNG: {png_path}")
        return
    except Exception as exc:
        if render_error is not None:
            raise RuntimeError(
                "Unable to render SVG with CairoSVG or PyQt6. "
                f"CairoSVG error: {render_error}. PyQt6 error: {exc}"
            ) from exc
        raise


def render_png_to_ico(png_path: Path, ico_path: Path) -> None:
    ico_path.parent.mkdir(parents=True, exist_ok=True)
    with Image.open(png_path) as image:
        rgba_image = image.convert("RGBA")
        rgba_image.save(str(ico_path), format="ICO", sizes=[(size, size) for size in DEFAULT_ICO_SIZES])


def main() -> int:
    args = parse_args()

    svg_path = args.svg.resolve()
    if not svg_path.is_file():
        raise FileNotFoundError(f"SVG file not found: {svg_path}")
    if args.png_size <= 0:
        raise ValueError("--png-size must be a positive integer")

    png_path = args.png.resolve() if args.png else svg_path.with_suffix(".png")
    ico_path = args.ico.resolve() if args.ico else svg_path.with_suffix(".ico")

    render_svg_to_png(svg_path, png_path, args.png_size)
    render_png_to_ico(png_path, ico_path)

    print(f"SVG : {svg_path}")
    print(f"PNG : {png_path}")
    print(f"ICO : {ico_path}")
    print(f"Size: {args.png_size}x{args.png_size}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
