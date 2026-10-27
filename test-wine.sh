#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
runner="${WINE:-wine}"
if ! command -v "$runner" >/dev/null; then
  echo 'Set WINE=/absolute/path/to/wine. This test uses an isolated prefix.' >&2
  exit 1
fi
if [[ ! -f build/adapter_test.exe ]]; then
  echo 'Run build.sh first.' >&2
  exit 1
fi
export WINEPREFIX="$PWD/build/wineprefix" WINEARCH=win64 WINEDEBUG=-all
export WINEDLLOVERRIDES='winemenubuilder.exe=d;mscoree=d;mshtml=d'
"$runner" "$PWD/build/adapter_test.exe" "$PWD/dist/MountsNexus.dll" 2>&1 | tee build/adapter-test.log
