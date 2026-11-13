#!/bin/sh
# Rebuild the storage-mode art, table and review sheets. Run from the repo root.
set -e
W=workspace/storagegfx
swift $W/render-art.swift brand/fonts $W/art
python3 $W/storagegfx.py artifacts/nor-analysis/r1-run-20261107/nor-after-r1.bin artifacts/nor-analysis/bootloader.out $W/art $W/out
swift $W/sheets.swift brand/fonts $W/out docs/storage-gfx-mockups
