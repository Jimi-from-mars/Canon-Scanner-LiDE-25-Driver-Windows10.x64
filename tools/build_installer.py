"""Build the offline native setup helper and NSIS wizard. Build dependencies only need network once."""
import argparse,hashlib,json,os,pathlib,shutil,subprocess,sys
root=pathlib.Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser();parser.add_argument('--helper-only',action='store_true');opt=parser.parse_args()
work=root/'artifacts/setup';payload=work/'payload';generated=work/'generated'
(root/'release').mkdir(exist_ok=True)
payload.mkdir(parents=True,exist_ok=True);generated.mkdir(exist_ok=True)
temp=root/'artifacts/temp';temp.mkdir(exist_ok=True);env=dict(os.environ,TMP=str(temp),TEMP=str(temp))
driver=root/'artifacts/native/Lide25Wia.dll';digest=hashlib.sha256(driver.read_bytes()).hexdigest()
twain={bits:root/f'artifacts/native/Lide25Twain{bits}.ds' for bits in [32,64]}
header='#pragma once\n#define L25_DRIVER_SHA256 "'+digest+'"\n'
for bits,file in twain.items():header+=f'#define L25_TWAIN{bits}_SHA256 "'+hashlib.sha256(file.read_bytes()).hexdigest()+'"\n'
for bits in [32,64]:header+=f'#define L25_DSM{bits}_SHA256 "'+hashlib.sha256((root/f'third_party/twain-dsm/runtime/{bits}/TWAINDSM.dll').read_bytes()).hexdigest()+'"\n'
(generated/'setup_manifest.h').write_text(header)
gcc=shutil.which('g++.exe')
cmd=[gcc,'-std=c++17','-O2','-g0','-s','-ffile-prefix-map='+str(root)+'=.','-ffile-prefix-map='+root.as_posix()+'=.','-D_WIN32_WINNT=0x0A00','-municode','-static','-I'+str(generated),'-I'+str(root/'third_party/wia-reference'),
     str(root/'src/setup/setup_helper.cpp'),str(root/'artifacts/native/usbscan_transport.o'),
     '-lsetupapi','-lcfgmgr32','-lbcrypt','-ladvapi32','-lole32','-loleaut32','-luuid','-o',str(payload/'lide25-setup-helper.exe')]
p=subprocess.run(cmd,env=env,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(work/'helper-build.log').write_bytes(p.stdout)
if p.returncode:print(p.stdout.decode(errors='replace')[-18000:]);raise SystemExit(p.returncode)
shutil.copy2(driver,payload/'Lide25Wia.dll')
for bits,file in twain.items():shutil.copy2(file,payload/file.name)
for bits in [32,64]:shutil.copy2(root/f'third_party/twain-dsm/runtime/{bits}/TWAINDSM.dll',payload/f'TwainDsm{bits}.dll')
shutil.copy2(root/'LICENSE',payload/'LICENSE.txt')
licenses=payload/'licenses'
if licenses.exists():
    if licenses.resolve().parent!=payload.resolve():raise SystemExit('License output escaped the generated payload directory')
    shutil.rmtree(licenses)
licenses.mkdir()
for directory in ['sane','wia-reference','runtime-licenses','nlohmann','twain','twain-dsm']:
    source=root/'third_party'/directory;target=licenses/directory;target.mkdir(exist_ok=True)
    for f in source.iterdir():
        if f.is_file() and (f.name.startswith(('LICENSE','COPYING')) or (directory=='runtime-licenses' and f.name!='UPSTREAM.json')):
            shutil.copy2(f,target/f.name)
print('Built setup helper. Driver SHA256:',digest,flush=True)
if opt.helper_only:raise SystemExit(0)
nsis=list((root/'toolchain').glob('nsis-*/makensis.exe'))
if not nsis and shutil.which('makensis.exe'):nsis=[pathlib.Path(shutil.which('makensis.exe'))]
if len(nsis)!=1:raise SystemExit('Put NSIS 3 on PATH, or install one portable copy at toolchain/nsis-*/makensis.exe')
if (nsis[0].parent/'COPYING').is_file():shutil.copy2(nsis[0].parent/'COPYING',licenses/'NSIS-COPYING.txt')
subprocess.run([str(nsis[0]),'/V3','/INPUTCHARSET','UTF8',str(root/'installer/lide25.nsi')],env=env,cwd=root,check=True)
