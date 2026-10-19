"""Package only redistributable source files and matching release checksums."""
from pathlib import Path
import hashlib,json,zipfile
root=Path(__file__).resolve().parent.parent
release=root/'release';release.mkdir(exist_ok=True)
version=(root/'VERSION').read_text().strip()
items=[]
for directory in ['src','tools','installer','third_party']:
    items.extend(p for p in (root/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc')
items.extend(root/name for name in ['README.md','README.zh-CN.md','LICENSE','CMakeLists.txt','VERSION','.gitignore'])
manifest={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in items}
archive=release/f'LiDE25-Win10-x64-Source-{version}.zip'
prefix=f'LiDE25-Source-{version}/'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=7) as z:
    for p in sorted(items):z.write(p,prefix+p.relative_to(root).as_posix())
    z.writestr(prefix+'SHA256SUMS.json',json.dumps(manifest,indent=2))
exe=release/f'LiDE25-Win10-x64-Setup-{version}.exe'
hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [exe,archive]}
(release/f'SHA256SUMS-{version}.txt').write_text(''.join(h+'  '+name+'\n' for name,h in hashes.items()),encoding='utf-8')
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    for name,h in manifest.items():assert hashlib.sha256(z.read(prefix+name)).hexdigest()==h
print(json.dumps({'source_files_verified':len(manifest),'artifacts':hashes}))
