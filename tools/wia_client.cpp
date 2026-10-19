/* Integration test of the actual Windows WIA service, not direct driver calls. */
#define UNICODE
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#include <windows.h>
#include <initguid.h>
#include "wia_lh.h"
#include <shlwapi.h>
#include <cstdio>
#include <cwchar>
extern "C" const IID IID_IWiaTransfer={0xc39d6942,0x2f4e,0x4d04,{0x92,0xfe,0x4e,0xf4,0xd3,0xa1,0xde,0x5a}};
class Callback final:public IWiaTransferCallback {
    LONG refs=1;const wchar_t *path;bool cancel;
public:
    Callback(const wchar_t *p,bool c):path(p),cancel(c){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **p) override {if(!p)return E_POINTER;*p=nullptr;if(id!=IID_IUnknown&&id!=IID_IWiaTransferCallback)return E_NOINTERFACE;*p=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override{ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE TransferCallback(LONG,WiaTransferParams *p) override{printf("message=%ld percent=%ld bytes=%llu\n",p->lMessage,p->lPercentComplete,(unsigned long long)p->ulTransferredBytes);fflush(stdout);return cancel?S_FALSE:S_OK;}
    HRESULT STDMETHODCALLTYPE GetNextStream(LONG,BSTR,BSTR,IStream **s) override{return SHCreateStreamOnFileEx(path,STGM_CREATE|STGM_WRITE|STGM_SHARE_EXCLUSIVE,FILE_ATTRIBUTE_NORMAL,TRUE,nullptr,s);}
};
static HRESULT set(IWiaPropertyStorage *p,PROPID id,LONG value){PROPSPEC s={};s.ulKind=PRSPEC_PROPID;s.propid=id;PROPVARIANT v={};v.vt=VT_I4;v.lVal=value;HRESULT hr=p->WriteMultiple(1,&s,&v,WIA_IPA_FIRST);printf("set %lu=%ld hr=%08lx\n",id,value,(unsigned long)hr);return hr;}
int wmain(int argc,wchar_t **argv){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);IWiaDevMgr2 *mgr=nullptr;HRESULT hr=CoCreateInstance(CLSID_WiaDevMgr2,nullptr,CLSCTX_LOCAL_SERVER,IID_IWiaDevMgr2,(void**)&mgr);printf("manager=%08lx\n",(unsigned long)hr);if(FAILED(hr))return 2;
    IEnumWIA_DEV_INFO *e=nullptr;hr=mgr->EnumDeviceInfo(WIA_DEVINFO_ENUM_LOCAL,&e);if(FAILED(hr))return 3;
    IWiaPropertyStorage *p=nullptr;IWiaItem2 *root=nullptr;
    while(e->Next(1,&p,nullptr)==S_OK){PROPSPEC s={};s.ulKind=PRSPEC_PROPID;s.propid=WIA_DIP_DEV_ID;PROPVARIANT v={};hr=p->ReadMultiple(1,&s,&v);p->Release();p=nullptr;if(SUCCEEDED(hr)&&v.vt==VT_BSTR){wprintf(L"device=%ls\n",v.bstrVal);if(!_wcsnicmp(v.bstrVal,L"{6BDD1FC6-810F-11D0-BEC7-08002BE2092F}",38)){hr=mgr->CreateDevice(0,v.bstrVal,&root);printf("connect=%08lx\n",(unsigned long)hr);PropVariantClear(&v);break;}}PropVariantClear(&v);}
    e->Release();mgr->Release();if(!root)return 4;
    IEnumWiaItem2 *items=nullptr;hr=root->EnumChildItems(nullptr,&items);printf("enum=%08lx\n",(unsigned long)hr);if(FAILED(hr))return 5;
    IWiaItem2 *item=nullptr;hr=items->Next(1,&item,nullptr);items->Release();if(hr!=S_OK)return 6;
    LONG type=0;item->GetItemType(&type);printf("item_type=%lx\n",(unsigned long)type);
    if(argc>1){hr=item->QueryInterface(IID_IWiaPropertyStorage,(void**)&p);if(FAILED(hr))return 7;LONG dpi=argc>2?_wtol(argv[2]):150,mode=argc>3?_wtol(argv[3]):3; // WIA_DATA_COLOR=3
        LONG extent=argc>4?_wtol(argv[4]):177;
        if(FAILED(set(p,WIA_IPS_XRES,dpi))||FAILED(set(p,WIA_IPA_DATATYPE,mode))||FAILED(set(p,WIA_IPS_XEXTENT,extent))||FAILED(set(p,WIA_IPS_YEXTENT,extent)))return 8;p->Release();
        IWiaTransfer *t=nullptr;hr=item->QueryInterface(IID_IWiaTransfer,(void**)&t);if(FAILED(hr))return 9;auto cb=new Callback(argv[1],argc>5);hr=t->Download(0,cb);printf("download=%08lx\n",(unsigned long)hr);cb->Release();t->Release();
    }item->Release();root->Release();CoUninitialize();return FAILED(hr)?10:0;
}
