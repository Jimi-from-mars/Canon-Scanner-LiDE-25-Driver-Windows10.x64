#include "usbscan_transport.h"
#include <stdio.h>
#include <winioctl.h>
#include <usbscan.h>
#include <string.h>
int main(int argc,char **argv) {
    L25Usb usb; if(!l25_usb_open(&usb,NULL)) {printf("{\"opened\":false,\"win32_error\":%lu}\n",usb.last_error);return 1;}
    if(argc>1 && !strcmp(argv[1],"--recover")) {
        PIPE_TYPE kinds[]={READ_DATA_PIPE,WRITE_DATA_PIPE};
        for(unsigned i=0;i<2;i++){int r=l25_usb_ioctl(&usb,IOCTL_RESET_PIPE,&kinds[i],sizeof(kinds[i]),NULL,0,NULL);fprintf(stderr,"reset pipe %d: ok=%d error=%lu\n",kinds[i],r,usb.last_error);}
        unsigned char buffer[65536];
        for(int i=0;i<8;i++){size_t n=sizeof(buffer);int r=l25_usb_read(&usb,buffer,&n,500);fprintf(stderr,"drain: ok=%d bytes=%llu error=%lu\n",r,(unsigned long long)n,usb.last_error);if(!r||!n)break;}
        unsigned char stop[]={0,7,0,1,0};size_t n=sizeof(stop);
        int r=l25_usb_write(&usb,stop,&n,1000);fprintf(stderr,"stop engine: ok=%d error=%lu\n",r,usb.last_error);
    }
    USBSCAN_PIPE_CONFIGURATION pipes={0}; DWORD count=0;
    int ok=l25_usb_ioctl(&usb,IOCTL_GET_PIPE_CONFIGURATION,NULL,0,&pipes,sizeof(pipes),&count);
    printf("{\"opened\":true,\"pipe_query\":%s,\"pipes\":[",ok?"true":"false");
    if(ok) for(ULONG i=0;i<pipes.NumberOfPipes && i<MAX_NUM_PIPES;i++) {
        USBSCAN_PIPE_INFORMATION *p=&pipes.PipeInfo[i];
        printf("%s{\"index\":%lu,\"endpoint\":%u,\"type\":%d,\"max_packet\":%u}",i?",":"",i,p->EndpointAddress,p->PipeType,p->MaximumPacketSize);
    }
    unsigned char cmd[]={1,0x69,0,1},version=0;size_t n=4;
    ok=l25_usb_write(&usb,cmd,&n,3000) && n==4;
    printf("],\"write_ok\":%s,\"write_length\":%llu,\"write_error\":%lu",ok?"true":"false",(unsigned long long)n,usb.last_error);
    if(ok) {n=1;ok=l25_usb_read(&usb,&version,&n,3000) && n==1;}
    printf(",\"read_length\":%llu,\"chip_read\":%s,\"register_69\":%u,\"chip_family\":%u,\"win32_error\":%lu}\n",(unsigned long long)n,ok?"true":"false",version,version&7,usb.last_error);
    l25_usb_close(&usb);return ok?0:2;
}
