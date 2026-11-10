# Fluent UI icons

These icons use PySide6 Fluent Widgets 1.11.3 resources, under the included GPLv3
license. The Stop icon comes from ok-script Qt resources.
`provenance.json` records the exact resource paths and SVG SHA-256 hashes.
`source` retains the original SVG files. ChevronUp is ChevronDown rotated 180
degrees for the expanding-card control.

PNG files are rendered from these SVGs at multiple physical pixel sizes and
compiled into Qt resources. The C++ interface does not load the
Python Fluent Widgets package. Portable source builds use the checked-in SVG
and PNG assets directly; they need no asset-generation step.

Repository-only development exporter: `tools_dualpc/export_classic_icons.py`.
