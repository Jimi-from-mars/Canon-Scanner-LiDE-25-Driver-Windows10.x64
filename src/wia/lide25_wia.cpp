/* LiDE 25 native WIA 2.0 minidriver. Windows 10 x64 only. */
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <initguid.h>
#include <sti.h>
#include "stiusd.h"
#include "wiamindr_lh.h"
#include <vector>
#include <string>
#include <new>
#include <algorithm>
#include <cstdio>
#include "../core/lide25_core.h"
#include "../transport/usbscan_transport.h"

static const CLSID driver_clsid={0x0ddf42cb,0x24f2,0x4cec,{0x9f,0xd7,0x2e,0x6a,0x25,0xc9,0x1b,0x10}};
static const IID mini_iid={0xd8cdee14,0x3c6c,0x11d2,{0x9a,0x35,0,0xc0,0x4f,0xa3,0x61,0x45}};
static const IID usd_iid={0x0c9bb460,0x51ac,0x11d0,{0x90,0xea,0,0xaa,0,0x60,0xf8,0x6c}};
static LONG objects=0,locks=0;
static HMODULE module_handle=nullptr;
static void trace(const char *message,HRESULT result=S_OK) {
    char s[512];snprintf(s,sizeof(s),"Lide25Wia: %s hr=0x%08lx\n",message,(unsigned long)result);OutputDebugStringA(s);
    wchar_t path[MAX_PATH];if(!GetModuleFileNameW(module_handle,path,MAX_PATH))return;
    wchar_t *slash=wcsrchr(path,L'\\');if(!slash)return;wcscpy(slash+1,L"Lide25Wia.log");
    HANDLE f=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f!=INVALID_HANDLE_VALUE){DWORD n;WriteFile(f,s,(DWORD)strlen(s),&n,nullptr);CloseHandle(f);}
}
static HRESULT status_to_hr(int s) {switch(s){case 0:return S_OK;case 2:return S_FALSE;case 3:return WIA_ERROR_BUSY;case 4:return E_INVALIDARG;case 10:return E_OUTOFMEMORY;case 11:return E_ACCESSDENIED;default:return WIA_ERROR_DEVICE_COMMUNICATION;}}

class Properties {
    std::vector<PROPSPEC> specs;std::vector<PROPVARIANT> values;std::vector<WIA_PROPERTY_INFO> info;std::vector<LPOLESTR> names;std::vector<PROPID> ids;
    std::vector<std::vector<LONG>> lists;
public:
    Properties(){specs.reserve(64);values.reserve(64);info.reserve(64);}
    ~Properties(){for(auto &v:values)PropVariantClear(&v);}
    void add(PROPID id,const wchar_t *name,LONG value,ULONG flags=WIA_PROP_READ|WIA_PROP_NONE){
        PROPSPEC ps={};ps.ulKind=PRSPEC_PROPID;ps.propid=id;specs.push_back(ps);ids.push_back(id);names.push_back(const_cast<LPWSTR>(name));
        PROPVARIANT pv={};pv.vt=VT_I4;pv.lVal=value;values.push_back(pv);WIA_PROPERTY_INFO pi={};pi.vt=VT_I4;pi.lAccessFlags=flags;info.push_back(pi);
    }
    void guid(PROPID id,const wchar_t *name,const GUID &value,ULONG flags=WIA_PROP_READ|WIA_PROP_NONE){add(id,name,0,flags);values.back().vt=VT_CLSID;values.back().puuid=(GUID*)CoTaskMemAlloc(sizeof(GUID));if(values.back().puuid)*values.back().puuid=value;info.back().vt=VT_CLSID;}
    void str(PROPID id,const wchar_t *name,const wchar_t *value){add(id,name,0);values.back().vt=VT_BSTR;values.back().bstrVal=SysAllocString(value);info.back().vt=VT_BSTR;}
    void range(PROPID id,const wchar_t *name,LONG value,LONG lo,LONG hi){add(id,name,value,WIA_PROP_READ|WIA_PROP_WRITE|WIA_PROP_RANGE|WIA_PROP_CACHEABLE);info.back().ValidVal.Range={lo,value,hi,1};}
    void list(PROPID id,const wchar_t *name,LONG value,std::initializer_list<LONG> valid){add(id,name,value,WIA_PROP_READ|WIA_PROP_WRITE|WIA_PROP_LIST|WIA_PROP_CACHEABLE);lists.emplace_back(valid);info.back().ValidVal.List={(LONG)valid.size(),value,(BYTE*)lists.back().data()};}
    void guidlist(PROPID id,const wchar_t *name,const GUID &value){guid(id,name,value,WIA_PROP_READ|WIA_PROP_WRITE|WIA_PROP_LIST);info.back().ValidVal.ListGuid={1,value,values.back().puuid};}
    void flag(PROPID id,const wchar_t *name,LONG value,LONG bits){add(id,name,value,WIA_PROP_READ|WIA_PROP_WRITE|WIA_PROP_FLAG);info.back().ValidVal.Flag={value,bits};}
    HRESULT upload(BYTE *ctx){for(auto &v:values)if((v.vt==VT_CLSID&&!v.puuid)||(v.vt==VT_BSTR&&!v.bstrVal))return E_OUTOFMEMORY;HRESULT hr=wiasSetItemPropNames(ctx,(LONG)ids.size(),ids.data(),names.data());if(SUCCEEDED(hr))hr=wiasWriteMultiple(ctx,(ULONG)specs.size(),specs.data(),values.data());if(SUCCEEDED(hr))hr=wiasSetItemPropAttribs(ctx,(LONG)specs.size(),specs.data(),info.data());return hr;}
};

class Driver final:public IStiUSD,public IWiaMiniDrv {
    class Inner final:public IUnknown {
        Driver *owner;
    public:
        explicit Inner(Driver *d):owner(d){}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **p) override{return owner->inner_query(id,p);}
        ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&owner->refs);}
        ULONG STDMETHODCALLTYPE Release() override{ULONG n=InterlockedDecrement(&owner->refs);if(!n)delete owner;return n;}
    } inner{this};
    IUnknown *outer=nullptr;
    LONG refs=1;IWiaDrvItem *root=nullptr;std::wstring root_name;HANDLE device_lock=nullptr;
    WIA_FORMAT_INFO bmp={};
    static LONG value(BYTE *ctx,PROPID id,LONG fallback){LONG n=fallback;wiasReadPropLong(ctx,id,&n,nullptr,FALSE);return n;}
    HRESULT sync(BYTE *ctx,ULONG count=0,const PROPSPEC *changed=nullptr){
        LONG flags=0;HRESULT hr=wiasGetItemType(ctx,&flags);if(FAILED(hr)||flags&WiaItemTypeRoot)return hr;
        LONG x=value(ctx,WIA_IPS_XRES,300),y=value(ctx,WIA_IPS_YRES,x);bool cx=false,cy=false;
        for(ULONG i=0;i<count;i++)if(changed[i].ulKind==PRSPEC_PROPID){cx|=changed[i].propid==WIA_IPS_XRES;cy|=changed[i].propid==WIA_IPS_YRES;}
        if(cx&&!cy){y=x;wiasWritePropLong(ctx,WIA_IPS_YRES,y);}else if(cy&&!cx){x=y;wiasWritePropLong(ctx,WIA_IPS_XRES,x);}
        if(x!=y||(x!=75&&x!=150&&x!=300&&x!=600&&x!=1200))return E_INVALIDARG;
        // The pinned backend exposes 215 whole millimetres horizontally.
        LONG mw=8464*x/1000,mh=11690*y/1000;
        LONG xp=value(ctx,WIA_IPS_XPOS,0),yp=value(ctx,WIA_IPS_YPOS,0);
        LONG w=value(ctx,WIA_IPS_XEXTENT,mw),h=value(ctx,WIA_IPS_YEXTENT,mh);
        if(cx||cy){xp=std::min(xp,mw-1);yp=std::min(yp,mh-1);w=std::min(w,mw-xp);h=std::min(h,mh-yp);}
        if(xp<0||yp<0||w<1||h<1||xp+w>mw||yp+h>mh)return E_INVALIDARG;
        for(ULONG i=0;i<count;i++)if(changed[i].ulKind==PRSPEC_PROPID&&changed[i].propid==WIA_IPS_CUR_INTENT){LONG intent=value(ctx,WIA_IPS_CUR_INTENT,WIA_INTENT_IMAGE_TYPE_COLOR);wiasWritePropLong(ctx,WIA_IPA_DATATYPE,(intent&WIA_INTENT_IMAGE_TYPE_TEXT)?WIA_DATA_THRESHOLD:(intent&WIA_INTENT_IMAGE_TYPE_GRAYSCALE)?WIA_DATA_GRAYSCALE:WIA_DATA_COLOR);}
        LONG type=value(ctx,WIA_IPA_DATATYPE,WIA_DATA_COLOR),depth=type==WIA_DATA_COLOR?24:type==WIA_DATA_GRAYSCALE?8:type==WIA_DATA_THRESHOLD?1:0;
        if(!depth)return E_INVALIDARG;
        wiasWritePropLong(ctx,WIA_IPA_DEPTH,depth);wiasWritePropLong(ctx,WIA_IPA_CHANNELS_PER_PIXEL,depth==24?3:1);wiasWritePropLong(ctx,WIA_IPA_BITS_PER_CHANNEL,depth==1?1:8);
        wiasSetValidListLong(ctx,WIA_IPA_DEPTH,1,depth,&depth);
        wiasWritePropLong(ctx,WIA_IPA_PIXELS_PER_LINE,w);wiasWritePropLong(ctx,WIA_IPA_NUMBER_OF_LINES,h);
        wiasWritePropLong(ctx,WIA_IPA_BYTES_PER_LINE,((w*depth+31)/32)*4);
        wiasWritePropLong(ctx,WIA_IPA_ITEM_SIZE,((w*depth+31)/32)*4*h);
        wiasWritePropLong(ctx,WIA_IPS_XPOS,xp);wiasWritePropLong(ctx,WIA_IPS_YPOS,yp);wiasWritePropLong(ctx,WIA_IPS_XEXTENT,w);wiasWritePropLong(ctx,WIA_IPS_YEXTENT,h);
        wiasSetValidRangeLong(ctx,WIA_IPS_XPOS,0,xp,mw-1,1);wiasSetValidRangeLong(ctx,WIA_IPS_YPOS,0,yp,mh-1,1);
        wiasSetValidRangeLong(ctx,WIA_IPS_XEXTENT,1,w,mw-xp,1);wiasSetValidRangeLong(ctx,WIA_IPS_YEXTENT,1,h,mh-yp,1);
        return S_OK;
    }
public:
    explicit Driver(IUnknown *controller=nullptr):outer(controller){InterlockedIncrement(&objects);device_lock=CreateMutexW(nullptr,FALSE,L"Global\\Lide25WiaSession_04A9_2220");bmp.guidFormatID=WiaImgFmt_BMP;bmp.lTymed=TYMED_FILE;trace("construct");}
    ~Driver(){if(root){root->UnlinkItemTree(WiaItemTypeDisconnected);root->Release();}if(device_lock)CloseHandle(device_lock);InterlockedDecrement(&objects);trace("destruct");}
    HRESULT inner_query(REFIID id,void **out){if(!out)return E_POINTER;*out=nullptr;if(id==IID_IUnknown)*out=static_cast<IUnknown*>(&inner);else if(id==usd_iid)*out=static_cast<IStiUSD*>(this);else if(id==mini_iid)*out=static_cast<IWiaMiniDrv*>(this);else return E_NOINTERFACE;((IUnknown*)*out)->AddRef();return S_OK;}
    ULONG inner_release(){return inner.Release();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **out) override {return outer?outer->QueryInterface(id,out):inner.QueryInterface(id,out);}
    ULONG STDMETHODCALLTYPE AddRef() override{return outer?outer->AddRef():inner.AddRef();}
    ULONG STDMETHODCALLTYPE Release() override{return outer?outer->Release():inner.Release();}
    HRESULT STDMETHODCALLTYPE Initialize(PSTIDEVICECONTROL,DWORD,HKEY) override{trace("Initialize");return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCapabilities(PSTI_USD_CAPS c) override{if(!c)return E_POINTER;memset(c,0,sizeof(*c));c->dwVersion=STI_VERSION_3;c->dwGenericCaps=STI_GENCAP_WIA;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetStatus(PSTI_DEVICE_STATUS s) override{if(!s)return E_POINTER;L25Usb u;bool present=l25_usb_open(&u,nullptr)!=0;if(present)l25_usb_close(&u);s->dwOnlineState=present?STI_ONLINESTATE_OPERATIONAL:STI_ONLINESTATE_OFFLINE;s->dwHardwareStatusCode=0;s->dwEventHandlingState=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE DeviceReset() override{l25_core_cancel();return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Diagnostic(LPSTI_DIAG d) override{if(!d)return E_POINTER;L25Usb u;if(!l25_usb_open(&u,nullptr))return HRESULT_FROM_WIN32(u.last_error);l25_usb_close(&u);d->dwBasicDiagCode=STI_DIAGCODE_HWPRESENCE;return S_OK;}
    HRESULT STDMETHODCALLTYPE Escape(STI_RAW_CONTROL_CODE,LPVOID,DWORD,LPVOID,DWORD,LPDWORD n) override{if(n)*n=0;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetLastError(LPDWORD e) override{if(!e)return E_POINTER;*e=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE LockDevice() override{DWORD r=WaitForSingleObject(device_lock,10000);return r==WAIT_OBJECT_0||r==WAIT_ABANDONED?S_OK:WIA_ERROR_BUSY;}
    HRESULT STDMETHODCALLTYPE UnLockDevice() override{return ReleaseMutex(device_lock)?S_OK:HRESULT_FROM_WIN32(::GetLastError());}
    HRESULT STDMETHODCALLTYPE RawReadData(LPVOID,LPDWORD,LPOVERLAPPED) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RawWriteData(LPVOID,DWORD,LPOVERLAPPED) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RawReadCommand(LPVOID,LPDWORD,LPOVERLAPPED) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RawWriteCommand(LPVOID,DWORD,LPOVERLAPPED) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetNotificationHandle(HANDLE) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetNotificationData(LPSTINOTIFY) override{return STIERR_NOEVENTS;}
    HRESULT STDMETHODCALLTYPE GetLastErrorInfo(STI_ERROR_INFO *e) override{if(!e)return E_POINTER;e->dwGenericError=e->dwVendorError=0;e->szExtendedErrorText[0]=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE drvInitializeWia(BYTE*,LONG,BSTR,BSTR name,IUnknown*,IUnknown*,IWiaDrvItem **out,IUnknown **inner,LONG *error) override try {
        trace("drvInitializeWia");if(!out||!inner||!error||!name)return E_POINTER;*error=0;*out=nullptr;*inner=nullptr;
        HRESULT hr=S_OK;if(!root){root_name=name;BSTR n=SysAllocString(L"Root");hr=wiasCreateDrvItem(WiaItemTypeRoot|WiaItemTypeDevice|WiaItemTypeFolder,n,name,this,0,nullptr,&root);SysFreeString(n);
            if(SUCCEEDED(hr)){IWiaDrvItem *child=nullptr;std::wstring full=root_name+L"\\Flatbed";BSTR cn=SysAllocString(L"Flatbed"),fn=SysAllocString(full.c_str());hr=wiasCreateDrvItem(WiaItemTypeImage|WiaItemTypeFile|WiaItemTypeTransfer|WiaItemTypeProgrammableDataSource,cn,fn,this,0,nullptr,&child);SysFreeString(cn);SysFreeString(fn);if(SUCCEEDED(hr)){hr=child->AddItemToFolder(root);child->Release();}}}
        if(SUCCEEDED(hr)){root->AddRef();*out=root;}trace("drvInitializeWia result",hr);return hr;
    } catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_UNEXPECTED;}
    HRESULT STDMETHODCALLTYPE drvInitItemProperties(BYTE *ctx,LONG,LONG *error) override try {
        if(!ctx||!error)return E_POINTER;*error=0;LONG flags=0;HRESULT hr=wiasGetItemType(ctx,&flags);if(FAILED(hr))return hr;Properties p;
        if(flags&WiaItemTypeRoot){p.guid(WIA_IPA_ITEM_CATEGORY,WIA_IPA_ITEM_CATEGORY_STR,WIA_CATEGORY_ROOT);p.add(WIA_DPS_DOCUMENT_HANDLING_CAPABILITIES,WIA_DPS_DOCUMENT_HANDLING_CAPABILITIES_STR,FLAT);p.add(WIA_DPS_DOCUMENT_HANDLING_STATUS,WIA_DPS_DOCUMENT_HANDLING_STATUS_STR,FLAT_READY);}
        else{
            p.guid(WIA_IPA_ITEM_CATEGORY,WIA_IPA_ITEM_CATEGORY_STR,WIA_CATEGORY_FLATBED);p.add(WIA_IPA_ACCESS_RIGHTS,WIA_IPA_ACCESS_RIGHTS_STR,WIA_ITEM_READ);
            p.add(WIA_IPS_OPTICAL_XRES,WIA_IPS_OPTICAL_XRES_STR,1200);p.add(WIA_IPS_OPTICAL_YRES,WIA_IPS_OPTICAL_YRES_STR,1200);
            p.add(WIA_IPS_MAX_HORIZONTAL_SIZE,WIA_IPS_MAX_HORIZONTAL_SIZE_STR,8464);p.add(WIA_IPS_MAX_VERTICAL_SIZE,WIA_IPS_MAX_VERTICAL_SIZE_STR,11690);
            p.add(WIA_IPS_MIN_HORIZONTAL_SIZE,WIA_IPS_MIN_HORIZONTAL_SIZE_STR,10);p.add(WIA_IPS_MIN_VERTICAL_SIZE,WIA_IPS_MIN_VERTICAL_SIZE_STR,10);
            p.list(WIA_IPS_XRES,WIA_IPS_XRES_STR,300,{75,150,300,600,1200});p.list(WIA_IPS_YRES,WIA_IPS_YRES_STR,300,{75,150,300,600,1200});
            p.range(WIA_IPS_XPOS,WIA_IPS_XPOS_STR,0,0,2549);p.range(WIA_IPS_YPOS,WIA_IPS_YPOS_STR,0,0,3506);
            p.range(WIA_IPS_XEXTENT,WIA_IPS_XEXTENT_STR,2480,1,2550);p.range(WIA_IPS_YEXTENT,WIA_IPS_YEXTENT_STR,3507,1,3507);
            p.list(WIA_IPA_DATATYPE,WIA_IPA_DATATYPE_STR,WIA_DATA_COLOR,{WIA_DATA_THRESHOLD,WIA_DATA_GRAYSCALE,WIA_DATA_COLOR});
            p.list(WIA_IPA_DEPTH,WIA_IPA_DEPTH_STR,24,{24});p.add(WIA_IPA_CHANNELS_PER_PIXEL,WIA_IPA_CHANNELS_PER_PIXEL_STR,3);p.add(WIA_IPA_BITS_PER_CHANNEL,WIA_IPA_BITS_PER_CHANNEL_STR,8);
            p.add(WIA_IPA_PIXELS_PER_LINE,WIA_IPA_PIXELS_PER_LINE_STR,2480);p.add(WIA_IPA_NUMBER_OF_LINES,WIA_IPA_NUMBER_OF_LINES_STR,3507);p.add(WIA_IPA_BYTES_PER_LINE,WIA_IPA_BYTES_PER_LINE_STR,7440);
            p.add(WIA_IPA_ITEM_SIZE,WIA_IPA_ITEM_SIZE_STR,7440*3507);p.add(WIA_IPA_PLANAR,WIA_IPA_PLANAR_STR,WIA_PACKED_PIXEL);
            p.guid(WIA_IPA_PREFERRED_FORMAT,WIA_IPA_PREFERRED_FORMAT_STR,WiaImgFmt_BMP);p.guidlist(WIA_IPA_FORMAT,WIA_IPA_FORMAT_STR,WiaImgFmt_BMP);p.str(WIA_IPA_FILENAME_EXTENSION,WIA_IPA_FILENAME_EXTENSION_STR,L"BMP");
            p.list(WIA_IPA_COMPRESSION,WIA_IPA_COMPRESSION_STR,WIA_COMPRESSION_NONE,{WIA_COMPRESSION_NONE});p.list(WIA_IPA_TYMED,WIA_IPA_TYMED_STR,TYMED_FILE,{TYMED_FILE});p.add(WIA_IPA_BUFFER_SIZE,WIA_IPA_BUFFER_SIZE_STR,65536);
            p.range(WIA_IPS_BRIGHTNESS,WIA_IPS_BRIGHTNESS_STR,0,-1000,1000);p.range(WIA_IPS_CONTRAST,WIA_IPS_CONTRAST_STR,0,-1000,1000);
            p.range(WIA_IPS_PREVIEW,WIA_IPS_PREVIEW_STR,WIA_FINAL_SCAN,WIA_FINAL_SCAN,WIA_PREVIEW_SCAN);p.add(WIA_IPS_SHOW_PREVIEW_CONTROL,WIA_IPS_SHOW_PREVIEW_CONTROL_STR,WIA_SHOW_PREVIEW_CONTROL);
            p.flag(WIA_IPS_CUR_INTENT,WIA_IPS_CUR_INTENT_STR,WIA_INTENT_IMAGE_TYPE_COLOR,WIA_INTENT_IMAGE_TYPE_COLOR|WIA_INTENT_IMAGE_TYPE_GRAYSCALE|WIA_INTENT_IMAGE_TYPE_TEXT|WIA_INTENT_MINIMIZE_SIZE|WIA_INTENT_MAXIMIZE_QUALITY);
            p.list(WIA_IPS_ROTATION,WIA_IPS_ROTATION_STR,PORTRAIT,{PORTRAIT});p.add(WIA_IPS_THRESHOLD,WIA_IPS_THRESHOLD_STR,128);
            p.add(WIA_IPS_SUPPORTS_CHILD_ITEM_CREATION,WIA_IPS_SUPPORTS_CHILD_ITEM_CREATION_STR,FALSE);p.add(WIA_IPS_SEGMENTATION,WIA_IPS_SEGMENTATION_STR,WIA_DONT_USE_SEGMENTATION_FILTER);
        }
        hr=p.upload(ctx);
        trace("drvInitItemProperties",hr);return hr;
    } catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_UNEXPECTED;}
    HRESULT STDMETHODCALLTYPE drvValidateItemProperties(BYTE *ctx,LONG,ULONG n,const PROPSPEC *ps,LONG *error) override{if(error)*error=0;HRESULT hr=sync(ctx,n,ps);if(SUCCEEDED(hr))hr=wiasValidateItemProperties(ctx,n,ps);trace("validate",hr);return hr;}
    HRESULT STDMETHODCALLTYPE drvWriteItemProperties(BYTE *ctx,LONG,PMINIDRV_TRANSFER_CONTEXT,LONG *error) override{if(error)*error=0;HRESULT hr=sync(ctx);trace("write properties",hr);return hr;}
    HRESULT STDMETHODCALLTYPE drvReadItemProperties(BYTE*,LONG,ULONG,const PROPSPEC*,LONG *error) override{if(error)*error=0;trace("read properties");return S_OK;}
    HRESULT STDMETHODCALLTYPE drvLockWiaDevice(BYTE*,LONG,LONG *error) override{if(error)*error=0;return LockDevice();}
    HRESULT STDMETHODCALLTYPE drvUnLockWiaDevice(BYTE*,LONG,LONG *error) override{if(error)*error=0;return UnLockDevice();}
    HRESULT STDMETHODCALLTYPE drvAnalyzeItem(BYTE*,LONG,LONG*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE drvGetDeviceErrorStr(LONG,LONG,LPOLESTR *s,LONG *e) override{if(s)*s=nullptr;if(e)*e=0;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE drvDeviceCommand(BYTE*,LONG,const GUID*,IWiaDrvItem **out,LONG *e) override{if(out)*out=nullptr;if(e)*e=0;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE drvGetCapabilities(BYTE*,LONG,LONG *n,WIA_DEV_CAP_DRV **caps,LONG *e) override{if(!n||!caps||!e)return E_POINTER;*n=0;*caps=nullptr;*e=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE drvDeleteItem(BYTE*,LONG,LONG *e) override{if(e)*e=0;return E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE drvFreeDrvItemContext(LONG,BYTE*,LONG *e) override{if(e)*e=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE drvGetWiaFormatInfo(BYTE*,LONG,LONG *n,WIA_FORMAT_INFO **formats,LONG *e) override{trace("formats");if(!n||!formats||!e)return E_POINTER;*n=1;*formats=&bmp;*e=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE drvNotifyPnpEvent(const GUID*,BSTR,ULONG) override{l25_core_cancel();return S_OK;}
    HRESULT STDMETHODCALLTYPE drvUnInitializeWia(BYTE*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE drvAcquireItemData(BYTE *ctx,LONG flags,PMINIDRV_TRANSFER_CONTEXT transfer,LONG *error) override;
};

struct Transfer {
    IWiaMiniDrvTransferCallback *callback=nullptr;IStream *stream=nullptr;L25ImageInfo image={};std::vector<BYTE> pending;size_t rows=0;ULONG64 bytes=0;HRESULT result=S_OK;
    int requested_width=0,requested_height=0;
    int send(LONG message,LONG percent){WiaTransferParams p={};p.lMessage=message;p.lPercentComplete=percent;p.ulTransferredBytes=bytes;result=callback->SendMessage(0,&p);return result!=S_OK;}
    int put(const void *p,ULONG size){ULONG n=0;result=stream->Write(p,size,&n);if(SUCCEEDED(result)&&n!=size)result=STG_E_WRITEFAULT;return FAILED(result);}
    static int begin(void *arg,const L25ImageInfo *info) try {auto &t=*(Transfer*)arg;t.image=*info;
        // SANE rounds a 1-bit raster up to eight pixels; BMP carries the exact
        // requested width while the unused trailing bits remain scanline padding.
        if(info->height!=t.requested_height||info->width<t.requested_width||
           (info->width!=t.requested_width&&!(info->depth==1&&info->channels==1&&info->width==(t.requested_width+7)/8*8))){t.result=WIA_ERROR_DEVICE_COMMUNICATION;return 1;}
        t.image.width=t.requested_width;info=&t.image;
        BITMAPFILEHEADER fh={};BITMAPINFOHEADER ih={};int bits=info->channels*info->depth;DWORD palette=bits<=8?(1u<<bits)*4:0;DWORD stride=((info->width*bits+31)/32)*4;
        fh.bfType=0x4d42;fh.bfOffBits=sizeof(fh)+sizeof(ih)+palette;fh.bfSize=fh.bfOffBits+stride*info->height;
        ih.biSize=sizeof(ih);ih.biWidth=info->width;ih.biHeight=-info->height;ih.biPlanes=1;ih.biBitCount=(WORD)bits;ih.biSizeImage=stride*info->height;ih.biXPelsPerMeter=ih.biYPelsPerMeter=(LONG)(info->dpi/0.0254+0.5);ih.biClrUsed=bits<=8?1u<<bits:0;
        if(t.put(&fh,sizeof(fh))||t.put(&ih,sizeof(ih)))return 1;
        if(palette){std::vector<RGBQUAD> colors(1u<<bits);for(unsigned i=0;i<colors.size();i++){BYTE n=(BYTE)(i*255/(colors.size()-1));colors[i]={n,n,n,0};}if(t.put(colors.data(),palette))return 1;}
        t.pending.reserve(info->bytes_per_line*2);return 0;
    }catch(...){((Transfer*)arg)->result=E_OUTOFMEMORY;return 1;}
    static int data(void *arg,const uint8_t *p,size_t n) try {auto &t=*(Transfer*)arg;t.pending.insert(t.pending.end(),p,p+n);size_t used=0;int bpl=t.image.bytes_per_line;DWORD stride=((t.image.width*t.image.channels*t.image.depth+31)/32)*4;
        while(t.pending.size()-used>=(size_t)bpl){std::vector<BYTE> row(stride,0);memcpy(row.data(),t.pending.data()+used,bpl);if(t.image.channels==3)for(int x=0;x<t.image.width;x++)std::swap(row[x*3],row[x*3+2]);if(t.image.depth==1)for(int i=0;i<bpl;i++)row[i]^=0xff;
            if(t.image.depth==1&&(t.image.width&7))row[bpl-1]&=(BYTE)(0xffu<<(8-(t.image.width&7)));
            if(t.put(row.data(),stride))return 1;used+=bpl;t.rows++;t.bytes+=stride;}
        if(used)t.pending.erase(t.pending.begin(),t.pending.begin()+used);return 0;
    }catch(...){((Transfer*)arg)->result=E_OUTOFMEMORY;return 1;}
    static int progress(void *arg,int stage,int percent){auto &t=*(Transfer*)arg;return t.send(WIA_TRANSFER_MSG_STATUS,stage?percent:0);}
    ~Transfer(){if(stream)stream->Release();if(callback)callback->Release();}
};
HRESULT Driver::drvAcquireItemData(BYTE *ctx,LONG flags,PMINIDRV_TRANSFER_CONTEXT transfer,LONG *error){
    if(!ctx||!transfer||!error)return E_POINTER;*error=0;trace("acquire");if(!(flags&WIA_MINIDRV_TRANSFER_DOWNLOAD)||transfer->guidFormatID!=WiaImgFmt_BMP)return E_NOTIMPL;
    HRESULT hr=sync(ctx);if(FAILED(hr))return hr;Transfer t;
    const IID callback_iid={0xa9d2ee89,0x2ce5,0x4ff0,{0x8a,0xdb,0xc9,0x61,0xd1,0xd7,0x74,0xca}};
    if(!transfer->pIWiaMiniDrvCallBack)return E_POINTER;
    hr=transfer->pIWiaMiniDrvCallBack->QueryInterface(callback_iid,(void**)&t.callback);if(FAILED(hr)){trace("callback query",hr);return hr;}
    BSTR name=nullptr,full=nullptr;wiasReadPropStr(ctx,WIA_IPA_ITEM_NAME,&name,nullptr,TRUE);wiasReadPropStr(ctx,WIA_IPA_FULL_ITEM_NAME,&full,nullptr,TRUE);
    hr=t.callback->GetNextStream(0,name,full,&t.stream);SysFreeString(name);SysFreeString(full);if(hr!=S_OK||!t.stream)return hr==S_OK?E_POINTER:hr;
    LONG dpi=value(ctx,WIA_IPS_XRES,300);L25ScanRequest r={};r.size=sizeof(r);r.dpi=dpi;LONG type=value(ctx,WIA_IPA_DATATYPE,WIA_DATA_COLOR);r.mode=type==WIA_DATA_COLOR?0:type==WIA_DATA_GRAYSCALE?1:2;
    t.requested_width=value(ctx,WIA_IPS_XEXTENT,2480);t.requested_height=value(ctx,WIA_IPS_YEXTENT,3507);
    r.left_mm=value(ctx,WIA_IPS_XPOS,0)*25.4/dpi;r.top_mm=value(ctx,WIA_IPS_YPOS,0)*25.4/dpi;r.width_mm=value(ctx,WIA_IPS_XEXTENT,2480)*25.4/dpi;r.height_mm=value(ctx,WIA_IPS_YEXTENT,3507)*25.4/dpi;r.brightness=value(ctx,WIA_IPS_BRIGHTNESS,0);r.contrast=value(ctx,WIA_IPS_CONTRAST,0);
    L25Sink sink={&t,Transfer::begin,Transfer::data,Transfer::progress};int status=l25_core_acquire(&r,&sink);hr=t.result!=S_OK?t.result:status_to_hr(status);
    if(hr==S_OK){if(t.pending.size()||t.rows!=(size_t)t.image.height)hr=WIA_ERROR_DEVICE_COMMUNICATION;else{t.send(WIA_TRANSFER_MSG_END_OF_STREAM,100);hr=t.result;}}
    trace("acquire result",hr);return hr;
}

class Factory final:public IClassFactory {
    LONG refs=1;
public:
    Factory(){InterlockedIncrement(&objects);}~Factory(){InterlockedDecrement(&objects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **p) override{if(!p)return E_POINTER;*p=nullptr;if(id!=IID_IUnknown&&id!=IID_IClassFactory)return E_NOINTERFACE;*p=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override{ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer,REFIID id,void **p) override{trace("CreateInstance requested");if(!p)return E_POINTER;*p=nullptr;if(outer&&id!=IID_IUnknown)return CLASS_E_NOAGGREGATION;Driver *d=new(std::nothrow)Driver(outer);if(!d)return E_OUTOFMEMORY;HRESULT hr=d->inner_query(id,p);d->inner_release();trace("CreateInstance",hr);return hr;}
    HRESULT STDMETHODCALLTYPE LockServer(BOOL b) override{if(b)InterlockedIncrement(&locks);else InterlockedDecrement(&locks);return S_OK;}
};
extern "C" __declspec(dllexport) HRESULT __stdcall DllGetClassObject(REFCLSID cls,REFIID iid,void **p){if(cls!=driver_clsid)return CLASS_E_CLASSNOTAVAILABLE;Factory *f=new(std::nothrow)Factory;if(!f)return E_OUTOFMEMORY;HRESULT hr=f->QueryInterface(iid,p);f->Release();return hr;}
extern "C" __declspec(dllexport) HRESULT __stdcall DllCanUnloadNow(){return objects==0&&locks==0?S_OK:S_FALSE;}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){module_handle=module;DisableThreadLibraryCalls(module);}return TRUE;}
