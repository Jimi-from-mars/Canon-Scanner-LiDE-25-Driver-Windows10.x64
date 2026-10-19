#include "usbscan_transport.h"
#include <setupapi.h>
#include <winioctl.h>
#include <usbscan.h>
#include <wchar.h>
#include <stdio.h>
#include <limits.h>

static const GUID image_class = {0x6bdd1fc6,0x810f,0x11d0,{0xbe,0xc7,0x08,0x00,0x2b,0xe2,0x09,0x2f}};

static int find_interface_port(wchar_t *port,DWORD capacity) {
    HDEVINFO set=SetupDiGetClassDevsW(&image_class,NULL,NULL,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(set==INVALID_HANDLE_VALUE)return 0;
    int found=0;SP_DEVICE_INTERFACE_DATA iface={0};iface.cbSize=sizeof(iface);
    for(DWORD i=0;SetupDiEnumDeviceInterfaces(set,NULL,&image_class,i,&iface);++i){
        BYTE storage[4096]={0};SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail=(SP_DEVICE_INTERFACE_DETAIL_DATA_W*)storage;detail->cbSize=sizeof(*detail);
        SP_DEVINFO_DATA dev={0};dev.cbSize=sizeof(dev);
        if(!SetupDiGetDeviceInterfaceDetailW(set,&iface,detail,sizeof(storage),NULL,&dev))continue;
        wchar_t ids[2048]={0};if(!SetupDiGetDeviceRegistryPropertyW(set,&dev,SPDRP_HARDWAREID,NULL,(BYTE*)ids,sizeof(ids),NULL))continue;
        int match=0;for(wchar_t *p=ids;*p;p+=wcslen(p)+1)if(!_wcsicmp(p,L"USB\\VID_04A9&PID_2220"))match=1;
        if(match&&wcslen(detail->DevicePath)<capacity){wcscpy(port,detail->DevicePath);found=1;break;}
    }
    SetupDiDestroyDeviceInfoList(set);return found;
}

static int find_port(wchar_t *port, DWORD capacity) {
    HDEVINFO set=SetupDiGetClassDevsW(&image_class,NULL,NULL,DIGCF_PRESENT);
    if(set==INVALID_HANDLE_VALUE) return 0;
    int found=0;
    SP_DEVINFO_DATA dev={0}; dev.cbSize=sizeof(dev);
    for(DWORD i=0; SetupDiEnumDeviceInfo(set,i,&dev); ++i) {
        wchar_t ids[2048]={0}; DWORD type=0;
        if(!SetupDiGetDeviceRegistryPropertyW(set,&dev,SPDRP_HARDWAREID,&type,(BYTE*)ids,sizeof(ids),NULL)) continue;
        int matches=0;
        for(wchar_t *p=ids; *p; p+=wcslen(p)+1)
            if(_wcsicmp(p,L"USB\\VID_04A9&PID_2220")==0) matches=1;
        if(!matches) continue;
        HKEY key=SetupDiOpenDevRegKey(set,&dev,DICS_FLAG_GLOBAL,0,DIREG_DRV,KEY_QUERY_VALUE);
        if(key==INVALID_HANDLE_VALUE) continue;
        DWORD bytes=capacity*sizeof(wchar_t);
        LONG result=RegQueryValueExW(key,L"CreateFileName",NULL,&type,(BYTE*)port,&bytes);
        RegCloseKey(key);
        if(result==ERROR_SUCCESS && type==REG_SZ && port[0] && _wcsicmp(port,L"AUTO")) {found=1; break;}
    }
    SetupDiDestroyDeviceInfoList(set);
    if(!found)found=find_interface_port(port,capacity);
    if(!found) SetLastError(ERROR_NOT_FOUND);
    return found;
}

static int complete(L25Usb *usb, OVERLAPPED *op, BOOL immediate, DWORD *done, DWORD timeout) {
    DWORD error=immediate?ERROR_SUCCESS:GetLastError();
    if(!immediate && error==ERROR_IO_PENDING) {
        DWORD wait=WaitForSingleObject(op->hEvent,timeout);
        if(wait!=WAIT_OBJECT_0) {
            error=wait==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError();
            CancelIoEx(usb->handle,op);
            /* Keep OVERLAPPED and caller buffer alive until the kernel retires I/O. */
            GetOverlappedResult(usb->handle,op,done,TRUE);
            usb->last_error=error; SetLastError(error); return 0;
        }
        if(!GetOverlappedResult(usb->handle,op,done,FALSE)) error=GetLastError();
        else error=ERROR_SUCCESS;
    }
    usb->last_error=error; SetLastError(error);
    return error==ERROR_SUCCESS;
}

int l25_usb_ioctl(L25Usb *usb,DWORD code,void *in,DWORD in_size,void *out,DWORD out_size,DWORD *received) {
    OVERLAPPED op={0}; op.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!op.hEvent) {usb->last_error=GetLastError();return 0;}
    DWORD done=0;
    BOOL immediate=DeviceIoControl(usb->handle,code,in,in_size,out,out_size,&done,&op);
    int ok=complete(usb,&op,immediate,&done,5000);
    CloseHandle(op.hEvent); if(received) *received=done; return ok;
}

int l25_usb_open(L25Usb *usb,const wchar_t *requested) {
    usb->handle=INVALID_HANDLE_VALUE;usb->last_error=0;
    wchar_t port[512]={0};
    if(requested && requested[0]) {
        if(wcslen(requested)>=511) {usb->last_error=ERROR_INVALID_PARAMETER;return 0;}
        wcscpy(port,requested);
    } else if(!find_port(port,512)) {usb->last_error=GetLastError();return 0;}
    usb->handle=CreateFileW(port,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
    if(usb->handle==INVALID_HANDLE_VALUE) {usb->last_error=GetLastError();return 0;}
    DEVICE_DESCRIPTOR desc={0};DWORD got=0;
    if(!l25_usb_ioctl(usb,IOCTL_GET_DEVICE_DESCRIPTOR,NULL,0,&desc,sizeof(desc),&got) ||
       got<sizeof(desc) || desc.usVendorId!=0x04a9 || desc.usProductId!=0x2220) {
        DWORD error=usb->last_error?usb->last_error:ERROR_BAD_DEVICE;
        l25_usb_close(usb);usb->last_error=error;return 0;
    }
    /* usbscan uses seconds (not milliseconds); values above 214 overflow its timeout. */
    USBSCAN_TIMEOUT timeouts={8,8,1};
    l25_usb_ioctl(usb,IOCTL_SET_TIMEOUT,&timeouts,sizeof(timeouts),NULL,0,NULL);
    usb->last_error=0;return 1;
}

void l25_usb_cancel(L25Usb *usb) {if(usb->handle!=INVALID_HANDLE_VALUE) CancelIoEx(usb->handle,NULL);}
void l25_usb_close(L25Usb *usb) {if(usb->handle!=INVALID_HANDLE_VALUE) {l25_usb_cancel(usb);CloseHandle(usb->handle);usb->handle=INVALID_HANDLE_VALUE;}}

static int transfer(L25Usb *usb,void *data,size_t *length,DWORD timeout,int write) {
    if(!length || *length>MAXDWORD || (!data && *length)) {usb->last_error=ERROR_INVALID_PARAMETER;return 0;}
    OVERLAPPED op={0};op.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!op.hEvent) {usb->last_error=GetLastError();return 0;}
    DWORD done=0;
    BOOL immediate=write?WriteFile(usb->handle,data,(DWORD)*length,&done,&op):ReadFile(usb->handle,data,(DWORD)*length,&done,&op);
    int ok=complete(usb,&op,immediate,&done,timeout);
    CloseHandle(op.hEvent);*length=done;return ok;
}
int l25_usb_read(L25Usb *usb,void *data,size_t *length,DWORD timeout) {return transfer(usb,data,length,timeout,0);}
int l25_usb_write(L25Usb *usb,const void *data,size_t *length,DWORD timeout) {return transfer(usb,(void*)data,length,timeout,1);}
