// Native, model-scoped PnP operations. No vendor INF and no signature-policy changes.
#pragma once
#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>
#include <vector>
#include <string>
#include <stdexcept>
static const GUID l25_image_class={0x6bdd1fc6,0x810f,0x11d0,{0xbe,0xc7,0x08,0,0x2b,0xe2,0x09,0x2f}};
static void wincheck(BOOL ok,const char *operation){if(!ok)throw std::runtime_error(std::string(operation)+" (Windows error "+std::to_string(GetLastError())+")");}
static std::wstring winpath(){wchar_t p[MAX_PATH];wincheck(GetWindowsDirectoryW(p,MAX_PATH)!=0,"GetWindowsDirectory");return p;}
static BOOL install_device(HDEVINFO set,SP_DEVINFO_DATA *dev,SP_DRVINFO_DATA_W *driver,DWORD flags,BOOL *reboot){
 HMODULE lib=LoadLibraryExW(L"newdev.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);wincheck(lib!=nullptr,"Load Windows device installer");
 auto fn=(BOOL(WINAPI*)(HWND,HDEVINFO,SP_DEVINFO_DATA*,SP_DRVINFO_DATA_W*,DWORD,BOOL*))GetProcAddress(lib,"DiInstallDevice");if(!fn){FreeLibrary(lib);throw std::runtime_error("Windows DiInstallDevice is unavailable.");}
 BOOL ok=fn(nullptr,set,dev,driver,flags,reboot);DWORD e=GetLastError();FreeLibrary(lib);SetLastError(e);return ok;
}
static bool restore_matching_driver(const std::wstring &published_inf){
 // A second standard Windows API is reserved for restoring a previously
 // installed, hardware-ID-matching package if explicit driver selection fails.
 wchar_t repository[32768]={};DWORD required=0;
 auto location=(BOOL(WINAPI*)(PCWSTR,void*,PCWSTR,PWSTR,DWORD,PDWORD))GetProcAddress(GetModuleHandleW(L"setupapi.dll"),"SetupGetInfDriverStoreLocationW");
 if(!location||!location(published_inf.c_str(),nullptr,nullptr,repository,32768,&required))throw std::runtime_error("The original signed driver package could not be located in the driver store.");
 HMODULE lib=LoadLibraryExW(L"newdev.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);wincheck(lib!=nullptr,"Load driver recovery API");
 auto fn=(BOOL(WINAPI*)(HWND,LPCWSTR,LPCWSTR,DWORD,PBOOL))GetProcAddress(lib,"UpdateDriverForPlugAndPlayDevicesW");if(!fn){FreeLibrary(lib);throw std::runtime_error("The standard driver recovery API is unavailable.");}
 BOOL reboot=FALSE;BOOL ok=fn(nullptr,L"USB\\VID_04A9&PID_2220",repository,INSTALLFLAG_FORCE,&reboot);DWORD error=GetLastError();FreeLibrary(lib);SetLastError(error);wincheck(ok,"Restore the original matching driver package");return reboot!=FALSE;
}
struct Device {
 HDEVINFO set=INVALID_HANDLE_VALUE;SP_DEVINFO_DATA dev={};
 Device(){set=SetupDiGetClassDevsW(nullptr,nullptr,nullptr,DIGCF_ALLCLASSES|DIGCF_PRESENT);wincheck(set!=INVALID_HANDLE_VALUE,"Enumerate devices");dev.cbSize=sizeof(dev);int found=0;
  for(DWORD i=0;SetupDiEnumDeviceInfo(set,i,&dev);++i){wchar_t ids[2048]={};if(!SetupDiGetDeviceRegistryPropertyW(set,&dev,SPDRP_HARDWAREID,nullptr,(BYTE*)ids,sizeof(ids),nullptr))continue;bool match=false;
   for(auto p=ids;*p;p+=wcslen(p)+1)if(!_wcsicmp(p,L"USB\\VID_04A9&PID_2220"))match=true;
   if(match){chosen=dev;found++;}
  }
  if(found!=1){SetupDiDestroyDeviceInfoList(set);set=INVALID_HANDLE_VALUE;throw std::runtime_error(found?"Connect only one LiDE 25 scanner during setup.":"Connect the LiDE 25 scanner by USB, then retry.");}dev=chosen;
 }
 ~Device(){if(set!=INVALID_HANDLE_VALUE)SetupDiDestroyDeviceInfoList(set);}
 SP_DEVINFO_DATA chosen={};
 Device(const Device&)=delete;Device&operator=(const Device&)=delete;
 std::wstring property(DWORD id){wchar_t p[2048]={};if(!SetupDiGetDeviceRegistryPropertyW(set,&dev,id,nullptr,(BYTE*)p,sizeof(p),nullptr))return L"";return p;}
 std::wstring instance(){wchar_t p[MAX_DEVICE_ID_LEN];wincheck(SetupDiGetDeviceInstanceIdW(set,&dev,p,MAX_DEVICE_ID_LEN,nullptr),"Get device instance");return p;}
 std::wstring class_path(){auto p=property(SPDRP_DRIVER);if(p.empty())return L"";return L"SYSTEM\\CurrentControlSet\\Control\\Class\\"+p;}
 std::wstring inf(){HKEY k;auto path=class_path();if(path.empty()||RegOpenKeyExW(HKEY_LOCAL_MACHINE,path.c_str(),0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return L"";wchar_t p[MAX_PATH]={};DWORD n=sizeof(p);RegQueryValueExW(k,L"InfPath",nullptr,nullptr,(BYTE*)p,&n);RegCloseKey(k);return p;}
 std::wstring section(){HKEY k;auto path=class_path();if(path.empty()||RegOpenKeyExW(HKEY_LOCAL_MACHINE,path.c_str(),0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return L"";wchar_t p[MAX_PATH]={};DWORD n=sizeof(p);RegQueryValueExW(k,L"InfSection",nullptr,nullptr,(BYTE*)p,&n);RegCloseKey(k);return p;}
 bool bind(const std::wstring &inf_file,const std::wstring &section_name,bool inspect_only=false){
  // A global list lets Windows select the signed Image class driver even when
  // this scanner currently has no class/driver. DiInstallDevice owns the bind.
  SP_DEVINSTALL_PARAMS_W p={};p.cbSize=sizeof(p);wincheck(SetupDiGetDeviceInstallParamsW(set,nullptr,&p),"Get global install parameters");p.Flags|=DI_ENUMSINGLEINF;p.FlagsEx|=DI_FLAGSEX_ALLOWEXCLUDEDDRVS;
  if(inf_file.size()>=MAX_PATH)throw std::runtime_error("INF path is too long.");wcscpy(p.DriverPath,inf_file.c_str());
  wincheck(SetupDiSetDeviceInstallParamsW(set,nullptr,&p),"Select Windows INF");wincheck(SetupDiBuildDriverInfoList(set,nullptr,SPDIT_CLASSDRIVER),"Build signed driver list");
  SP_DRVINFO_DATA_W d={};d.cbSize=sizeof(d);bool found=false;
  for(DWORD i=0;SetupDiEnumDriverInfoW(set,nullptr,SPDIT_CLASSDRIVER,i,&d);++i){std::vector<BYTE> b(8192);auto detail=(SP_DRVINFO_DETAIL_DATA_W*)b.data();detail->cbSize=sizeof(*detail);
   if(SetupDiGetDriverInfoDetailW(set,nullptr,&d,detail,(DWORD)b.size(),nullptr)&&!_wcsicmp(detail->SectionName,section_name.c_str())){found=true;break;}}
  if(!found){SetupDiDestroyDriverInfoList(set,nullptr,SPDIT_CLASSDRIVER);throw std::runtime_error("Required Windows USB scanning component was not found in the local driver store.");}
  // DiInstallDevice requires a node associated with this device, not a node
  // from the global list. Inherit that list through the documented open flag;
  // this also works when the physical device has no Image class yet.
  wincheck(SetupDiSetSelectedDriverW(set,nullptr,&d),"Select global signed driver");
  auto id=instance();
  wincheck(SetupDiOpenDeviceInfoW(set,id.c_str(),nullptr,DIOD_INHERIT_CLASSDRVS,&dev),"Associate signed driver list with scanner");
  SP_DRVINFO_DATA_W selected={};selected.cbSize=sizeof(selected);
  wincheck(SetupDiGetSelectedDriverW(set,&dev,&selected),"Get inherited device driver");
  if(inspect_only){SetupDiDestroyDriverInfoList(set,&dev,SPDIT_CLASSDRIVER);SetupDiDestroyDriverInfoList(set,nullptr,SPDIT_CLASSDRIVER);return false;}
  wincheck(SetupDiSetSelectedDriverW(set,&dev,&selected),"Select scanner driver and class");
  BOOL reboot=FALSE;BOOL ok=install_device(set,&dev,&selected,0,&reboot);DWORD error=GetLastError();SetupDiDestroyDriverInfoList(set,&dev,SPDIT_CLASSDRIVER);SetupDiDestroyDriverInfoList(set,nullptr,SPDIT_CLASSDRIVER);SetLastError(error);wincheck(ok,"Install the selected signed driver");return reboot!=FALSE;
 }
 bool clear_binding_for_test(){BOOL reboot=FALSE;wincheck(install_device(set,&dev,nullptr,DIIDFLAG_INSTALLNULLDRIVER,&reboot),"Test: remove current binding");return reboot!=FALSE;}
};
