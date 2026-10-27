#!/usr/bin/env python3
from pathlib import Path
import hashlib
import json
import zipfile

root = Path(__file__).resolve().parents[1]
dist = root/'dist'
licenses = dist/'MountsNexus'/'licenses'
licenses.mkdir(parents=True,exist_ok=True)
for source, name in ((root/'vendor'/'Nexus.LICENSE','Nexus.txt'),
                     (root/'vendor'/'Mumble.LICENSE','Mumble.txt'),
                     (root/'vendor'/'json.LICENSE','nlohmann-json.txt'),
                     (root/'vendor'/'imgui'/'LICENSE.txt','Dear-ImGui.txt')):
    (licenses/name).write_bytes(source.read_bytes())
files = [dist/'MountsNexus.dll', *sorted((dist/'MountsNexus'/'icons').glob('*.png')), *sorted(licenses.glob('*.txt'))]
for name in ('README.md', 'THIRD_PARTY.md'):
    source = root/name
    if source.exists():
        dest = dist/'MountsNexus'/name
        dest.write_bytes(source.read_bytes())
        files.append(dest)
manifest = {str(p.relative_to(dist)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
(dist/'SHA256SUMS.json').write_text(json.dumps(manifest, indent=2)+'\n')
with zipfile.ZipFile(dist/'MountsNexus-local-0.1.1.zip','w',zipfile.ZIP_DEFLATED) as archive:
    for path in files:
        archive.write(path, str(path.relative_to(dist)))
    archive.write(dist/'SHA256SUMS.json','SHA256SUMS.json')
print(f'Packaged {len(files)} files: {dist}/MountsNexus-local-0.1.1.zip')
