#!/usr/bin/env python3
"""Install only this local addon; preserve other addons and existing icon/config files."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--game-dir', required=True, type=Path)
args = parser.parse_args()
game = args.game_dir.expanduser().resolve()
if not (game/'Gw2-64.exe').is_file():
    parser.error('Expected the Guild Wars 2 folder containing Gw2-64.exe')
if not (game/'d3d11.dll').is_file():
    parser.error('Nexus must already be installed (missing d3d11.dll)')
dist = root/'dist'
manifest = json.loads((dist/'SHA256SUMS.json').read_text())
for relative, expected in manifest.items():
    path = dist/relative
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise SystemExit(f'Build checksum mismatch: {relative}')
addons = game/'addons'
target = addons/'MountsNexus.dll'
if target.exists():
    # Check mapped DLLs, not just whether some unrelated game process is running.
    for process in Path('/proc').iterdir():
        if not process.name.isdecimal():
            continue
        try:
            maps = (process/'maps').read_text()
        except (OSError, UnicodeError):
            continue
        if str(target) in maps:
            raise SystemExit('MountsNexus.dll is loaded. Close GW2 before updating it.')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    backup = addons/'MountsNexus'/'backups'/stamp
    backup.mkdir(parents=True)
    shutil.copy2(target,backup/'MountsNexus.dll')
    settings = addons/'MountsNexus'/'settings.json'
    if settings.exists():
        shutil.copy2(settings,backup/'settings.json')
    print(f'Backup: {backup}')
for source in sorted((dist/'MountsNexus').rglob('*')):
    if not source.is_file():
        continue
    destination = addons/'MountsNexus'/source.relative_to(dist/'MountsNexus')
    destination.parent.mkdir(parents=True,exist_ok=True)
    if destination.exists() and source.suffix.lower()=='.png':
        continue
    shutil.copy2(source,destination)
addons.mkdir(exist_ok=True)
temporary=addons/'MountsNexus.dll.installing'
shutil.copy2(dist/'MountsNexus.dll',temporary)
os.replace(temporary,target)
receipt = {'installed_at':datetime.now(timezone.utc).isoformat(), 'dll':str(target),
           'sha256':hashlib.sha256(target.read_bytes()).hexdigest()}
(root/'build'/'installation.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(f'Installed: {target}')
print('Ctrl+Shift+M opens settings; hold Ctrl+Alt+M for the radial.')
