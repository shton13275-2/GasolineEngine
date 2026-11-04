#!/usr/bin/env python3
"""Extract the embedded Metal Shading Language sources from GpuRenderer.cpp.

The extracted files are used by the iOS CI job to run Apple's Metal compiler.
The application continues to use the embedded strings at runtime.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


EXPECTED_SHADER_NAMES = (
    "vertexSource",
    "fragmentSource",
    "shadowVertexSource",
    "shadowFragmentSource",
    "bloomVertexSource",
    "bloomFragmentSource",
    "overlayVertexSource",
    "overlayFragmentSource",
)

SHADER_PATTERN = re.compile(
    r'constexpr\s+char\s+(?P<name>[A-Za-z_]\w*)\s*\[\s*\]\s*=\s*'
    r'R"metal\((?P<source>.*?)\)metal"\s*;',
    re.DOTALL,
)


def extract_shaders(source_path: Path, output_dir: Path) -> list[Path]:
    source_text = source_path.read_text(encoding="utf-8")
    extracted: dict[str, str] = {}

    for match in SHADER_PATTERN.finditer(source_text):
        name = match.group("name")
        if name in extracted:
            raise ValueError(f"Duplicate embedded shader constant: {name}")
        extracted[name] = match.group("source")

    missing = [name for name in EXPECTED_SHADER_NAMES if name not in extracted]
    unexpected = sorted(set(extracted) - set(EXPECTED_SHADER_NAMES))
    if missing or unexpected:
        details = []
        if missing:
            details.append("missing: " + ", ".join(missing))
        if unexpected:
            details.append("unexpected: " + ", ".join(unexpected))
        raise ValueError("Embedded MSL source set changed (" + "; ".join(details) + ")")

    output_dir.mkdir(parents=True, exist_ok=True)
    output_paths: list[Path] = []
    for name in EXPECTED_SHADER_NAMES:
        shader_source = extracted[name]
        if not shader_source.strip():
            raise ValueError(f"Embedded shader source is empty: {name}")
        if not shader_source.endswith("\n"):
            shader_source += "\n"
        output_path = output_dir / f"{name}.metal"
        output_path.write_text(shader_source.lstrip("\r\n"), encoding="utf-8")
        output_paths.append(output_path)

    return output_paths


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Path to GpuRenderer.cpp")
    parser.add_argument("--output", type=Path, required=True, help="Directory for extracted .metal files")
    args = parser.parse_args()

    try:
        paths = extract_shaders(args.source, args.output)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"Extracted {len(paths)} embedded MSL sources:")
    for path in paths:
        print(f"  {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
