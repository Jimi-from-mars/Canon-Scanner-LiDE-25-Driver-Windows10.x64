// ABI and acquisition test client; --dsm exercises an actual TWAIN manager.
#define UNICODE
#include <windows.h>
#include "../third_party/twain/twain.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <stdexcept>
#include <thread>
#include <chrono>
static DSENTRYPROC ds=nullptr;static DSMENTRYPROC dsm=nullptr;
static TW_IDENTITY app={},scanner={};static TW_ENTRYPOINT memory={};static bool ready=false;
static TW_UINT16 TW_CALLINGSTYLE callback(pTW_IDENTITY,pTW_IDENTITY,TW_UINT32,TW_UINT16,TW_UINT16 message,TW_MEMREF){if(message==MSG_XFERREADY)ready=true;return TWRC_SUCCESS;}
static TW_UINT16 call(TW_UINT32 g,TW_UINT16 d,TW_UINT16 m,void*p){return dsm?dsm(&app,&scanner,g,d,m,p):ds(&app,g,d,m,p);}
static void require(bool b,const char *s){if(!b)throw std::runtime_error(s);}
static void *lock(TW_HANDLE h){return memory.DSM_MemLock?memory.DSM_MemLock(h):GlobalLock(h);}
static void unlock(TW_HANDLE h){if(memory.DSM_MemUnlock)memory.DSM_MemUnlock(h);else GlobalUnlock(h);}
static void release(TW_HANDLE h){if(memory.DSM_MemFree)memory.DSM_MemFree(h);else GlobalFree(h);}
static TW_UINT16 set(TW_UINT16 cap,TW_UINT16 type,TW_UINT32 n){TW_ONEVALUE one={type,n};TW_HANDLE h=memory.DSM_MemAllocate?memory.DSM_MemAllocate(sizeof(one)):GlobalAlloc(GHND,sizeof(one));require(h!=nullptr,"allocate capability");void*p=lock(h);require(p!=nullptr,"lock capability");std::memcpy(p,&one,sizeof(one));unlock(h);TW_CAPABILITY c={cap,TWON_ONEVALUE,h};auto result=call(DG_CONTROL,DAT_CAPABILITY,MSG_SET,&c);release(h);return result;}
static TW_FIX32 fixed(double value){long n=(long)(value*65536+0.5);return {(TW_INT16)(n>>16),(TW_UINT16)n};}
static TW_UINT32 fixbits(double n){TW_FIX32 f=fixed(n);TW_UINT32 r;std::memcpy(&r,&f,4);return r;}
static void save(const wchar_t *path,const std::vector<BYTE>&dib){auto info=(const BITMAPINFOHEADER*)dib.data();DWORD palette=info->biBitCount<=8?(info->biClrUsed?info->biClrUsed:1u<<info->biBitCount):0;BITMAPFILEHEADER f={};f.bfType=0x4d42;f.bfOffBits=14+info->biSize+palette*4;f.bfSize=(DWORD)(14+dib.size());HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);require(file!=INVALID_HANDLE_VALUE,"create output");DWORD wrote=0;bool ok=WriteFile(file,&f,sizeof(f),&wrote,nullptr)&&wrote==sizeof(f)&&WriteFile(file,dib.data(),(DWORD)dib.size(),&wrote,nullptr)&&wrote==dib.size();CloseHandle(file);require(ok,"write output");}
int wmain(int argc,wchar_t **argv)try{
    require(argc>=3,"Usage: twain-client --direct source.ds | --dsm manager.dll [output.bmp dpi pixel native|memory cancel]");
    HMODULE library=LoadLibraryExW(argv[2],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);require(library!=nullptr,"load source or manager");
    app.Id=1;app.Version.MajorNum=1;app.Version.Language=TWLG_USA;app.Version.Country=TWCY_USA;app.ProtocolMajor=2;app.ProtocolMinor=5;app.SupportedGroups=DG_CONTROL|DG_IMAGE|DF_APP2;std::strcpy(app.Manufacturer,"LiDE25 Open Driver");std::strcpy(app.ProductFamily,"Test client");std::strcpy(app.ProductName,"LiDE25 TWAIN Test");
    HWND parent=CreateWindowExW(0,L"STATIC",L"LiDE25 TWAIN Test",WS_OVERLAPPED,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);require(parent!=nullptr,"create application message window");
    if(!std::wcscmp(argv[1],L"--dsm")){
        dsm=(DSMENTRYPROC)GetProcAddress(library,"DSM_Entry");require(dsm!=nullptr,"manager entry");require(dsm(&app,nullptr,DG_CONTROL,DAT_PARENT,MSG_OPENDSM,&parent)==TWRC_SUCCESS,"open manager");
        memory.Size=sizeof(memory);if(dsm(&app,nullptr,DG_CONTROL,DAT_ENTRYPOINT,MSG_GET,&memory)!=TWRC_SUCCESS)memory={};
        TW_UINT16 rc=dsm(&app,nullptr,DG_CONTROL,DAT_IDENTITY,MSG_GETFIRST,&scanner);
        while(rc==TWRC_SUCCESS){printf("discovered=%s vendor=%s protocol=%u.%u groups=%08lx\n",scanner.ProductName,scanner.Manufacturer,scanner.ProtocolMajor,scanner.ProtocolMinor,(unsigned long)scanner.SupportedGroups);if(!std::strcmp(scanner.Manufacturer,"LiDE25 Open Driver"))break;rc=dsm(&app,nullptr,DG_CONTROL,DAT_IDENTITY,MSG_GETNEXT,&scanner);}
        require(rc==TWRC_SUCCESS,"find installed LiDE25 source through manager");
        require(dsm(&app,nullptr,DG_CONTROL,DAT_IDENTITY,MSG_OPENDS,&scanner)==TWRC_SUCCESS,"open installed source");
        if(app.SupportedGroups&DF_DSM2){TW_CALLBACK cb={};cb.CallBackProc=(TW_MEMREF)callback;require(dsm(&app,&scanner,DG_CONTROL,DAT_CALLBACK,MSG_REGISTER_CALLBACK,&cb)==TWRC_SUCCESS,"register callback");}
    }else{
        ds=(DSENTRYPROC)GetProcAddress(library,"DS_Entry");require(ds!=nullptr,"source export");scanner.Id=2;require(call(DG_CONTROL,DAT_IDENTITY,MSG_GET,&scanner)==TWRC_SUCCESS,"source identity");require(call(DG_CONTROL,DAT_IDENTITY,MSG_OPENDS,&scanner)==TWRC_SUCCESS,"open source");memory.Size=sizeof(memory);memory.DSM_Entry=callback;require(call(DG_CONTROL,DAT_ENTRYPOINT,MSG_SET,&memory)==TWRC_SUCCESS,"set manager entrypoint");
    }
    printf("source=%s bitness=%u\n",scanner.ProductName,(unsigned)(sizeof(void*)*8));
    TW_CAPABILITY cap={CAP_SUPPORTEDCAPS,TWON_DONTCARE16,nullptr};require(call(DG_CONTROL,DAT_CAPABILITY,MSG_GET,&cap)==TWRC_SUCCESS,"supported capabilities");auto a=(TW_ARRAY*)lock(cap.hContainer);require(a&&a->NumItems>=20,"capability array");unlock(cap.hContainer);release(cap.hContainer);
    for(int pixel=0;pixel<=2;++pixel){
        require(set(ICAP_PIXELTYPE,TWTY_UINT16,pixel)==TWRC_SUCCESS,"set mode for bit-depth contract");
        TW_CAPABILITY depth={ICAP_BITDEPTH,TWON_DONTCARE16,nullptr};
        require(call(DG_CONTROL,DAT_CAPABILITY,MSG_GETCURRENT,&depth)==TWRC_SUCCESS&&depth.ConType==TWON_ONEVALUE,"current bit depth");
        auto value=(TW_ONEVALUE*)lock(depth.hContainer);require(value&&value->ItemType==TWTY_UINT16&&value->Item==(pixel==0?1u:pixel==1?8u:24u),"bit depth must describe total bits per pixel");unlock(depth.hContainer);release(depth.hContainer);
    }
    require(set(ICAP_XRESOLUTION,TWTY_FIX32,fixbits(100))==TWRC_FAILURE,"reject unsupported dpi");TW_STATUS status={};require(call(DG_CONTROL,DAT_STATUS,MSG_GET,&status)==TWRC_SUCCESS&&status.ConditionCode==TWCC_BADVALUE,"invalid dpi condition");
    if(argc>3){
        int dpi=argc>4?_wtoi(argv[4]):150,pixel=argc>5?_wtoi(argv[5]):2;bool mem=argc>6&&!std::wcscmp(argv[6],L"memory"),cancel=argc>7&&!std::wcscmp(argv[7],L"cancel"),mm=argc>8&&!std::wcscmp(argv[8],L"mm");
        if(mm)require(set(ICAP_UNITS,TWTY_UINT16,TWUN_MILLIMETERS)==TWRC_SUCCESS,"set millimeter units");
        require(set(ICAP_XRESOLUTION,TWTY_FIX32,fixbits(dpi/(mm?25.4:1)))==TWRC_SUCCESS,"set dpi");require(set(ICAP_PIXELTYPE,TWTY_UINT16,pixel)==TWRC_SUCCESS,"set image mode");require(set(ICAP_XFERMECH,TWTY_UINT16,mem?TWSX_MEMORY:TWSX_NATIVE)==TWRC_SUCCESS,"set mechanism");
        TW_IMAGELAYOUT layout={};layout.Frame={fixed(0),fixed(0),fixed(mm?25.4:1),fixed(mm?25.4:1)};require(call(DG_IMAGE,DAT_IMAGELAYOUT,MSG_SET,&layout)==TWRC_SUCCESS,"set scan region");
        TW_USERINTERFACE ui={FALSE,FALSE,parent};require(call(DG_CONTROL,DAT_USERINTERFACE,MSG_ENABLEDS,&ui)==TWRC_SUCCESS,"enable source");
        ULONGLONG deadline=GetTickCount64()+10000;
        while(!ready&&GetTickCount64()<deadline){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TW_EVENT e={&message,MSG_NULL};if(call(DG_CONTROL,DAT_EVENT,MSG_PROCESSEVENT,&e)!=TWRC_DSEVENT){TranslateMessage(&message);DispatchMessageW(&message);}if(e.TWMessage==MSG_XFERREADY)ready=true;}Sleep(10);}
        require(ready,"transfer-ready notification");TW_IMAGEINFO info={};require(call(DG_IMAGE,DAT_IMAGEINFO,MSG_GET,&info)==TWRC_SUCCESS,"image info");
        printf("image=%ldx%ld depth=%d mechanism=%s\n",info.ImageWidth,info.ImageLength,info.BitsPerPixel,mem?"memory":"native");fflush(stdout);
        std::thread cancelling;
        if(cancel)cancelling=std::thread([](){std::this_thread::sleep_for(std::chrono::milliseconds(800));HWND h=FindWindowW(L"Lide25TwainSource",nullptr);DWORD process=0;if(h)GetWindowThreadProcessId(h,&process);if(process==GetCurrentProcessId())PostMessageW(h,WM_COMMAND,101,0);});
        TW_UINT16 result=TWRC_FAILURE;std::vector<BYTE> dib;
        if(!mem){TW_HANDLE h=nullptr;result=call(DG_IMAGE,DAT_IMAGENATIVEXFER,MSG_GET,&h);if(result==TWRC_XFERDONE){auto b=(BITMAPINFOHEADER*)lock(h);require(b!=nullptr,"lock native DIB");require(b->biHeight>0,"native DIB must be bottom-up for legacy consumers");DWORD palette=b->biBitCount<=8?(b->biClrUsed?b->biClrUsed:1u<<b->biBitCount):0;size_t size=b->biSize+palette*4+((b->biWidth*b->biBitCount+31)/32)*4*std::abs(b->biHeight);dib.assign((BYTE*)b,(BYTE*)b+size);unlock(h);release(h);}}
        else{
            int stride=((info.ImageWidth*info.BitsPerPixel+31)/32)*4;DWORD colors=info.BitsPerPixel<=8?1u<<info.BitsPerPixel:0;
            dib.resize(40+colors*4+(size_t)stride*info.ImageLength);auto b=(BITMAPINFOHEADER*)dib.data();b->biSize=40;b->biWidth=info.ImageWidth;b->biHeight=-info.ImageLength;b->biPlanes=1;b->biBitCount=info.BitsPerPixel;b->biSizeImage=stride*info.ImageLength;
            for(DWORD i=0;i<colors;++i){BYTE gray=(BYTE)(i*255/(colors-1));dib[40+i*4]=dib[41+i*4]=dib[42+i*4]=gray;}
            std::vector<BYTE> buffer(65536);do{TW_IMAGEMEMXFER transfer={};transfer.Memory={TWMF_APPOWNS|TWMF_POINTER,(TW_UINT32)buffer.size(),buffer.data()};result=call(DG_IMAGE,DAT_IMAGEMEMXFER,MSG_GET,&transfer);if(result==TWRC_SUCCESS||result==TWRC_XFERDONE){require(transfer.YOffset+transfer.Rows<=(TW_UINT32)info.ImageLength,"memory transfer row bounds");for(TW_UINT32 y=0;y<transfer.Rows;++y){auto from=buffer.data()+y*transfer.BytesPerRow;auto to=dib.data()+40+colors*4+(size_t)(transfer.YOffset+y)*stride;if(info.BitsPerPixel==24)for(int x=0;x<info.ImageWidth;++x){to[x*3]=from[x*3+2];to[x*3+1]=from[x*3+1];to[x*3+2]=from[x*3];}else std::memcpy(to,from,transfer.BytesPerRow);}}}while(result==TWRC_SUCCESS);
        }
        if(cancelling.joinable())cancelling.join();printf("transfer_result=%u\n",result);require(result==(cancel?TWRC_CANCEL:TWRC_XFERDONE),"image transfer result");
        TW_PENDINGXFERS pending={};require(call(DG_CONTROL,DAT_PENDINGXFERS,MSG_ENDXFER,&pending)==TWRC_SUCCESS&&pending.Count==0,"end transfer");require(call(DG_CONTROL,DAT_USERINTERFACE,MSG_DISABLEDS,&ui)==TWRC_SUCCESS,"disable source");if(!cancel)save(argv[3],dib);
    }
    if(dsm){require(dsm(&app,nullptr,DG_CONTROL,DAT_IDENTITY,MSG_CLOSEDS,&scanner)==TWRC_SUCCESS,"close source");require(dsm(&app,nullptr,DG_CONTROL,DAT_PARENT,MSG_CLOSEDSM,&parent)==TWRC_SUCCESS,"close manager");}
    else require(call(DG_CONTROL,DAT_IDENTITY,MSG_CLOSEDS,&scanner)==TWRC_SUCCESS,"close source");
    DestroyWindow(parent);FreeLibrary(library);printf("passed=true\n");return 0;
}catch(const std::exception&e){std::fprintf(stderr,"FAILED: %s\n",e.what());return 1;}
