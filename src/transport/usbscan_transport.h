#pragma once
#include <windows.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { HANDLE handle; DWORD last_error; } L25Usb;
int l25_usb_open(L25Usb *usb, const wchar_t *port);
void l25_usb_close(L25Usb *usb);
int l25_usb_read(L25Usb *usb, void *data, size_t *length, DWORD timeout_ms);
int l25_usb_write(L25Usb *usb, const void *data, size_t *length, DWORD timeout_ms);
int l25_usb_ioctl(L25Usb *usb, DWORD code, void *in, DWORD in_size, void *out, DWORD out_size, DWORD *received);
void l25_usb_cancel(L25Usb *usb);
#ifdef __cplusplus
}
#endif
