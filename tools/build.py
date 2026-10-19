"""Build native Windows user-mode driver components using the available MinGW toolchain."""
import argparse, json, os, pathlib, shutil, subprocess, sys
root=pathlib.Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser();parser.add_argument('--wia-only',action='store_true');args=parser.parse_args()
out=root/'artifacts/native';out.mkdir(parents=True,exist_ok=True)
temp=root/'artifacts/temp';temp.mkdir(exist_ok=True)
env=dict(os.environ,TMP=str(temp),TEMP=str(temp))
gcc=shutil.which('gcc.exe');gxx=shutil.which('g++.exe')
if not gcc:raise SystemExit('A native Windows GCC toolchain is required')
flags=['-g0','-s','-O1','-D_WIN32_WINNT=0x0A00','-DWINVER=0x0A00',
       '-ffile-prefix-map='+str(root)+'=.', '-ffile-prefix-map='+root.as_posix()+'=.',
       '-I'+str(root/'src/transport'),'-I'+str(root/'src/core/compat'),'-I'+str(root/'third_party/sane/include')]
def run(args,log):
    p=subprocess.run(args,cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (out/log).write_bytes(p.stdout)
    if p.returncode:
        print(p.stdout.decode(errors='replace')[-14000:]);raise SystemExit(p.returncode)
    if p.stdout:print(p.stdout.decode(errors='replace')[-1500:])
    print('Built',args[-1],flush=True)
if not args.wia_only:
    run([sys.executable,'-B',str(root/'tools/prepare_sane.py')],'prepare.log')
    run([gcc,*flags,str(root/'src/transport/usbscan_transport.c'),str(root/'tools/probe.c'),'-lsetupapi','-o',str(out/'lide25-probe.exe')],'probe-build.log')
sources=['third_party/sane/backend/plustek.c','third_party/sane/sanei/sanei_lm983x.c',
         'third_party/sane/sanei/sanei_constrain_value.c','third_party/sane/sanei/sanei_config.c',
         'src/core/sane_windows.c','src/core/lide25_core.c','src/transport/usbscan_transport.c']
objects=[]
for source in sources:
    obj=out/(pathlib.Path(source).stem+'.o')
    if not args.wia_only:
        run([gcc,*flags,'-Wno-pointer-to-int-cast','-Wno-int-to-pointer-cast','-c',str(root/source),'-o',str(obj)],obj.stem+'-build.log')
    elif not obj.exists():raise SystemExit('Run the full build once before --wia-only')
    objects.append(str(obj))
if (root/'src/wia/lide25_wia.cpp').exists():
    resource=out/'wia-version.o'
    windres=shutil.which('windres.exe')
    if not windres:raise SystemExit('MinGW windres.exe is required')
    run([windres,str(root/'src/wia/version.rc'),'-o',str(resource)],'wia-version-build.log')
    run([gxx,*flags,'-I'+str(root/'third_party/wia-reference'),'-shared','-static',str(root/'src/wia/lide25_wia.cpp'),str(resource),*objects,'-lsetupapi','-lws2_32','-lwiaservc','-lwiaguid','-lole32','-loleaut32','-luuid','-lm','-o',str(out/'Lide25Wia.dll')],'wia-build.log')
    run([gxx,*flags,'-I'+str(root/'third_party/wia-reference'),str(root/'tools/wia_selftest.cpp'),'-static-libgcc','-static-libstdc++','-municode','-luuid','-o',str(out/'lide25-wia-selftest.exe')],'wia-selftest-build.log')
    run([gxx,*flags,'-I'+str(root/'third_party/wia-reference'),str(root/'tools/wia_client.cpp'),'-static','-municode','-lole32','-loleaut32','-lshlwapi','-luuid','-o',str(out/'lide25-wia-client.exe')],'wia-client-build.log')
if not args.wia_only and (root/'tools/core_diag.c').exists():
    run([gcc,*flags,str(root/'tools/core_diag.c'),*objects,'-lsetupapi','-lws2_32','-lm','-o',str(out/'lide25-core-diag.exe')],'core-diag-build.log')
(out/'toolchain.json').write_text(json.dumps({'gcc':gcc,'gxx':gxx,'flags':flags},indent=2))
