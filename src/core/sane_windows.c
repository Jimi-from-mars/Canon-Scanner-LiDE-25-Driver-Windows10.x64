#include "../../third_party/sane/include/sane/config.h"
#include "../../third_party/sane/include/sane/sane.h"
#include "../../third_party/sane/include/sane/sanei_usb.h"
#include "../../third_party/sane/include/sane/sanei_thread.h"
#include "usbscan_transport.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <process.h>

/* Cooperative Win32 worker and an in-memory bounded pipe replace POSIX signals/fork. */
typedef struct {CRITICAL_SECTION cs;CONDITION_VARIABLE readable,writable;unsigned char data[65536];size_t start,size;int reader_open,writer_open;} L25Pipe;
static L25Pipe channel;
static int (*pulse_hook)(void*)=NULL;static void *pulse_context=NULL;
extern void l25_backend_cancel(void);
void l25_set_pulse(int (*hook)(void*),void *ctx) {pulse_hook=hook;pulse_context=ctx;}
static INIT_ONCE channel_once=INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK init_channel(PINIT_ONCE once,void *arg,void **ctx) {(void)once;(void)arg;(void)ctx;InitializeCriticalSection(&channel.cs);InitializeConditionVariable(&channel.readable);InitializeConditionVariable(&channel.writable);return TRUE;}
int l25_pipe(int fd[2]) {InitOnceExecuteOnce(&channel_once,init_channel,NULL,NULL);EnterCriticalSection(&channel.cs);channel.start=channel.size=0;channel.reader_open=channel.writer_open=1;LeaveCriticalSection(&channel.cs);fd[0]=10000;fd[1]=10001;return 0;}
int l25_pipe_read(int fd,void *buffer,unsigned count) {
    if(fd!=10000) return _read(fd,buffer,count);
    EnterCriticalSection(&channel.cs);
    while(!channel.size && channel.writer_open && channel.reader_open) {
        SleepConditionVariableCS(&channel.readable,&channel.cs,250);
        if(!channel.size && pulse_hook) {LeaveCriticalSection(&channel.cs);int cancel=pulse_hook(pulse_context);if(cancel)l25_backend_cancel();EnterCriticalSection(&channel.cs);}
    }
    unsigned n=(unsigned)(channel.size<count?channel.size:count);
    for(unsigned i=0;i<n;i++) ((unsigned char*)buffer)[i]=channel.data[(channel.start+i)%sizeof(channel.data)];
    channel.start=(channel.start+n)%sizeof(channel.data);channel.size-=n;WakeAllConditionVariable(&channel.writable);LeaveCriticalSection(&channel.cs);return n;
}
int l25_pipe_write(int fd,const void *buffer,unsigned count) {
    if(fd!=10001) return _write(fd,buffer,count);
    EnterCriticalSection(&channel.cs);unsigned sent=0;
    while(sent<count && channel.reader_open && channel.writer_open) {
        while(channel.size==sizeof(channel.data) && channel.reader_open) SleepConditionVariableCS(&channel.writable,&channel.cs,1000);
        if(!channel.reader_open || !channel.writer_open) break;
        size_t room=sizeof(channel.data)-channel.size;unsigned n=(unsigned)(room<count-sent?room:count-sent);
        for(unsigned i=0;i<n;i++) channel.data[(channel.start+channel.size+i)%sizeof(channel.data)]=((const unsigned char*)buffer)[sent+i];
        sent+=n;channel.size+=n;WakeAllConditionVariable(&channel.readable);
    }
    LeaveCriticalSection(&channel.cs);if(!sent && count){errno=EPIPE;return -1;}return sent;
}
int l25_pipe_close(int fd) {
    if(fd!=10000 && fd!=10001) return _close(fd);
    EnterCriticalSection(&channel.cs);if(fd==10000) channel.reader_open=0;else channel.writer_open=0;WakeAllConditionVariable(&channel.readable);WakeAllConditionVariable(&channel.writable);LeaveCriticalSection(&channel.cs);return 0;
}
int l25_fcntl(int fd,int command,...) {(void)fd;(void)command;return -1;}
void l25_abort_channel(void) {InitOnceExecuteOnce(&channel_once,init_channel,NULL,NULL);l25_pipe_close(10000);}

typedef struct {int (*func)(void*);void *arg;} Worker;
static unsigned __stdcall worker_main(void *p) {Worker w=*(Worker*)p;free(p);return (unsigned)w.func(w.arg);}
void sanei_thread_init(void) {}
SANE_Bool sanei_thread_is_forked(void) {return SANE_FALSE;}
SANE_Bool sanei_thread_is_valid(SANE_Pid id) {return id!=(SANE_Pid)-1 && id!=0;}
SANE_Pid sanei_thread_begin(int (*func)(void*),void *arg) {Worker *w=malloc(sizeof(*w));if(!w)return (SANE_Pid)-1;w->func=func;w->arg=arg;uintptr_t h=_beginthreadex(NULL,0,worker_main,w,0,NULL);if(!h){free(w);return (SANE_Pid)-1;}return (SANE_Pid)h;}
int sanei_thread_sendsig(SANE_Pid id,int sig) {(void)id;(void)sig;l25_abort_channel();return 0;}
int sanei_thread_kill(SANE_Pid id) {(void)id;l25_abort_channel();return 0;}
/* Never free scanner state while the worker still owns it. USB I/O is timed out
   separately; cancellation remains cooperative rather than terminating a thread. */
static DWORD join_worker(SANE_Pid id) {DWORD wait=WaitForSingleObject((HANDLE)id,60000);if(wait==WAIT_TIMEOUT){l25_backend_cancel();wait=WaitForSingleObject((HANDLE)id,INFINITE);}return wait;}
SANE_Pid sanei_thread_waitpid(SANE_Pid id,int *status) {if(join_worker(id)!=WAIT_OBJECT_0)return (SANE_Pid)-1;DWORD code;GetExitCodeThread((HANDLE)id,&code);if(status)*status=(int)code;CloseHandle((HANDLE)id);return id;}
SANE_Status sanei_thread_get_status(SANE_Pid id) {DWORD code;if(join_worker(id)!=WAIT_OBJECT_0 || !GetExitCodeThread((HANDLE)id,&code))return SANE_STATUS_IO_ERROR;CloseHandle((HANDLE)id);return (SANE_Status)code;}

static L25Usb ports[8];static int initialized=0;static DWORD io_timeout=15000;
void sanei_usb_init(void) {if(!initialized){for(int i=0;i<8;i++)ports[i].handle=INVALID_HANDLE_VALUE;initialized=1;}}
void sanei_usb_exit(void) {for(int i=0;i<8;i++)l25_usb_close(&ports[i]);}
SANE_Status sanei_usb_open(SANE_String_Const name,SANE_Int *fd) {(void)name;sanei_usb_init();for(int i=0;i<8;i++)if(ports[i].handle==INVALID_HANDLE_VALUE){if(!l25_usb_open(&ports[i],NULL))return SANE_STATUS_IO_ERROR;*fd=i;return SANE_STATUS_GOOD;}return SANE_STATUS_DEVICE_BUSY;}
void sanei_usb_close(SANE_Int fd) {if(fd>=0 && fd<8)l25_usb_close(&ports[fd]);}
SANE_Status sanei_usb_get_vendor_product(SANE_Int fd,SANE_Word *vendor,SANE_Word *product) {if(fd<0 || fd>=8 || ports[fd].handle==INVALID_HANDLE_VALUE)return SANE_STATUS_INVAL;*vendor=0x04a9;*product=0x2220;return SANE_STATUS_GOOD;}
SANE_Status sanei_usb_find_devices(SANE_Int vendor,SANE_Int product,SANE_Status (*attach)(SANE_String_Const)) {if((vendor==0 || vendor==0x04a9)&&(product==0 || product==0x2220))return attach("usbscan:04a9:2220");return SANE_STATUS_GOOD;}
SANE_Status sanei_usb_write_bulk(SANE_Int fd,const SANE_Byte *data,size_t *size) {if(fd<0||fd>=8)return SANE_STATUS_INVAL;size_t expected=*size;if(getenv("LIDE25_USB_TRACE"))fprintf(stderr,"USB OUT %llu [%02x %02x %02x %02x]\n",(unsigned long long)*size,data[0],*size>1?data[1]:0,*size>2?data[2]:0,*size>3?data[3]:0);int ok=l25_usb_write(&ports[fd],data,size,io_timeout);if(!ok)fprintf(stderr,"USB write failed: requested=%llu actual=%llu error=%lu\n",(unsigned long long)expected,(unsigned long long)*size,ports[fd].last_error);return ok && *size?SANE_STATUS_GOOD:SANE_STATUS_IO_ERROR;}
SANE_Status sanei_usb_read_bulk(SANE_Int fd,SANE_Byte *data,size_t *size) {if(fd<0||fd>=8)return SANE_STATUS_INVAL;size_t expected=*size;int ok=l25_usb_read(&ports[fd],data,size,io_timeout);if(!ok)fprintf(stderr,"USB read failed: requested=%llu actual=%llu error=%lu\n",(unsigned long long)expected,(unsigned long long)*size,ports[fd].last_error);return ok && *size?SANE_STATUS_GOOD:SANE_STATUS_IO_ERROR;}
void sanei_usb_set_timeout(SANE_Int timeout) {io_timeout=(DWORD)timeout;}
void sanei_access_init(const char *backend) {(void)backend;}
static HANDLE device_mutex=NULL;
SANE_Status sanei_access_lock(const char *name,SANE_Word timeout) {(void)name;if(!device_mutex)device_mutex=CreateMutexW(NULL,FALSE,L"Global\\Lide25OpenDriver_04A9_2220");if(!device_mutex)return SANE_STATUS_ACCESS_DENIED;DWORD r=WaitForSingleObject(device_mutex,(DWORD)timeout*1000);return r==WAIT_OBJECT_0||r==WAIT_ABANDONED?SANE_STATUS_GOOD:SANE_STATUS_DEVICE_BUSY;}
SANE_Status sanei_access_unlock(const char *name) {(void)name;if(device_mutex)ReleaseMutex(device_mutex);return SANE_STATUS_GOOD;}
void sanei_debug_ndebug(int level,const char *msg,...) {(void)level;(void)msg;}
void sanei_init_debug(const char *name,int *level) {(void)name;const char *v=getenv("LIDE25_DEBUG");*level=v?atoi(v):0;}
void sanei_debug_msg(int level,int max,const char *name,const char *fmt,va_list ap) {if(level>max)return;fprintf(stderr,"[%s] ",name);vfprintf(stderr,fmt,ap);fflush(stderr);}
const SANE_String_Const sane_strstatus(SANE_Status s) {static const char *names[]={"Success","Unsupported","Cancelled","Busy","Invalid argument","EOF","ADF jam","No document","Cover open","I/O error","No memory","Access denied"};return s>=0&&s<12?names[s]:"Unknown";}
