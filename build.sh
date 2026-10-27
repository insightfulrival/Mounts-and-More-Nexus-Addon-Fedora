#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
if [[ -n "${ZIG:-}" ]]; then
  compiler=("$ZIG" c++)
elif command -v zig >/dev/null; then
  compiler=(zig c++)
elif command -v python-zig >/dev/null; then
  compiler=(python-zig c++)
else
  echo 'Set ZIG=/absolute/path/to/zig (tested: 0.14.1), or put zig on PATH.' >&2
  exit 1
fi
mkdir -p build dist/MountsNexus/icons
python3 tools/generate_lakes.py
"${compiler[@]}" -std=c++17 -O2 -UNDEBUG tests/core_test.cpp -o build/core_test
./build/core_test
common=(-std=c++17 -O2 -Ivendor -Ivendor/imgui -Isrc -Wno-format-security)
"${compiler[@]}" -target x86_64-windows-gnu "${common[@]}" -shared -static \
  src/addon.cpp vendor/imgui/imgui.cpp vendor/imgui/imgui_draw.cpp \
  vendor/imgui/imgui_widgets.cpp vendor/imgui/imgui_tables.cpp \
  -luser32 -lgdi32 -limm32 -lwinhttp -o dist/MountsNexus.dll
"${compiler[@]}" -target x86_64-windows-gnu "${common[@]}" -UNDEBUG -static \
  tests/adapter_test.cpp vendor/imgui/imgui.cpp vendor/imgui/imgui_draw.cpp \
  vendor/imgui/imgui_widgets.cpp vendor/imgui/imgui_tables.cpp \
  -luser32 -lgdi32 -limm32 -lwinhttp -o build/adapter_test.exe
cp assets/*.png dist/MountsNexus/icons/
python3 tools/verify_pe.py dist/MountsNexus.dll
python3 tools/package.py
