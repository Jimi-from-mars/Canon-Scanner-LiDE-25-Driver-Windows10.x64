#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {uint32_t size;int dpi,mode;double left_mm,top_mm,width_mm,height_mm;int brightness,contrast;} L25ScanRequest;
typedef struct {int width,height,channels,depth,bytes_per_line,dpi;} L25ImageInfo;
typedef struct {void *context;int (*begin)(void*,const L25ImageInfo*);int (*data)(void*,const uint8_t*,size_t);int (*progress)(void*,int,int);} L25Sink;
/* mode: 0 RGB, 1 grayscale, 2 one-bit line art; return values are SANE_Status. */
int l25_core_acquire(const L25ScanRequest *request,const L25Sink *sink);
void l25_core_cancel(void);
const char *l25_core_error(int status);
#ifdef __cplusplus
}
#endif
