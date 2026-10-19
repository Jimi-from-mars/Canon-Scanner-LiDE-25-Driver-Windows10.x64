#include "lide25_core.h"
#include "../../third_party/sane/include/sane/config.h"
#include "sane/sane.h"
#include <math.h>
#include <stdio.h>
extern SANE_Status sane_plustek_init(SANE_Int*,SANE_Auth_Callback);
extern SANE_Status sane_plustek_get_devices(const SANE_Device***,SANE_Bool);
extern SANE_Status sane_plustek_open(SANE_String_Const,SANE_Handle*);
extern const SANE_Option_Descriptor *sane_plustek_get_option_descriptor(SANE_Handle,SANE_Int);
extern SANE_Status sane_plustek_control_option(SANE_Handle,SANE_Int,SANE_Action,void*,SANE_Int*);
extern SANE_Status sane_plustek_get_parameters(SANE_Handle,SANE_Parameters*);
extern SANE_Status sane_plustek_start(SANE_Handle);
extern SANE_Status sane_plustek_read(SANE_Handle,SANE_Byte*,SANE_Int,SANE_Int*);
extern void sane_plustek_close(SANE_Handle);
extern void sane_plustek_exit(void);
extern void l25_backend_cancel(void);
extern void l25_backend_clear_cancel(void);
extern void l25_set_pulse(int (*hook)(void*),void *ctx);
static volatile LONG acquiring=0,cancelled=0;
typedef struct {const L25Sink *sink;ULONGLONG started;int stage,percent;} Pulse;
static int pulse(void *arg) {Pulse *p=(Pulse*)arg;if(InterlockedCompareExchange(&cancelled,0,0)||GetTickCount64()-p->started>600000)return 1;return p->sink->progress?p->sink->progress(p->sink->context,p->stage,p->percent):0;}
void l25_core_cancel(void) {if(InterlockedCompareExchange(&acquiring,0,0)){InterlockedExchange(&cancelled,1);l25_backend_cancel();}}
const char *l25_core_error(int s) {return sane_strstatus((SANE_Status)s);}
static SANE_Status option(SANE_Handle h,const char *name,double number,const char *string) {
    for(int i=1;i<150;i++) {const SANE_Option_Descriptor *d=sane_plustek_get_option_descriptor(h,i);if(!d)break;
        if(d->name && !strcmp(d->name,name)) {int info=0;SANE_Word n=d->type==SANE_TYPE_FIXED?(!strncmp(name,"br-",3)?(SANE_Word)ceil(number*65536.0):SANE_FIX(number)):(int)number;
            return sane_plustek_control_option(h,i,SANE_ACTION_SET_VALUE,string?(void*)string:&n,&info);}}
    return SANE_STATUS_INVAL;
}
int l25_core_acquire(const L25ScanRequest *r,const L25Sink *sink) {
    if(!r||r->size!=sizeof(*r)||!sink||!sink->begin||!sink->data||r->mode<0||r->mode>2||
       (r->dpi!=75&&r->dpi!=150&&r->dpi!=300&&r->dpi!=600&&r->dpi!=1200)||
       !isfinite(r->left_mm)||!isfinite(r->top_mm)||!isfinite(r->width_mm)||!isfinite(r->height_mm)||
       r->left_mm<0||r->top_mm<0||r->width_mm<=0||r->height_mm<=0||r->left_mm+r->width_mm>216.0||r->top_mm+r->height_mm>297.0||
       r->brightness < -1000 || r->brightness>1000 ||r->contrast < -1000 ||r->contrast>1000)return SANE_STATUS_INVAL;
    if(InterlockedCompareExchange(&acquiring,1,0))return SANE_STATUS_DEVICE_BUSY;
    InterlockedExchange(&cancelled,0);l25_backend_clear_cancel();Pulse p={sink,GetTickCount64(),0,0};l25_set_pulse(pulse,&p);
    int initialized=0;SANE_Handle h=NULL;SANE_Int version=0;const SANE_Device **devices=NULL;SANE_Parameters params={0};
    SANE_Status s=sane_plustek_init(&version,NULL);initialized=1;
    if(s)goto done;
    s=sane_plustek_get_devices(&devices,0);if(s)goto done;if(!devices||!devices[0]){s=SANE_STATUS_INVAL;goto done;}
    s=sane_plustek_open(devices[0]->name,&h);if(s)goto done;
    const char *modes[]={"Color","Gray","Lineart"};
#define SET(name,value,string) do{s=option(h,name,value,string);if(s)goto done;}while(0)
    SET("mode",0,modes[r->mode]);SET("resolution",r->dpi,NULL);
    SET("tl-x",r->left_mm,NULL);SET("tl-y",r->top_mm,NULL);
    SET("br-x",r->left_mm+r->width_mm,NULL);SET("br-y",r->top_mm+r->height_mm,NULL);
    SET("brightness",r->brightness/10.0,NULL);SET("contrast",r->contrast/10.0,NULL);
    SET("lamp-off-at-exit",1,NULL);
#undef SET
    s=sane_plustek_start(h);if(s)goto done;
    s=sane_plustek_get_parameters(h,&params);if(s)goto done;
    if(params.pixels_per_line<=0||params.lines<=0||params.bytes_per_line<=0||
       (uint64_t)params.lines*params.bytes_per_line>512ULL*1024*1024){s=SANE_STATUS_INVAL;goto done;}
    L25ImageInfo info={params.pixels_per_line,params.lines,params.format==SANE_FRAME_RGB?3:1,params.depth,params.bytes_per_line,r->dpi};
    if(sink->begin(sink->context,&info)){s=SANE_STATUS_CANCELLED;goto done;}
    uint64_t total=0,expected=(uint64_t)params.lines*params.bytes_per_line;uint8_t data[32768];
    for(;;){SANE_Int n=0;s=sane_plustek_read(h,data,sizeof(data),&n);
        if(n<0||(uint64_t)n>expected-total){s=SANE_STATUS_IO_ERROR;break;}
        if(n){if(sink->data(sink->context,data,(size_t)n)){s=SANE_STATUS_CANCELLED;break;}total+=n;}
        p.stage=1;p.percent=(int)(total*100/expected);
        if(sink->progress && sink->progress(sink->context,p.stage,p.percent)){s=SANE_STATUS_CANCELLED;break;}
        if(InterlockedCompareExchange(&cancelled,0,0)){s=SANE_STATUS_CANCELLED;break;}
        if(s==SANE_STATUS_EOF){s=total==expected?SANE_STATUS_GOOD:SANE_STATUS_IO_ERROR;break;}
        if(s!=SANE_STATUS_GOOD)break;
    }
done:
    if(s==SANE_STATUS_CANCELLED)l25_backend_cancel();
    if(h)sane_plustek_close(h);if(initialized)sane_plustek_exit();
    l25_set_pulse(NULL,NULL);InterlockedExchange(&acquiring,0);return s;
}
