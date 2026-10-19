// Both TWAIN bitnesses use the same out-of-process Windows WIA service.
#define UNICODE
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#include "wia_acquire.h"
#include <initguid.h>
#include "../../third_party/wia-reference/wia_lh.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <cstring>
#include <stdexcept>
extern "C" const IID IID_IWiaTransfer={0xc39d6942,0x2f4e,0x4d04,{0x92,0xfe,0x4e,0xf4,0xd3,0xa1,0xde,0x5a}};
namespace lide25 {
template<class T> struct Com {
    T *p=nullptr; ~Com(){if(p)p->Release();}
    T** out(){return &p;} T*operator->()const{return p;}
};
struct Apartment {
    HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}
};
static bool ours(const wchar_t *id){
    if(!id||_wcsnicmp(id,L"{6BDD1FC6-810F-11D0-BEC7-08002BE2092F}\\",39))return false;
    std::wstring path=L"SYSTEM\\CurrentControlSet\\Control\\Class\\";path+=id;
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,path.c_str(),0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&key)!=ERROR_SUCCESS)return false;
    wchar_t value[128]={};DWORD size=sizeof(value),type=0;
    LONG status=RegQueryValueExW(key,L"USDClass",nullptr,&type,(BYTE*)value,&size);RegCloseKey(key);
    return status==ERROR_SUCCESS&&type==REG_SZ&&!_wcsicmp(value,L"{0DDF42CB-24F2-4CEC-9FD7-2E6A25C91B10}");
}
static HRESULT open(Com<IWiaItem2>&root){
    Com<IWiaDevMgr2> mgr;
    HRESULT hr=CoCreateInstance(CLSID_WiaDevMgr2,nullptr,CLSCTX_LOCAL_SERVER,IID_IWiaDevMgr2,(void**)mgr.out());
    if(FAILED(hr))return hr;
    Com<IEnumWIA_DEV_INFO> devices;hr=mgr->EnumDeviceInfo(WIA_DEVINFO_ENUM_LOCAL,devices.out());if(FAILED(hr))return hr;
    IWiaPropertyStorage *props=nullptr;
    while(devices->Next(1,&props,nullptr)==S_OK){
        PROPSPEC spec={};spec.ulKind=PRSPEC_PROPID;spec.propid=WIA_DIP_DEV_ID;
        PROPVARIANT value={};hr=props->ReadMultiple(1,&spec,&value);props->Release();
        if(SUCCEEDED(hr)&&value.vt==VT_BSTR&&ours(value.bstrVal)){
            hr=mgr->CreateDevice(0,value.bstrVal,root.out());PropVariantClear(&value);return hr;
        }
        PropVariantClear(&value);
    }
    return WIA_ERROR_OFFLINE;
}
bool available(){Apartment apartment;Com<IWiaItem2> root;return SUCCEEDED(open(root));}
static HRESULT write(IWiaPropertyStorage *props,PROPID id,LONG n){
    PROPSPEC spec={};spec.ulKind=PRSPEC_PROPID;spec.propid=id;
    PROPVARIANT value={};value.vt=VT_I4;value.lVal=n;
    return props->WriteMultiple(1,&spec,&value,WIA_IPA_FIRST);
}
class Receiver final:public IWiaTransferCallback {
    LONG refs=1;Progress progress;void *context;
public:
    IStream *stream=nullptr;
    Receiver(Progress p,void*c):progress(p),context(c){}
    ~Receiver(){if(stream)stream->Release();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(id!=IID_IUnknown&&id!=IID_IWiaTransferCallback)return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release()override{ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE TransferCallback(LONG,WiaTransferParams *p)override{
        return progress&&!progress(context,p->lPercentComplete)?S_FALSE:S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNextStream(LONG,BSTR,BSTR,IStream **out)override{
        if(!out)return E_POINTER;*out=nullptr;if(stream)return E_UNEXPECTED;
        HRESULT hr=CreateStreamOnHGlobal(nullptr,TRUE,&stream);
        if(SUCCEEDED(hr)){stream->AddRef();*out=stream;}return hr;
    }
};
HRESULT acquire(const Settings&s,Image&image,Progress progress,void *context)try{
    image.clear();Apartment apartment;
    if(FAILED(apartment.hr)&&apartment.hr!=RPC_E_CHANGED_MODE)return apartment.hr;
    Com<IWiaItem2> root;HRESULT hr=open(root);if(FAILED(hr))return hr;
    Com<IEnumWiaItem2> children;hr=root->EnumChildItems(nullptr,children.out());if(FAILED(hr))return hr;
    Com<IWiaItem2> flatbed;hr=children->Next(1,flatbed.out(),nullptr);if(hr!=S_OK)return FAILED(hr)?hr:E_FAIL;
    Com<IWiaPropertyStorage> props;hr=flatbed->QueryInterface(IID_IWiaPropertyStorage,(void**)props.out());if(FAILED(hr))return hr;
    auto area=geometry(s);LONG x=area.x,y=area.y,w=area.width,h=area.height;
    if(w<1||h<1||x<0||y<0)return E_INVALIDARG;
    const std::pair<PROPID,LONG> values[]={
        {WIA_IPS_XRES,s.dpi},{WIA_IPA_DATATYPE,s.pixel==0?WIA_DATA_THRESHOLD:s.pixel==1?WIA_DATA_GRAYSCALE:WIA_DATA_COLOR},
        {WIA_IPS_XPOS,0},{WIA_IPS_YPOS,0},{WIA_IPS_XEXTENT,w},{WIA_IPS_YEXTENT,h},
        {WIA_IPS_XPOS,x},{WIA_IPS_YPOS,y},{WIA_IPS_BRIGHTNESS,s.brightness},{WIA_IPS_CONTRAST,s.contrast}};
    for(auto v:values){hr=write(props.p,v.first,v.second);if(FAILED(hr))return hr;}
    Com<IWiaTransfer> transfer;hr=flatbed->QueryInterface(IID_IWiaTransfer,(void**)transfer.out());if(FAILED(hr))return hr;
    auto receiver=new Receiver(progress,context);
    hr=transfer->Download(0,receiver);
    if(hr!=S_OK||!receiver->stream){receiver->Release();return hr==S_OK?E_FAIL:hr;}
    HGLOBAL global=nullptr;hr=GetHGlobalFromStream(receiver->stream,&global);
    STATSTG stat={};if(SUCCEEDED(hr))hr=receiver->stream->Stat(&stat,STATFLAG_NONAME);
    if(FAILED(hr)||stat.cbSize.QuadPart<54||stat.cbSize.QuadPart>512ull*1024*1024){receiver->Release();return E_OUTOFMEMORY;}
    const BYTE *file=(const BYTE*)GlobalLock(global);
    if(!file){receiver->Release();return E_OUTOFMEMORY;}
    BITMAPFILEHEADER f;BITMAPINFOHEADER b;std::memcpy(&f,file,sizeof(f));std::memcpy(&b,file+14,sizeof(b));
    bool valid=f.bfType==0x4d42&&b.biSize>=40&&b.biWidth>0&&b.biHeight!=0&&b.biHeight!=LONG_MIN&&b.biPlanes==1&&b.biCompression==BI_RGB&&(b.biBitCount==1||b.biBitCount==8||b.biBitCount==24);
    uint64_t stride=valid?(((uint64_t)b.biWidth*b.biBitCount+31)/32)*4:0;
    uint64_t rows=valid?(uint64_t)std::abs(b.biHeight):0;
    valid=valid&&f.bfOffBits>=54&&f.bfOffBits>=14+b.biSize&&f.bfOffBits<=stat.cbSize.QuadPart&&stride*rows<=stat.cbSize.QuadPart-f.bfOffBits;
    if(valid){
        try{image.dib.assign(file+14,file+(size_t)stat.cbSize.QuadPart);}catch(...){GlobalUnlock(global);receiver->Release();throw;}
        image.pixels_offset=f.bfOffBits-14;image.width=b.biWidth;image.height=(int)rows;image.depth=b.biBitCount;image.stride=(int)stride;image.bottom_up=b.biHeight>0;
    }
    GlobalUnlock(global);receiver->Release();return valid?S_OK:E_FAIL;
}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
}
