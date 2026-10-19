// LiDE25 offline setup helper. Only Windows inbox USB components are selected.
#define UNICODE
#define _UNICODE
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#include "device_binding.h"
#include <initguid.h>
#include "../../third_party/wia-reference/wia_lh.h"
#include "../../third_party/nlohmann/json.hpp"
#include "setup_manifest.h"
#include "../transport/usbscan_transport.h"
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
using json=nlohmann::json;namespace fs=std::filesystem;
static const wchar_t *clsid=L"{0DDF42CB-24F2-4CEC-9FD7-2E6A25C91B10}";
static const std::wstring com_path=std::wstring(L"SOFTWARE\\Classes\\CLSID\\")+clsid+L"\\InprocServer32";
static const std::vector<std::wstring> names={L"USDClass",L"Capabilities",L"SubClass",L"DeviceType",L"DeviceSubType",L"FriendlyName",L"Vendor",L"IsPnP",L"DeviceID",L"CreateFileName"};
static fs::path app;static json log_events=json::array();
static std::string utf8(const std::wstring &s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),r.data(),n,nullptr,nullptr);return r;}
static std::wstring wide(const std::string &s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);if(!n)throw std::runtime_error("Invalid UTF-8 in setup state.");std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),r.data(),n);return r;}
static fs::path runtime_dll(){return app/L"versions"/wide(std::string(L25_DRIVER_SHA256).substr(0,16))/L"Lide25Wia.dll";}
static void result_message(const std::wstring &message){auto file=app/L"setup-result.ini";WritePrivateProfileStringW(L"Result",L"Message",message.c_str(),file.c_str());}
static json read_json(const fs::path &p){std::ifstream f(p);if(!f)throw std::runtime_error("Setup state could not be opened.");json j;f>>j;return j;}
static void write_json(const fs::path &p,const json &j){auto tmp=p;tmp+=L".tmp";std::ofstream f(tmp,std::ios::binary|std::ios::trunc);f<<j.dump(2);f.close();if(!f)throw std::runtime_error("Could not save setup recovery state.");wincheck(MoveFileExW(tmp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"Commit setup recovery state");}
static void note(const std::string &s){log_events.push_back({{"time_ms",GetTickCount64()},{"message",s}});std::printf("%s\n",s.c_str());std::fflush(stdout);try{write_json(app/L"setup-log.json",log_events);}catch(...) {}}
struct Key {HKEY h=nullptr;Key(const std::wstring &path,bool create=false){LONG r=create?RegCreateKeyExW(HKEY_LOCAL_MACHINE,path.c_str(),0,nullptr,0,KEY_READ|KEY_WRITE|KEY_WOW64_64KEY,nullptr,&h,nullptr):RegOpenKeyExW(HKEY_LOCAL_MACHINE,path.c_str(),0,KEY_READ|KEY_WOW64_64KEY,&h);if(r!=ERROR_SUCCESS)h=nullptr;}~Key(){if(h)RegCloseKey(h);}};
static json snapshot(const std::wstring &path,const std::vector<std::wstring> &keys){json j=json::object();Key k(path);for(auto &name:keys){DWORD type=0,n=0;LONG r=k.h?RegQueryValueExW(k.h,name.c_str(),nullptr,&type,nullptr,&n):ERROR_FILE_NOT_FOUND;
 if(r==ERROR_FILE_NOT_FOUND){j[utf8(name)]={{"exists",false}};continue;}if(r!=ERROR_SUCCESS||n>65536)throw std::runtime_error("Could not read original registry value.");std::vector<BYTE>b(n);if(n)wincheck(RegQueryValueExW(k.h,name.c_str(),nullptr,&type,b.data(),&n)==ERROR_SUCCESS,"Read original registry value");j[utf8(name)]={{"exists",true},{"type",type},{"bytes",b}};}return j;}
static void restore_values(const std::wstring &path,const json &j,const std::vector<std::wstring> &allowed){Key k(path,true);wincheck(k.h!=nullptr,"Open recovery registry key");for(auto &name:allowed){auto value=j.at(utf8(name));if(!value.at("exists").get<bool>()){LONG r=RegDeleteValueW(k.h,name.c_str());if(r!=ERROR_SUCCESS&&r!=ERROR_FILE_NOT_FOUND)throw std::runtime_error("Could not clear a newly added registry value.");continue;}
 auto b=value.at("bytes").get<std::vector<BYTE>>();DWORD type=value.at("type");if(b.size()>65536||type>REG_QWORD)throw std::runtime_error("Recovery value is out of bounds.");wincheck(RegSetValueExW(k.h,name.c_str(),0,type,b.empty()?nullptr:b.data(),(DWORD)b.size())==ERROR_SUCCESS,"Restore registry value");}}
static void str(Key &k,const wchar_t *name,const std::wstring &v){wincheck(RegSetValueExW(k.h,name,0,REG_SZ,(const BYTE*)v.c_str(),(DWORD)((v.size()+1)*2))==ERROR_SUCCESS,"Write scanner registration");}
static void number(Key &k,const wchar_t *name,DWORD v){wincheck(RegSetValueExW(k.h,name,0,REG_DWORD,(BYTE*)&v,sizeof(v))==ERROR_SUCCESS,"Write scanner capability");}
static std::wstring reg_string(const std::wstring &path,const wchar_t *name){Key k(path);wchar_t b[2048]={};DWORD n=sizeof(b),type=0;if(!k.h||RegQueryValueExW(k.h,name,nullptr,&type,(BYTE*)b,&n)!=ERROR_SUCCESS||type!=REG_SZ)return {};return b;}
static std::wstring wia_device_id(Device &d){auto id=d.property(SPDRP_DRIVER);if(!id.empty())CharUpperBuffW(id.data(),(DWORD)id.size());return id;}
// The authoritative recovery record lives in HKLM, with inherited administrator
// write access. A user-writable installation folder cannot alter rollback input.
static bool has_state(){Key k(L"SOFTWARE\\LiDE25OpenDriver\\Recovery");DWORD n=0;return k.h&&RegQueryValueExW(k.h,L"State",nullptr,nullptr,nullptr,&n)==ERROR_SUCCESS;}
static json load_state(){Key k(L"SOFTWARE\\LiDE25OpenDriver\\Recovery");DWORD type=0,n=0;if(!k.h||RegQueryValueExW(k.h,L"State",nullptr,&type,nullptr,&n)!=ERROR_SUCCESS||type!=REG_SZ||n>1024*1024)throw std::runtime_error("Protected recovery record is missing or invalid.");std::vector<wchar_t>b(n/2+1,0);wincheck(RegQueryValueExW(k.h,L"State",nullptr,&type,(BYTE*)b.data(),&n)==ERROR_SUCCESS,"Read protected recovery state");return json::parse(utf8(b.data()));}
static void save_state(const json &state){Key k(L"SOFTWARE\\LiDE25OpenDriver\\Recovery",true);wincheck(k.h!=nullptr,"Open protected recovery state");str(k,L"State",wide(state.dump()));write_json(app/L"setup-state.json",state);}
struct Service {SC_HANDLE scm=nullptr,svc=nullptr;Service(){scm=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);wincheck(scm!=nullptr,"Open service manager");svc=OpenServiceW(scm,L"stisvc",SERVICE_QUERY_STATUS|SERVICE_START|SERVICE_STOP);wincheck(svc!=nullptr,"Open Windows Image Acquisition service");}~Service(){if(svc)CloseServiceHandle(svc);if(scm)CloseServiceHandle(scm);}
 void wait(DWORD state){ULONGLONG end=GetTickCount64()+30000;SERVICE_STATUS s={};do{wincheck(QueryServiceStatus(svc,&s),"Query WIA service");if(s.dwCurrentState==state)return;Sleep(200);}while(GetTickCount64()<end);throw std::runtime_error("Windows Image Acquisition service did not reach the requested state.");}
 void stop(){SERVICE_STATUS s={};wincheck(QueryServiceStatus(svc,&s),"Query WIA service");if(s.dwCurrentState==SERVICE_STOPPED)return;if(s.dwCurrentState!=SERVICE_STOP_PENDING)wincheck(ControlService(svc,SERVICE_CONTROL_STOP,&s),"Stop WIA service (finish other scans first)");wait(SERVICE_STOPPED);}
 void start(){SERVICE_STATUS s={};wincheck(QueryServiceStatus(svc,&s),"Query WIA service");if(s.dwCurrentState==SERVICE_RUNNING)return;if(s.dwCurrentState!=SERVICE_START_PENDING)wincheck(StartServiceW(svc,0,nullptr),"Start Windows Image Acquisition service");wait(SERVICE_RUNNING);}
};
static void platform(bool admin){SYSTEM_INFO s={};GetNativeSystemInfo(&s);if(s.wProcessorArchitecture!=PROCESSOR_ARCHITECTURE_AMD64)throw std::runtime_error("This installer requires an Intel/AMD x64 Windows system.");
 typedef LONG(WINAPI *RtlVersion)(OSVERSIONINFOW*);auto rtl=(RtlVersion)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion");OSVERSIONINFOW v={};v.dwOSVersionInfoSize=sizeof(v);if(!rtl||rtl(&v)!=0||v.dwMajorVersion!=10||v.dwBuildNumber>=22000)throw std::runtime_error("Only Windows 10 x64 is supported; Windows 11 is not supported.");
 if(admin){SID_IDENTIFIER_AUTHORITY nt=SECURITY_NT_AUTHORITY;PSID sid=nullptr;BOOL yes=FALSE;wincheck(AllocateAndInitializeSid(&nt,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&sid),"Check administrator access");CheckTokenMembership(nullptr,sid,&yes);FreeSid(sid);if(!yes)throw std::runtime_error("Run setup with administrator approval.");}}
static std::string hash_file(const fs::path &p){BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;DWORD n=0,bytes=0;std::vector<BYTE> object,digest(32);if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)!=0)throw std::runtime_error("SHA256 provider unavailable.");
 BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(BYTE*)&bytes,sizeof(bytes),&n,0);object.resize(bytes);if(BCryptCreateHash(alg,&hash,object.data(),bytes,nullptr,0,0)!=0){BCryptCloseAlgorithmProvider(alg,0);throw std::runtime_error("Could not initialize SHA256.");}
 std::ifstream f(p,std::ios::binary);if(!f){BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);throw std::runtime_error("Driver DLL is missing.");}char b[32768];while(f){f.read(b,sizeof(b));if(f.gcount())BCryptHashData(hash,(BYTE*)b,(ULONG)f.gcount(),0);}BCryptFinishHash(hash,digest.data(),32,0);BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);static const char hex[]="0123456789abcdef";std::string r;for(BYTE x:digest){r+=hex[x>>4];r+=hex[x&15];}return r;}
static void verify_dll(){if(hash_file(app/L"Lide25Wia.dll")!=L25_DRIVER_SHA256)throw std::runtime_error("The driver file does not match this installer. Re-download the complete offline installer.");
 if(hash_file(app/L"Lide25Twain32.ds")!=L25_TWAIN32_SHA256||hash_file(app/L"Lide25Twain64.ds")!=L25_TWAIN64_SHA256)throw std::runtime_error("A TWAIN component failed its integrity check. Re-download the complete installer.");
 if(hash_file(app/L"TwainDsm32.dll")!=L25_DSM32_SHA256||hash_file(app/L"TwainDsm64.dll")!=L25_DSM64_SHA256)throw std::runtime_error("A TWAIN manager component failed its integrity check.");
 HMODULE dll=LoadLibraryExW((app/L"Lide25Wia.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);wincheck(dll!=nullptr,"Load driver DLL");bool ok=GetProcAddress(dll,"DllGetClassObject")&&GetProcAddress(dll,"DllCanUnloadNow");FreeLibrary(dll);if(!ok)throw std::runtime_error("The driver COM entry points are missing.");}
static fs::path twain_destination(int bits){return fs::path(winpath())/(bits==32?L"twain_32":L"twain_64")/L"L25Open"/L"Lide25.ds";}
static fs::path manager_destination(int bits){wchar_t folder[MAX_PATH];UINT n=bits==32?GetSystemWow64DirectoryW(folder,MAX_PATH):GetSystemDirectoryW(folder,MAX_PATH);wincheck(n>0&&n<MAX_PATH,"Locate Windows TWAIN runtime directory");return fs::path(folder)/L"TWAINDSM.dll";}
static void install_managers(json &state){
 for(int bits:{32,64}){auto dest=manager_destination(bits);if(fs::exists(dest))continue;
  auto source=app/(bits==32?L"TwainDsm32.dll":L"TwainDsm64.dll");
  BOOL ok=CopyFileW(source.c_str(),dest.c_str(),TRUE);DWORD error=GetLastError();
  if(!ok&&(error==ERROR_FILE_EXISTS||error==ERROR_ALREADY_EXISTS))continue;
  SetLastError(error);wincheck(ok,"Install missing shared TWAIN manager");
  state["shared_twain_managers_added"][std::to_string(bits)]=bits==32?L25_DSM32_SHA256:L25_DSM64_SHA256;save_state(state);
 }
 // Shared managers are retained on normal uninstall, as other scanners and
 // applications may start using them. Existing managers are never replaced.
}
static void install_twain(json &state){
 if(!state.contains("twain_files"))state["twain_files"]=json::object();
 for(int bits:{32,64}){
  std::string key=std::to_string(bits),expected=bits==32?L25_TWAIN32_SHA256:L25_TWAIN64_SHA256;auto dest=twain_destination(bits);
  json allowed=state["twain_files"].value(key,json::array());
  if(fs::exists(dest)){auto old=hash_file(dest);if(old!=expected&&std::find(allowed.begin(),allowed.end(),old)==allowed.end())throw std::runtime_error("A different file occupies the LiDE25 TWAIN installation folder. No existing file was overwritten.");}
  if(std::find(allowed.begin(),allowed.end(),expected)==allowed.end())allowed.push_back(expected);
  state["twain_files"][key]=allowed;save_state(state);fs::create_directories(dest.parent_path());
  if(fs::exists(dest)&&hash_file(dest)==expected)continue;
  auto tmp=dest;tmp+=L".new";auto source=app/(bits==32?L"Lide25Twain32.ds":L"Lide25Twain64.ds");
  wincheck(CopyFileW(source.c_str(),tmp.c_str(),FALSE),"Copy TWAIN data source");
  BOOL ok=MoveFileExW(tmp.c_str(),dest.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);DWORD error=GetLastError();if(!ok)DeleteFileW(tmp.c_str());SetLastError(error);wincheck(ok,"Install TWAIN data source (close scanning applications first)");
 }
 note("Installed both 32-bit and 64-bit TWAIN sources automatically.");
}
static void remove_twain(const json &state){
 if(!state.contains("twain_files"))return;
 for(int bits:{32,64}){auto key=std::to_string(bits);if(!state["twain_files"].contains(key))continue;auto dest=twain_destination(bits);
  if(fs::exists(dest)){auto digest=hash_file(dest);const auto &allowed=state["twain_files"][key];if(std::find(allowed.begin(),allowed.end(),digest)==allowed.end())throw std::runtime_error("Another file now owns the TWAIN source path; refusing to remove it.");wincheck(DeleteFileW(dest.c_str()),"Remove TWAIN source (close scanning applications first)");}
  RemoveDirectoryW(dest.parent_path().c_str());
 }
}
static void verify_wia(Device &d){HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(init))throw std::runtime_error("COM initialization failed.");IWiaDevMgr2 *mgr=nullptr;IWiaItem2 *root=nullptr;IEnumWiaItem2 *items=nullptr;IWiaItem2 *item=nullptr;
 HRESULT hr=CoCreateInstance(CLSID_WiaDevMgr2,nullptr,CLSCTX_LOCAL_SERVER,IID_IWiaDevMgr2,(void**)&mgr);BSTR id=nullptr;const char *step="create WIA manager";
 if(SUCCEEDED(hr)){step="enumerate WIA device identifiers";IEnumWIA_DEV_INFO *devices=nullptr;hr=mgr->EnumDeviceInfo(WIA_DEVINFO_ENUM_LOCAL,&devices);if(SUCCEEDED(hr)){IWiaPropertyStorage *props=nullptr;auto expected=d.property(SPDRP_DRIVER);while(devices->Next(1,&props,nullptr)==S_OK){PROPSPEC spec={};spec.ulKind=PRSPEC_PROPID;spec.propid=WIA_DIP_DEV_ID;PROPVARIANT value={};HRESULT read=props->ReadMultiple(1,&spec,&value);props->Release();props=nullptr;if(SUCCEEDED(read)&&value.vt==VT_BSTR&&!_wcsicmp(value.bstrVal,expected.c_str()))id=SysAllocString(value.bstrVal);PropVariantClear(&value);if(id)break;}devices->Release();if(!id)hr=WIA_ERROR_OFFLINE;}}
 if(SUCCEEDED(hr)){step="connect WIA device";hr=mgr->CreateDevice(0,id,&root);}if(SUCCEEDED(hr)){step="enumerate scan items";hr=root->EnumChildItems(nullptr,&items);}if(SUCCEEDED(hr)){step="get flatbed item";hr=items->Next(1,&item,nullptr);}if(hr==S_FALSE)hr=E_UNEXPECTED;
 if(item)item->Release();if(items)items->Release();if(root)root->Release();if(mgr)mgr->Release();SysFreeString(id);CoUninitialize();if(FAILED(hr)){char b[160];std::snprintf(b,sizeof(b),"WIA verification failed at %s (0x%08lx).",step,(unsigned long)hr);throw std::runtime_error(b);}}
static void register_wia(Device &d){auto path=d.class_path();if(path.empty()||d.dev.ClassGuid!=l25_image_class)throw std::runtime_error("Windows did not create an Image class instance for the scanner.");Key k(path,true);wincheck(k.h!=nullptr,"Open scanner driver registration");str(k,L"USDClass",clsid);number(k,L"Capabilities",0x10);str(k,L"SubClass",L"StillImage");number(k,L"DeviceType",1);number(k,L"DeviceSubType",1);str(k,L"FriendlyName",L"CanoScan LiDE 25 (Open WIA)");str(k,L"Vendor",L"Canon Inc.");number(k,L"IsPnP",1);str(k,L"DeviceID",wia_device_id(d));if(reg_string(path,L"CreateFileName").empty())str(k,L"CreateFileName",L"AUTO");
 Key com(com_path,true);wincheck(com.h!=nullptr,"Register native WIA COM class");str(com,L"",runtime_dll().wstring());str(com,L"ThreadingModel",L"Both");}
static json capture(Device &d){json j={{"schema",1},{"instance",utf8(d.instance())},{"original_inf",utf8(d.inf())},{"original_section",utf8(d.section())},{"original_class_path",utf8(d.class_path())},{"original_values",snapshot(d.class_path(),names)},{"original_com",snapshot(com_path,{L"",L"ThreadingModel"})},{"install_directory",utf8(app.wstring())},{"stage","backed_up"}};return j;}
static void validate_state(const json &state){if(state.at("schema")!=1||wide(state.at("install_directory"))!=app.wstring())throw std::runtime_error("The recovery state belongs to a different installation directory.");auto id=wide(state.at("instance"));if(_wcsnicmp(id.c_str(),L"USB\\VID_04A9&PID_2220\\",22))throw std::runtime_error("Recovery state is not a LiDE 25 device.");}
static void restore(Device &d,json &state){validate_state(state);if(utf8(d.instance())!=state.at("instance"))throw std::runtime_error("Connect the same LiDE 25 that was installed by this setup.");
 remove_twain(state);
 auto inf=wide(state.at("original_inf")),section=wide(state.at("original_section"));if(!inf.empty()){if(inf.find_first_of(L"/\\:")!=std::wstring::npos||section.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._")!=std::wstring::npos)throw std::runtime_error("Original driver identity is invalid.");
  note("Restoring the original driver binding.");try{d.bind(winpath()+L"\\INF\\"+inf,section);}catch(const std::exception &e){note(std::string("Explicit selection recovery: ")+e.what());if(!_wcsicmp(inf.c_str(),L"sti.inf"))throw;restore_matching_driver(winpath()+L"\\INF\\"+inf);}Service service;service.stop();Device current;auto values=state.at("original_values");auto restore_names=names;if(!values.contains("CreateFileName"))restore_names.pop_back();restore_values(current.class_path(),values,restore_names);if(current.dev.ClassGuid==l25_image_class&&utf8(current.class_path())!=state.at("original_class_path")){Key k(current.class_path(),true);str(k,L"DeviceID",wia_device_id(current));}
 }else{
  note("Removing the USB binding created by this installation.");d.clear_binding_for_test();
 }
 bool existed=false;for(auto &v:state.at("original_com").items())if(v.value().at("exists").get<bool>())existed=true;
 if(existed)restore_values(com_path,state.at("original_com"),{L"",L"ThreadingModel"});else{LONG r=RegDeleteTreeW(HKEY_LOCAL_MACHINE,(std::wstring(L"SOFTWARE\\Classes\\CLSID\\")+clsid).c_str());if(r!=ERROR_SUCCESS&&r!=ERROR_FILE_NOT_FOUND)throw std::runtime_error("Could not remove the installed COM registration.");}
 state["stage"]="restored";save_state(state);note("Original configuration restored.");}
static int install(bool simulate_clean,bool prepare_only=false){platform(true);verify_dll();Device before;fs::path state_file=app/L"setup-state.json";json state;
 auto existing_directory=reg_string(L"SOFTWARE\\LiDE25OpenDriver",L"InstallDir");if(!existing_directory.empty()&&_wcsicmp(existing_directory.c_str(),app.c_str()))throw std::runtime_error("A LiDE 25 installation already exists in another folder. Use that installation directory.");
 if(has_state()){state=load_state();if(state.at("stage")!="restored")validate_state(state);if(state.at("stage")=="restored")state=capture(before);else if(state.at("instance")!=utf8(before.instance()))throw std::runtime_error("This installation directory belongs to another scanner.");}
 else state=capture(before);
 save_state(state);note("Original scanner settings backed up.");
 fs::create_directories(runtime_dll().parent_path());if(!fs::exists(runtime_dll()))fs::copy_file(app/L"Lide25Wia.dll",runtime_dll());if(hash_file(runtime_dll())!=L25_DRIVER_SHA256)throw std::runtime_error("The installed runtime DLL failed its integrity check.");
 Service svc;svc.stop();try{
  if(simulate_clean){note("TEST: installing a null driver to reproduce a scanner without a driver binding.");before.clear_binding_for_test();Device bare;state["test_unbound_service"]=utf8(bare.property(SPDRP_SERVICE));state["test_unbound_inf"]=utf8(bare.inf());save_state(state);}
  if(prepare_only){state["stage"]="test_unbound";save_state(state);svc.start();note("TEST: unbound device prepared; original settings remain recoverable.");return 0;}
  Device d;if(_wcsicmp(d.property(SPDRP_SERVICE).c_str(),L"usbscan")){note("Installing the Windows inbox USB scanning component (offline).");bool reboot=d.bind(winpath()+L"\\INF\\sti.inf",L"STI.USBSection");state["binding_changed"]=true;state["reboot_required"]=reboot;state["stage"]="usb_bound";save_state(state);}
  // PnP installation may automatically start stisvc. Stop it again so its
  // device cache cannot retain the temporary inbox/Canon registration.
  svc.stop();Device active;note("Registering the LiDE 25 WIA driver with WIA service stopped.");register_wia(active);state["installed_class_path"]=utf8(active.class_path());state["installed_inf"]=utf8(active.inf());state["installed_service"]=utf8(active.property(SPDRP_SERVICE));state["driver_sha256"]=L25_DRIVER_SHA256;state["stage"]="registered";save_state(state);
  L25Usb usb;if(!l25_usb_open(&usb,nullptr))throw std::runtime_error("USB scanner communication failed (Windows error "+std::to_string(usb.last_error)+"). Reconnect the USB cable, then retry.");l25_usb_close(&usb);
  install_managers(state);install_twain(state);svc.start();note("Verifying the scanner through the Windows WIA service.");verify_wia(active);state["stage"]="installed";state["installed_dll"]=utf8(runtime_dll().wstring());save_state(state);result_message(L"安装已完成。");note("Offline installation completed. Reopen your application and select CanoScan LiDE 25 using WIA or TWAIN.");return state.value("reboot_required",false)?3010:0;
 }catch(...){auto error=std::current_exception();note("Installation failed; restoring the saved configuration.");try{Device d;restore(d,state);svc.start();}catch(const std::exception &e){note(std::string("Recovery needs attention: ")+e.what());try{svc.start();}catch(...) {}}std::rethrow_exception(error);}}
static int uninstall(){platform(true);auto state=load_state();validate_state(state);if(state.at("stage")=="restored"){note("Already restored.");return 0;}
 auto owner=reg_string(com_path,L"");auto expected=state.value("installed_dll",utf8(runtime_dll().wstring()));if(!owner.empty()&&_wcsicmp(owner.c_str(),wide(expected).c_str()))throw std::runtime_error("Another installation now owns this WIA driver; refusing to overwrite it.");Device d;Service svc;svc.stop();try{restore(d,state);svc.start();}catch(...){try{svc.start();}catch(...){}throw;}result_message(L"卸载已完成，原设置已经恢复。");return 0;}
static int self_test(){platform(false);verify_dll();json valid={{"schema",1},{"instance","USB\\VID_04A9&PID_2220\\TEST"},{"install_directory",utf8(app.wstring())}};validate_state(valid);int refused=0;
 for(int i=0;i<3;i++){auto bad=valid;if(i==0)bad["schema"]=99;if(i==1)bad["instance"]="USB\\VID_0000&PID_0000\\OTHER";if(i==2)bad["install_directory"]=utf8((app/L"another-folder").wstring());try{validate_state(bad);}catch(...){refused++;}}
 if(refused!=3)throw std::runtime_error("Recovery scope guard self-test failed.");std::puts("{\"passed\":true,\"dll_integrity\":true,\"native_dll_load\":true,\"recovery_scope_guards\":3}");return 0;}
int wmain(int argc,wchar_t **argv){wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);app=fs::path(path).parent_path();try{
 if(argc!=2)throw std::runtime_error("Expected --inspect, --install or --uninstall.");std::wstring cmd=argv[1];
 if(cmd==L"--self-test")return self_test();
 if(cmd==L"--wia-check"){platform(false);Device d;verify_wia(d);std::puts("{\"passed\":true,\"wia_connection\":true}");return 0;}
 if(cmd==L"--state-summary"){auto s=load_state();validate_state(s);json j;for(auto name:{"stage","test_unbound_inf","test_unbound_service","installed_inf","installed_service"})if(s.contains(name))j[name]=s[name];std::puts(j.dump().c_str());return 0;}
 if(cmd==L"--binding-check"){platform(false);Device d;d.bind(winpath()+L"\\INF\\sti.inf",L"STI.USBSection",true);std::puts("{\"passed\":true,\"device_driver_list_inherited\":true,\"installation_attempted\":false}");return 0;}
 if(cmd==L"--inspect"){platform(false);Device d;json j={{"instance",utf8(d.instance())},{"service",utf8(d.property(SPDRP_SERVICE))},{"inf",utf8(d.inf())},{"class_path",utf8(d.class_path())},{"section",utf8(d.section())},{"driver_sha256",hash_file(app/L"Lide25Wia.dll")}};std::puts(j.dump(2).c_str());return 0;}
 HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Global\\Lide25OfflineSetup");if(!mutex||WaitForSingleObject(mutex,0)!=WAIT_OBJECT_0)throw std::runtime_error("Another LiDE 25 installer is already running.");
 try{auto previous=read_json(app/L"setup-log.json");if(previous.is_array())log_events=std::move(previous);}catch(...){}
 note("Starting action: "+utf8(cmd));
 int result;if(cmd==L"--install")result=install(false);else if(cmd==L"--test-clean-install")result=install(true);else if(cmd==L"--test-prepare-unbound")result=install(true,true);else if(cmd==L"--uninstall")result=uninstall();else throw std::runtime_error("Unknown setup action.");ReleaseMutex(mutex);CloseHandle(mutex);return result;
 }catch(const std::exception &e){note(std::string("ERROR: ")+e.what());result_message(wide(e.what()));try{write_json(app/L"setup-error.json",{{"error",e.what()},{"log","setup-log.json"}});}catch(...){}return 1;}catch(...){note("ERROR: Unexpected setup failure.");result_message(L"安装出现意外错误，请保留安装文件夹中的恢复记录。");return 1;}}
