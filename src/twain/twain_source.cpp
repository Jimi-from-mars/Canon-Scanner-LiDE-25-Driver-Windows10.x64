// Native TWAIN data source. All hardware operations go through our WIA driver.
#define UNICODE
#include <windows.h>
#include "../../third_party/twain/twain.h"
#include "wia_acquire.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <vector>
#include <new>

namespace {
HMODULE module=nullptr;
TW_ENTRYPOINT manager={};
TW_IDENTITY application={},source={};
TW_UINT16 condition=TWCC_SUCCESS;
int state=3, units=TWUN_INCHES, mechanism=TWSX_NATIVE, row=0;
bool scanning=false,cancelled=false,show_ui=false,ui_only=false,indicators=true;
HWND window=nullptr,parent=nullptr;
lide25::Settings settings;
lide25::Image image;
constexpr UINT ready_message=WM_APP+25;
constexpr int scan_button=100,cancel_button=101,dpi_combo=102,mode_combo=103,status_text=104;
TW_UINT16 fail(TW_UINT16 code){condition=code;return TWRC_FAILURE;}
TW_HANDLE allocate(TW_UINT32 n){return manager.DSM_MemAllocate?manager.DSM_MemAllocate(n):GlobalAlloc(GHND,n);}
void *lock(TW_HANDLE h){return manager.DSM_MemLock?manager.DSM_MemLock(h):GlobalLock(h);}
void unlock(TW_HANDLE h){if(manager.DSM_MemUnlock)manager.DSM_MemUnlock(h);else GlobalUnlock(h);}
void release(TW_HANDLE h){if(manager.DSM_MemFree)manager.DSM_MemFree(h);else GlobalFree(h);}
double value(TW_FIX32 n){return n.Whole+n.Frac/65536.0;}
TW_FIX32 fixed(double n){int64_t bits=(int64_t)std::llround(n*65536.0);TW_FIX32 f;f.Whole=(TW_INT16)(bits>>16);f.Frac=(TW_UINT16)bits;return f;}
TW_UINT32 bits(double n){auto f=fixed(n);TW_UINT32 result;std::memcpy(&result,&f,4);return result;}
double mm_per_unit(){return units==TWUN_MILLIMETERS?1.0:25.4;}
void defaults(){settings=lide25::Settings{};units=TWUN_INCHES;mechanism=TWSX_NATIVE;indicators=true;}
void identity(TW_IDENTITY &id,bool modern=true){
    // MSG_GET describes a source before the manager assigns its unique ID.
    // Do not copy a previous source's ID from the manager's reused buffer.
    std::memset(&id,0,sizeof(id));
    id.Version.MajorNum=0;id.Version.MinorNum=3;id.Version.Language=TWLG_USA;id.Version.Country=TWCY_USA;
    std::strcpy(id.Version.Info,"LiDE 25 WIA and TWAIN");id.ProtocolMajor=modern?2:1;id.ProtocolMinor=modern?5:9;
    id.SupportedGroups=DG_CONTROL|DG_IMAGE|(modern?DF_DS2:0);
    std::strcpy(id.Manufacturer,"LiDE25 Open Driver");std::strcpy(id.ProductFamily,"Flatbed scanner");std::strcpy(id.ProductName,"CanoScan LiDE 25 (Open)");
}
void find_manager(){
    if(manager.DSM_Entry)return;
    HMODULE m=GetModuleHandleW((application.SupportedGroups&DF_DSM2)?L"TWAINDSM.dll":L"TWAIN_32.DLL");
    if(!m)m=GetModuleHandleW(L"TWAINDSM.dll");
    if(m)manager.DSM_Entry=(DSMENTRYPROC)GetProcAddress(m,"DSM_Entry");
}
bool notify(TW_UINT16 message){find_manager();return manager.DSM_Entry&&manager.DSM_Entry(&source,&application,DG_CONTROL,DAT_NULL,message,nullptr)==TWRC_SUCCESS;}
TW_UINT16 one(TW_CAPABILITY &cap,TW_UINT16 type,TW_UINT32 n){
    cap.hContainer=allocate(sizeof(TW_ONEVALUE));if(!cap.hContainer)return fail(TWCC_LOWMEMORY);
    auto p=(TW_ONEVALUE*)lock(cap.hContainer);if(!p){release(cap.hContainer);cap.hContainer=nullptr;return fail(TWCC_LOWMEMORY);}
    p->ItemType=type;p->Item=n;unlock(cap.hContainer);cap.ConType=TWON_ONEVALUE;return TWRC_SUCCESS;
}
int item_size(TW_UINT16 type){return type==TWTY_INT16||type==TWTY_UINT16||type==TWTY_BOOL?2:4;}
TW_UINT16 list(TW_CAPABILITY &cap,TW_UINT16 type,const std::vector<TW_UINT32>&values,TW_UINT32 current,TW_UINT32 def,bool array=false){
    size_t base=array?offsetof(TW_ARRAY,ItemList):offsetof(TW_ENUMERATION,ItemList);
    cap.hContainer=allocate((TW_UINT32)(base+values.size()*item_size(type)));if(!cap.hContainer)return fail(TWCC_LOWMEMORY);
    BYTE *p=(BYTE*)lock(cap.hContainer);if(!p){release(cap.hContainer);cap.hContainer=nullptr;return fail(TWCC_LOWMEMORY);}
    std::memset(p,0,base);*(TW_UINT16*)p=type;
    if(array)((TW_ARRAY*)p)->NumItems=(TW_UINT32)values.size();
    else{auto e=(TW_ENUMERATION*)p;e->NumItems=(TW_UINT32)values.size();for(size_t i=0;i<values.size();++i){if(values[i]==current)e->CurrentIndex=(TW_UINT32)i;if(values[i]==def)e->DefaultIndex=(TW_UINT32)i;}}
    for(size_t i=0;i<values.size();++i)std::memcpy(p+base+i*item_size(type),&values[i],item_size(type));
    unlock(cap.hContainer);cap.ConType=array?TWON_ARRAY:TWON_ENUMERATION;return TWRC_SUCCESS;
}
const std::vector<TW_UINT32> capabilities={CAP_XFERCOUNT,CAP_SUPPORTEDCAPS,CAP_UICONTROLLABLE,CAP_INDICATORS,CAP_DEVICEONLINE,CAP_FEEDERENABLED,CAP_DUPLEX,CAP_DUPLEXENABLED,CAP_ENABLEDSUIONLY,ICAP_COMPRESSION,ICAP_PIXELTYPE,ICAP_UNITS,ICAP_XFERMECH,ICAP_XRESOLUTION,ICAP_YRESOLUTION,ICAP_BITDEPTH,ICAP_PIXELFLAVOR,ICAP_PLANARCHUNKY,ICAP_PHYSICALWIDTH,ICAP_PHYSICALHEIGHT,ICAP_SUPPORTEDSIZES,ICAP_MAXFRAMES,ICAP_BRIGHTNESS,ICAP_CONTRAST};
bool writable(TW_UINT16 cap){switch(cap){case CAP_XFERCOUNT:case CAP_INDICATORS:case CAP_FEEDERENABLED:case CAP_DUPLEXENABLED:case ICAP_COMPRESSION:case ICAP_PIXELTYPE:case ICAP_UNITS:case ICAP_XFERMECH:case ICAP_XRESOLUTION:case ICAP_YRESOLUTION:case ICAP_BITDEPTH:case ICAP_PIXELFLAVOR:case ICAP_PLANARCHUNKY:case ICAP_SUPPORTEDSIZES:case ICAP_BRIGHTNESS:case ICAP_CONTRAST:return true;default:return false;}}
TW_UINT16 capability(TW_UINT16 message,TW_CAPABILITY &cap){
    if(state<4)return fail(TWCC_SEQERROR);
    if(std::find(capabilities.begin(),capabilities.end(),cap.Cap)==capabilities.end())return fail(TWCC_CAPUNSUPPORTED);
    if(message==MSG_QUERYSUPPORT)return one(cap,TWTY_INT32,TWQC_GET|TWQC_GETCURRENT|TWQC_GETDEFAULT|(writable(cap.Cap)?TWQC_SET|TWQC_RESET:0));
    bool set=message==MSG_SET,reset=message==MSG_RESET;
    if((set||reset)&&(!writable(cap.Cap)||state!=4))return fail(state!=4?TWCC_SEQERROR:TWCC_CAPBADOPERATION);
    TW_UINT32 n=0;TW_UINT16 input_type=0;
    if(set){
        if(cap.ConType!=TWON_ONEVALUE||!cap.hContainer)return fail(TWCC_BADVALUE);
        auto p=(TW_ONEVALUE*)lock(cap.hContainer);if(!p)return fail(TWCC_BADVALUE);n=p->Item;input_type=p->ItemType;unlock(cap.hContainer);
        auto integer_type=[&](){return input_type==TWTY_INT16||input_type==TWTY_UINT16||input_type==TWTY_INT32||input_type==TWTY_UINT32||input_type==TWTY_BOOL;};
        if((cap.Cap==ICAP_XRESOLUTION||cap.Cap==ICAP_YRESOLUTION||cap.Cap==ICAP_BRIGHTNESS||cap.Cap==ICAP_CONTRAST)?input_type!=TWTY_FIX32:!integer_type())return fail(TWCC_BADVALUE);
    }
    if(!set&&!reset&&message!=MSG_GET&&message!=MSG_GETCURRENT&&message!=MSG_GETDEFAULT)return fail(TWCC_CAPBADOPERATION);
    bool def=message==MSG_GETDEFAULT||reset;
    auto scalar=[&](TW_UINT16 type,TW_UINT32 cur,TW_UINT32 initial){return one(cap,type,def?initial:cur);};
    auto enumeration=[&](TW_UINT16 type,const std::vector<TW_UINT32>&v,TW_UINT32 cur,TW_UINT32 initial){return message==MSG_GET?list(cap,type,v,cur,initial):scalar(type,cur,initial);};
    switch(cap.Cap){
    case CAP_SUPPORTEDCAPS:return list(cap,TWTY_UINT16,capabilities,0,0,true);
    case CAP_UICONTROLLABLE:case CAP_ENABLEDSUIONLY:return scalar(TWTY_BOOL,TRUE,TRUE);
    case CAP_DEVICEONLINE:return scalar(TWTY_BOOL,lide25::available(),TRUE);
    case CAP_XFERCOUNT:if(set&&n!=1&&(TW_INT16)n!=-1)return fail(TWCC_BADVALUE);if(set)return TWRC_SUCCESS;return scalar(TWTY_INT16,1,1);
    case CAP_FEEDERENABLED:case CAP_DUPLEXENABLED:if(set&&n)return fail(TWCC_BADVALUE);if(set)return TWRC_SUCCESS;return scalar(TWTY_BOOL,FALSE,FALSE);
    case CAP_DUPLEX:return scalar(TWTY_UINT16,TWDX_NONE,TWDX_NONE);
    case CAP_INDICATORS:if(set){if(n>1)return fail(TWCC_BADVALUE);indicators=n!=0;return TWRC_SUCCESS;}if(reset)indicators=true;return scalar(TWTY_BOOL,indicators,TRUE);
    case ICAP_COMPRESSION:if(set&&n!=TWCP_NONE)return fail(TWCC_BADVALUE);if(set)return TWRC_SUCCESS;return scalar(TWTY_UINT16,TWCP_NONE,TWCP_NONE);
    case ICAP_PIXELTYPE:if(set){if(n>TWPT_RGB)return fail(TWCC_BADVALUE);settings.pixel=(int)n;return TWRC_SUCCESS;}if(reset)settings.pixel=TWPT_RGB;return enumeration(TWTY_UINT16,{TWPT_BW,TWPT_GRAY,TWPT_RGB},settings.pixel,TWPT_RGB);
    case ICAP_UNITS:if(set){if(n!=TWUN_INCHES&&n!=TWUN_MILLIMETERS)return fail(TWCC_BADVALUE);units=(int)n;return TWRC_SUCCESS;}if(reset)units=TWUN_INCHES;return enumeration(TWTY_UINT16,{TWUN_INCHES,TWUN_MILLIMETERS},units,TWUN_INCHES);
    case ICAP_XFERMECH:if(set){if(n!=TWSX_NATIVE&&n!=TWSX_MEMORY)return fail(TWCC_BADVALUE);mechanism=(int)n;return TWRC_SUCCESS;}if(reset)mechanism=TWSX_NATIVE;return enumeration(TWTY_UINT16,{TWSX_NATIVE,TWSX_MEMORY},mechanism,TWSX_NATIVE);
    case ICAP_XRESOLUTION:case ICAP_YRESOLUTION:{double scale=units==TWUN_MILLIMETERS?25.4:1;std::vector<TW_UINT32> v;for(int dpi:{75,150,300,600,1200})v.push_back(bits(dpi/scale));if(set){TW_FIX32 f;std::memcpy(&f,&n,4);double requested=value(f)*scale;for(int dpi:{75,150,300,600,1200})if(std::abs(requested-dpi)<0.01){settings.dpi=dpi;return TWRC_SUCCESS;}return fail(TWCC_BADVALUE);}if(reset)settings.dpi=300;return enumeration(TWTY_FIX32,v,bits(settings.dpi/scale),bits(300/scale));}
    case ICAP_BITDEPTH:{int depth=settings.pixel==TWPT_BW?1:settings.pixel==TWPT_GRAY?8:24;if(set&&n!=(TW_UINT32)depth)return fail(TWCC_BADVALUE);if(set)return TWRC_SUCCESS;return scalar(TWTY_UINT16,depth,depth);}
    case ICAP_PIXELFLAVOR:if(set&&n!=TWPF_CHOCOLATE)return fail(TWCC_BADVALUE);if(set)return TWRC_SUCCESS;return scalar(TWTY_UINT16,TWPF_CHOCOLATE,TWPF_CHOCOLATE);
    case ICAP_PLANARCHUNKY:if(set&&n!=TWPC_CHUNKY)return fail(TWCC_BADVALUE);if(set)return TWRC_SUCCESS;return scalar(TWTY_UINT16,TWPC_CHUNKY,TWPC_CHUNKY);
    case ICAP_MAXFRAMES:return scalar(TWTY_UINT16,1,1);
    case ICAP_PHYSICALWIDTH:return scalar(TWTY_FIX32,bits(lide25::bed_width_mm/mm_per_unit()),bits(lide25::bed_width_mm/mm_per_unit()));
    case ICAP_PHYSICALHEIGHT:return scalar(TWTY_FIX32,bits(lide25::bed_height_mm/mm_per_unit()),bits(lide25::bed_height_mm/mm_per_unit()));
    case ICAP_SUPPORTEDSIZES:if(set){if(n!=TWSS_NONE&&n!=TWSS_A4&&n!=TWSS_USLETTER)return fail(TWCC_BADVALUE);settings.left=settings.top=0;settings.width=n==TWSS_A4?210:lide25::bed_width_mm;settings.height=n==TWSS_USLETTER?279.4:lide25::bed_height_mm;return TWRC_SUCCESS;}if(reset){settings.left=settings.top=0;settings.width=lide25::bed_width_mm;settings.height=lide25::bed_height_mm;}return enumeration(TWTY_UINT16,{TWSS_NONE,TWSS_A4,TWSS_USLETTER},TWSS_NONE,TWSS_NONE);
    case ICAP_BRIGHTNESS:case ICAP_CONTRAST:{int &target=cap.Cap==ICAP_BRIGHTNESS?settings.brightness:settings.contrast;if(set){TW_FIX32 f;std::memcpy(&f,&n,4);double v=value(f);if(v< -1000||v>1000)return fail(TWCC_BADVALUE);target=(int)std::lround(v);return TWRC_SUCCESS;}if(reset)target=0;if(message!=MSG_GET)return scalar(TWTY_FIX32,bits(target),bits(0));cap.hContainer=allocate(sizeof(TW_RANGE));if(!cap.hContainer)return fail(TWCC_LOWMEMORY);auto p=(TW_RANGE*)lock(cap.hContainer);if(!p){release(cap.hContainer);return fail(TWCC_LOWMEMORY);}*p={TWTY_FIX32,bits(-1000),bits(1000),bits(1),bits(0),bits(target)};unlock(cap.hContainer);cap.ConType=TWON_RANGE;return TWRC_SUCCESS;}
    }
    return fail(TWCC_CAPUNSUPPORTED);
}
void close_window(){if(window){DestroyWindow(window);window=nullptr;}}
void ready(){if(state!=5||ui_only)return;state=6;image.clear();row=0;if(!notify(MSG_XFERREADY)){state=5;condition=TWCC_OPERATIONERROR;}}
bool progress(void*,int percent){
    if(window){wchar_t text[80];std::swprintf(text,80,L"Scanning / 正在扫描：%d%%",percent);SetDlgItemTextW(window,status_text,text);
        MSG message;while(PeekMessageW(&message,window,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
    return !cancelled;
}
TW_UINT16 ensure_image(){
    if(!image.dib.empty())return TWRC_SUCCESS;
    if(scanning)return fail(TWCC_SEQERROR);scanning=true;cancelled=false;
    if(window){EnableWindow(GetDlgItem(window,scan_button),FALSE);SetDlgItemTextW(window,status_text,L"Scanning / 正在扫描…");}
    HRESULT hr=lide25::acquire(settings,image,progress,nullptr);scanning=false;
    if(window)SetDlgItemTextW(window,status_text,SUCCEEDED(hr)&&hr!=S_FALSE?L"Complete / 扫描完成":L"Stopped / 扫描已停止");
    if(cancelled||hr==S_FALSE){image.clear();cancelled=true;state=7;return TWRC_CANCEL;}
    if(FAILED(hr)){image.clear();return fail(hr==E_OUTOFMEMORY?TWCC_LOWMEMORY:hr==(HRESULT)0x80210006L?TWCC_MAXCONNECTIONS:TWCC_OPERATIONERROR);}
    return TWRC_SUCCESS;
}
LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case ready_message:ready();return 0;
    case WM_COMMAND:
        if(LOWORD(w)==scan_button&&state==5){
            {int selected=(int)SendDlgItemMessageW(hwnd,dpi_combo,CB_GETCURSEL,0,0);const int dpi[]={75,150,300,600,1200};if(selected>=0&&selected<5)settings.dpi=dpi[selected];selected=(int)SendDlgItemMessageW(hwnd,mode_combo,CB_GETCURSEL,0,0);if(selected>=0&&selected<3)settings.pixel=2-selected;}
            if(!ui_only){PostMessageW(hwnd,ready_message,0,0);}
            else notify(MSG_CLOSEDSOK);return 0;
        }
        if(LOWORD(w)==cancel_button){if(scanning)cancelled=true;else notify(MSG_CLOSEDSREQ);return 0;}
        break;
    case WM_CLOSE:if(scanning)cancelled=true;else notify(MSG_CLOSEDSREQ);return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
bool create_window(bool visible){
    WNDCLASSW wc={};wc.lpfnWndProc=procedure;wc.hInstance=module;wc.lpszClassName=L"Lide25TwainSource";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"CanoScan LiDE 25",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU, CW_USEDEFAULT,CW_USEDEFAULT,470,250,parent,nullptr,module,nullptr);
    if(!window)return false;
    if(visible){
        auto control=[&](const wchar_t *cls,const wchar_t *text,DWORD style,int x,int y,int width,int height,int id){HWND child=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,window,(HMENU)(INT_PTR)id,module,nullptr);SendMessageW(child,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);return child;};
        control(L"STATIC",L"Resolution / 分辨率",0,18,20,175,22,0);HWND dpi=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,220,16,205,150,dpi_combo);
        int index=0,selected=0;for(int n:{75,150,300,600,1200}){wchar_t text[20];std::swprintf(text,20,L"%d dpi",n);SendMessageW(dpi,CB_ADDSTRING,0,(LPARAM)text);if(n==settings.dpi)selected=index;++index;}SendMessageW(dpi,CB_SETCURSEL,selected,0);
        control(L"STATIC",L"Image mode / 图像模式",0,18,61,185,22,0);HWND mode=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,220,57,205,150,mode_combo);
        for(auto text:{L"Color / 彩色",L"Grayscale / 灰度",L"Black and white / 黑白"})SendMessageW(mode,CB_ADDSTRING,0,(LPARAM)text);SendMessageW(mode,CB_SETCURSEL,2-settings.pixel,0);
        control(L"STATIC",L"Scan area follows the application's settings.\n扫描区域由调用软件设置；默认扫描整个平板。",0,18,100,410,36,0);
        control(L"STATIC",L"Ready / 就绪",0,18,150,230,22,status_text);
        control(L"BUTTON",ui_only?L"OK / 确定":L"Scan / 扫描",BS_DEFPUSHBUTTON|WS_TABSTOP,250,150,85,30,scan_button);
        control(L"BUTTON",L"Cancel / 取消",WS_TABSTOP,345,150,85,30,cancel_button);
        ShowWindow(window,SW_SHOW);UpdateWindow(window);
    }
    return true;
}
TW_UINT16 layout(TW_UINT16 message,TW_IMAGELAYOUT &data){
    if(state<4)return fail(TWCC_SEQERROR);
    if(message==MSG_SET){if(state!=4)return fail(TWCC_SEQERROR);double scale=mm_per_unit(),left=value(data.Frame.Left)*scale,top=value(data.Frame.Top)*scale,right=value(data.Frame.Right)*scale,bottom=value(data.Frame.Bottom)*scale;
        if(left<0||top<0||right<=left||bottom<=top||right>lide25::bed_width_mm+0.001||bottom>lide25::bed_height_mm+0.001)return fail(TWCC_BADVALUE);
        auto candidate=settings;candidate.left=left;candidate.top=top;candidate.width=std::min(right,lide25::bed_width_mm)-left;candidate.height=std::min(bottom,lide25::bed_height_mm)-top;auto area=lide25::geometry(candidate);if(area.width<1||area.height<1)return fail(TWCC_BADVALUE);settings=candidate;return TWRC_SUCCESS;}
    if(message==MSG_RESET){if(state!=4)return fail(TWCC_SEQERROR);settings.left=settings.top=0;settings.width=lide25::bed_width_mm;settings.height=lide25::bed_height_mm;}
    if(message!=MSG_GET&&message!=MSG_GETDEFAULT&&message!=MSG_RESET)return fail(TWCC_BADPROTOCOL);
    auto s=message==MSG_GETDEFAULT?lide25::Settings{}:settings;double scale=mm_per_unit();
    data.Frame={fixed(s.left/scale),fixed(s.top/scale),fixed((s.left+s.width)/scale),fixed((s.top+s.height)/scale)};data.DocumentNumber=data.PageNumber=data.FrameNumber=1;return TWRC_SUCCESS;
}
TW_UINT16 image_info(TW_IMAGEINFO &info){
    if(state<6)return fail(TWCC_SEQERROR);std::memset(&info,0,sizeof(info));double scale=units==TWUN_MILLIMETERS?25.4:1;info.XResolution=info.YResolution=fixed(settings.dpi/scale);
    auto area=lide25::geometry(settings);info.ImageWidth=image.width?image.width:area.width;info.ImageLength=image.height?image.height:area.height;
    info.PixelType=(TW_INT16)settings.pixel;info.SamplesPerPixel=settings.pixel==TWPT_RGB?3:1;info.BitsPerPixel=settings.pixel==TWPT_RGB?24:settings.pixel==TWPT_GRAY?8:1;
    for(int i=0;i<info.SamplesPerPixel;++i)info.BitsPerSample[i]=settings.pixel==TWPT_BW?1:8;
    info.Planar=FALSE;info.Compression=TWCP_NONE;return TWRC_SUCCESS;
}
TW_UINT16 entry(TW_IDENTITY *origin,TW_UINT32 group,TW_UINT16 data,TW_UINT16 message,void *argument){
    if(group==DG_CONTROL&&data==DAT_STATUS&&message==MSG_GET&&argument){auto s=(TW_STATUS*)argument;s->ConditionCode=condition;s->Reserved=0;condition=TWCC_SUCCESS;return TWRC_SUCCESS;}
    if(group==DG_CONTROL&&data==DAT_IDENTITY&&argument){
        auto id=(TW_IDENTITY*)argument;
        if(message==MSG_GET){identity(*id,sizeof(void*)==8||(origin&&(origin->SupportedGroups&DF_DSM2)));return TWRC_SUCCESS;}
        if(message==MSG_OPENDS){if(state!=3||!origin)return fail(TWCC_SEQERROR);application=*origin;source=*id;defaults();state=4;find_manager();return TWRC_SUCCESS;}
        if(message==MSG_CLOSEDS){if(state!=4||scanning)return fail(TWCC_SEQERROR);close_window();image.clear();state=3;manager={};return TWRC_SUCCESS;}
    }
    if(group==DG_CONTROL&&data==DAT_ENTRYPOINT&&message==MSG_SET&&argument){auto p=(TW_ENTRYPOINT*)argument;if(p->Size<sizeof(TW_ENTRYPOINT)||!p->DSM_Entry)return fail(TWCC_BADVALUE);manager=*p;return TWRC_SUCCESS;}
    if(state<4)return fail(TWCC_SEQERROR);
    if(origin&&origin->Id!=application.Id)return fail(TWCC_SEQERROR);
    if(!argument)return fail(TWCC_BADVALUE);
    if(group==DG_CONTROL){
        if(data==DAT_CAPABILITY)return capability(message,*(TW_CAPABILITY*)argument);
        if(data==DAT_XFERGROUP&&message==MSG_GET){*(TW_UINT32*)argument=DG_IMAGE;return TWRC_SUCCESS;}
        if(data==DAT_EVENT&&message==MSG_PROCESSEVENT){auto event=(TW_EVENT*)argument;event->TWMessage=MSG_NULL;auto msg=(MSG*)event->pEvent;if(window&&msg&&(msg->hwnd==window||IsChild(window,msg->hwnd))){if(!IsDialogMessageW(window,msg)){TranslateMessage(msg);DispatchMessageW(msg);}return TWRC_DSEVENT;}return TWRC_NOTDSEVENT;}
        if(data==DAT_USERINTERFACE){auto ui=(TW_USERINTERFACE*)argument;
            if(message==MSG_ENABLEDS||message==MSG_ENABLEDSUIONLY){if(state!=4)return fail(TWCC_SEQERROR);parent=(HWND)ui->hParent;show_ui=ui->ShowUI!=FALSE;ui_only=message==MSG_ENABLEDSUIONLY;cancelled=false;
                if(!create_window(show_ui||ui_only))return fail(TWCC_LOWMEMORY);state=5;ui->ModalUI=FALSE;if(!show_ui&&!ui_only)PostMessageW(window,ready_message,0,0);return TWRC_SUCCESS;}
            if(message==MSG_DISABLEDS){if(state!=5||scanning)return fail(TWCC_SEQERROR);close_window();image.clear();state=4;return TWRC_SUCCESS;}
        }
        if(data==DAT_SETUPMEMXFER&&message==MSG_GET){auto p=(TW_SETUPMEMXFER*)argument;TW_UINT32 bytes=(TW_UINT32)((lide25::geometry(settings).width*(settings.pixel==2?24:settings.pixel==1?8:1)+7)/8);p->MinBufSize=std::max<TW_UINT32>(bytes,1);p->MaxBufSize=1024*1024;p->Preferred=std::max<TW_UINT32>(65536,p->MinBufSize);return TWRC_SUCCESS;}
        if(data==DAT_PENDINGXFERS){auto p=(TW_PENDINGXFERS*)argument;
            if(message==MSG_GET){p->Count=(state==6||state==7)&&(!cancelled)?1:0;p->EOJ=0;return TWRC_SUCCESS;}
            if(message==MSG_ENDXFER){if(state!=7)return fail(TWCC_SEQERROR);p->Count=0;p->EOJ=0;image.clear();row=0;state=5;if(window)EnableWindow(GetDlgItem(window,scan_button),TRUE);return TWRC_SUCCESS;}
            if(message==MSG_RESET){if(state<6||scanning)return fail(TWCC_SEQERROR);image.clear();row=0;state=5;p->Count=0;p->EOJ=0;return TWRC_SUCCESS;}
        }
    }
    if(group==DG_IMAGE){
        if(data==DAT_IMAGELAYOUT)return layout(message,*(TW_IMAGELAYOUT*)argument);
        if(data==DAT_IMAGEINFO&&message==MSG_GET)return image_info(*(TW_IMAGEINFO*)argument);
        if(data==DAT_IMAGENATIVEXFER&&message==MSG_GET){
            if(state!=6||mechanism!=TWSX_NATIVE)return fail(TWCC_SEQERROR);*(TW_HANDLE*)argument=nullptr;TW_UINT16 result=ensure_image();if(result!=TWRC_SUCCESS)return result;
            TW_HANDLE h=allocate((TW_UINT32)image.dib.size());if(!h)return fail(TWCC_LOWMEMORY);void *p=lock(h);if(!p){release(h);return fail(TWCC_LOWMEMORY);}std::memcpy(p,image.dib.data(),image.dib.size());
            // Native TWAIN consumers commonly expect a bottom-up Windows DIB.
            // WIA supplies top-down rows; preserve the displayed orientation.
            if(!image.bottom_up){
                ((BITMAPINFOHEADER*)p)->biHeight=image.height;
                auto pixels=(BYTE*)p+image.pixels_offset;
                for(int y=0;y<image.height;++y)std::memcpy(pixels+(size_t)y*image.stride,image.dib.data()+image.pixels_offset+(size_t)(image.height-1-y)*image.stride,image.stride);
            }
            unlock(h);*(TW_HANDLE*)argument=h;state=7;return TWRC_XFERDONE;
        }
        if(data==DAT_IMAGEMEMXFER&&message==MSG_GET){
            if((state!=6&&state!=7)||mechanism!=TWSX_MEMORY)return fail(TWCC_SEQERROR);auto p=(TW_IMAGEMEMXFER*)argument;
            if(!p->Memory.TheMem||!p->Memory.Length||!(p->Memory.Flags&(TWMF_POINTER|TWMF_HANDLE)))return fail(TWCC_BADVALUE);
            TW_UINT16 result=ensure_image();if(result!=TWRC_SUCCESS)return result;
            int bytes=(image.width*image.depth+7)/8,rows=std::min<int>((int)(p->Memory.Length/bytes),image.height-row);if(rows<1)return fail(TWCC_BADVALUE);
            BYTE *output=(p->Memory.Flags&TWMF_POINTER)?(BYTE*)p->Memory.TheMem:(BYTE*)lock((TW_HANDLE)p->Memory.TheMem);if(!output)return fail(TWCC_BADVALUE);
            for(int i=0;i<rows;++i){int input_row=image.bottom_up?image.height-1-row-i:row+i;auto input=image.dib.data()+image.pixels_offset+(size_t)input_row*image.stride;auto dest=output+(size_t)i*bytes;
                if(image.depth==24)for(int x=0;x<image.width;++x){dest[x*3]=input[x*3+2];dest[x*3+1]=input[x*3+1];dest[x*3+2]=input[x*3];}else std::memcpy(dest,input,bytes);}
            if(!(p->Memory.Flags&TWMF_POINTER))unlock((TW_HANDLE)p->Memory.TheMem);
            p->Compression=TWCP_NONE;p->BytesPerRow=bytes;p->Columns=image.width;p->Rows=rows;p->XOffset=0;p->YOffset=row;p->BytesWritten=rows*bytes;row+=rows;state=7;return row==image.height?TWRC_XFERDONE:TWRC_SUCCESS;
        }
    }
    return fail(TWCC_BADPROTOCOL);
}
}
extern "C" __declspec(dllexport) TW_UINT16 TW_CALLINGSTYLE DS_Entry(pTW_IDENTITY origin,TW_UINT32 group,TW_UINT16 data,TW_UINT16 message,TW_MEMREF argument){
    try{
        return entry(origin,group,data,message,argument);
    }catch(const std::bad_alloc&){scanning=false;return fail(TWCC_LOWMEMORY);}catch(...){scanning=false;return fail(TWCC_OPERATIONERROR);}
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){module=h;DisableThreadLibraryCalls(h);
}return TRUE;}
