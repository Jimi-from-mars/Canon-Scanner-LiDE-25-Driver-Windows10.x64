/* Internal hardware verification only; the driver does not require this utility. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sane/sane.h"
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
static int set_option(SANE_Handle h,const char *name,const void *value) {
    for(int i=1;i<150;i++) {const SANE_Option_Descriptor *d=sane_plustek_get_option_descriptor(h,i);if(!d)break;if(d->name && !strcmp(d->name,name)) {int info=0;int s=sane_plustek_control_option(h,i,SANE_ACTION_SET_VALUE,(void*)value,&info);fprintf(stderr,"set %s: %s\n",name,sane_strstatus(s));return s;}}
    return SANE_STATUS_INVAL;
}
int main(int argc,char **argv) {
    SANE_Int version=0;SANE_Handle h=NULL;const SANE_Device **devices=NULL;
    SANE_Status s=sane_plustek_init(&version,NULL);fprintf(stderr,"initialize: %s\n",sane_strstatus(s));if(s)return s;
    s=sane_plustek_get_devices(&devices,0);
    if(s || !devices || !devices[0]) {fprintf(stderr,"No supported device\n");sane_plustek_exit();return 10;}
    fprintf(stderr,"device: %s | %s %s\n",devices[0]->name,devices[0]->vendor,devices[0]->model);
    s=sane_plustek_open(devices[0]->name,&h);if(s){fprintf(stderr,"open: %s\n",sane_strstatus(s));sane_plustek_exit();return s;}
    if(argc<2 || !strcmp(argv[1],"--list")) {
        for(int i=0;i<150;i++){const SANE_Option_Descriptor *d=sane_plustek_get_option_descriptor(h,i);if(!d)break;printf("%d %s type=%d cap=%d\n",i,d->name?d->name:"",d->type,d->cap);}
    } else {
        int dpi=argc>2?atoi(argv[2]):150;SANE_Word v=dpi;
        if(set_option(h,"mode","Color") || set_option(h,"resolution",&v))return 20;
        v=SANE_FIX(0);set_option(h,"tl-x",&v);set_option(h,"tl-y",&v);
        v=SANE_FIX(argc>3?atof(argv[3]):30.0);set_option(h,"br-x",&v);
        v=SANE_FIX(argc>4?atof(argv[4]):30.0);set_option(h,"br-y",&v);
        SANE_Parameters p;s=sane_plustek_get_parameters(h,&p);
        fprintf(stderr,"scan: %d x %d, bytes/line=%d depth=%d\n",p.pixels_per_line,p.lines,p.bytes_per_line,p.depth);
        if(s) return s;
        s=sane_plustek_start(h);fprintf(stderr,"start: %s\n",sane_strstatus(s));
        if(!s) {
            FILE *f=fopen(argv[1],"wb");if(!f)return 21;
            fprintf(f,"P6\n%d %d\n255\n",p.pixels_per_line,p.lines);
            unsigned char data[32768];size_t total=0;
            do {int n=0;s=sane_plustek_read(h,data,sizeof(data),&n);if(n>0){fwrite(data,1,n,f);total+=n;}} while(s==SANE_STATUS_GOOD);
            fclose(f);fprintf(stderr,"end: %s, bytes=%llu expected=%llu\n",sane_strstatus(s),(unsigned long long)total,(unsigned long long)p.bytes_per_line*p.lines);
            if(s==SANE_STATUS_EOF && total==(size_t)p.bytes_per_line*p.lines)s=SANE_STATUS_GOOD;
        }
    }
    sane_plustek_close(h);sane_plustek_exit();return s;
}
