"""Build 32- and 64-bit TWAIN sources without bundling a private scanner core."""
from pathlib import Path
import os,shutil,subprocess
root=Path(__file__).resolve().parent.parent
out=root/'artifacts/native';out.mkdir(parents=True,exist_ok=True)
temp=root/'artifacts/temp';temp.mkdir(exist_ok=True)
env=dict(os.environ,TMP=str(temp),TEMP=str(temp))
for bits,prefix in [(32,'i686'),(64,'x86_64')]:
    compiler=os.environ.get(f'CXX{bits}') or shutil.which(f'{prefix}-w64-mingw32-g++.exe') or shutil.which(f'{prefix}-w64-mingw32-clang++.exe')
    if bits==64 and not compiler:compiler=shutil.which('g++.exe')
    if not compiler:raise SystemExit(f'Put a {prefix} MinGW-w64 C++ compiler on PATH or set CXX{bits}. LLVM-MinGW provides both architectures.')
    resource=out/f'twain{bits}-version.o'
    windres=shutil.which('windres.exe')
    if not windres:raise SystemExit('MinGW windres.exe must be on PATH')
    subprocess.run([windres,'--target='+('pe-i386' if bits==32 else 'pe-x86-64'),str(root/'src/twain/version.rc'),'-o',str(resource)],cwd=root,env=env,check=True)
    cmd=[compiler,'-std=c++17','-O2','-g0','-s','-static','-shared','-D_WIN32_WINNT=0x0A00','-DWIN32','-D_WINDOWS',
         '-ffile-prefix-map='+str(root)+'=.','-ffile-prefix-map='+root.as_posix()+'=.',
         '-I'+str(root/'third_party/wia-reference'),str(root/'src/twain/twain_source.cpp'),str(root/'src/twain/wia_acquire.cpp'),
         str(resource),'-Wl,--kill-at','-lole32','-loleaut32','-luuid','-ladvapi32','-lgdi32','-luser32','-o',str(out/f'Lide25Twain{bits}.ds')]
    p=subprocess.run(cmd,cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (out/f'twain{bits}-build.log').write_bytes(p.stdout)
    if p.returncode:print(p.stdout.decode(errors='replace')[-14000:]);raise SystemExit(p.returncode)
    print(f'Built Lide25Twain{bits}.ds',flush=True)
    client=[compiler,'-std=c++17','-O2','-g0','-s','-static','-municode','-DWIN32','-D_WINDOWS',
            '-ffile-prefix-map='+str(root)+'=.','-ffile-prefix-map='+root.as_posix()+'=.',str(root/'tools/twain_client.cpp'),'-o',str(out/f'lide25-twain-client{bits}.exe')]
    p=subprocess.run(client,cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (out/f'twain{bits}-client-build.log').write_bytes(p.stdout)
    if p.returncode:print(p.stdout.decode(errors='replace')[-14000:]);raise SystemExit(p.returncode)
