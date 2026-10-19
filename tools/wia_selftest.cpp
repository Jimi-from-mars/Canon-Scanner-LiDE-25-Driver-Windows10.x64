#define UNICODE
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#include <windows.h>
#include <sti.h>
#include "stiusd.h"
#include "wiamindr_lh.h"
#include <cstdio>
int wmain(int argc,wchar_t **argv){
    if(argc<2)return 2;HMODULE dll=LoadLibraryW(argv[1]);if(!dll){printf("LoadLibrary error=%lu\n",GetLastError());return 3;}
    auto get=(HRESULT(WINAPI*)(REFCLSID,REFIID,void**))GetProcAddress(dll,"DllGetClassObject");
    auto unload=(HRESULT(WINAPI*)())GetProcAddress(dll,"DllCanUnloadNow");if(!get||!unload)return 7;
    const CLSID cls={0x0ddf42cb,0x24f2,0x4cec,{0x9f,0xd7,0x2e,0x6a,0x25,0xc9,0x1b,0x10}};
    const IID usd_id={0x0c9bb460,0x51ac,0x11d0,{0x90,0xea,0,0xaa,0,0x60,0xf8,0x6c}};
    const IID mini_id={0xd8cdee14,0x3c6c,0x11d2,{0x9a,0x35,0,0xc0,0x4f,0xa3,0x61,0x45}};
    IClassFactory *f=nullptr;HRESULT hr=get(cls,IID_IClassFactory,(void**)&f);if(FAILED(hr)){printf("factory=%08lx\n",(unsigned long)hr);return 4;}
    IStiUSD *usd=nullptr;hr=f->CreateInstance(nullptr,usd_id,(void**)&usd);f->Release();if(FAILED(hr)){printf("create=%08lx\n",(unsigned long)hr);return 5;}
    IWiaMiniDrv *mini=nullptr;hr=usd->QueryInterface(mini_id,(void**)&mini);STI_USD_CAPS c={};HRESULT caps=usd->GetCapabilities(&c);
    if(mini)mini->Release();usd->Release();HRESULT can=unload();
    printf("{\"dll_loaded\":true,\"mini_query\":%ld,\"capabilities_result\":%ld,\"capabilities\":%lu,\"sti_version\":%lu,\"can_unload\":%ld}\n",(long)hr,(long)caps,c.dwGenericCaps,c.dwVersion,(long)can);
    bool ok=SUCCEEDED(hr)&&SUCCEEDED(caps)&&c.dwVersion==STI_VERSION_3&&can==S_OK;
    FreeLibrary(dll);return ok?0:6;
}
