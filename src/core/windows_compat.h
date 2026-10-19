#pragma once
#include <sys/types.h>
#include <stdint.h>
#include <signal.h>
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
typedef _sigset_t sigset_t;
static inline char *strsep(char **cursor,const char *delim) {if(!*cursor)return NULL;char *start=*cursor;char *end=start+strcspn(start,delim);if(*end){*end=0;*cursor=end+1;}else *cursor=NULL;return start;}
static inline char *strndup(const char *s,size_t n) {size_t len=strnlen(s,n);char *r=(char*)malloc(len+1);if(r){memcpy(r,s,len);r[len]=0;}return r;}
#ifndef SIGUSR1
#define SIGUSR1 10
#define SIGUSR2 12
#define SIGALRM 14
#define SIGCHLD 17
#define SIGKILL 9
#endif
#define SIG_SETMASK 2
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
struct sigaction {void (*sa_handler)(int);sigset_t sa_mask;int sa_flags;};
static inline int sigemptyset(sigset_t *s) {*s=0;return 0;}
static inline int sigfillset(sigset_t *s) {*s=~(sigset_t)0;return 0;}
static inline int sigaddset(sigset_t *s,int n) {*s|=((sigset_t)1<<n);return 0;}
static inline int sigdelset(sigset_t *s,int n) {*s&=~((sigset_t)1<<n);return 0;}
static inline int sigismember(const sigset_t *s,int n) {return (*s>>n)&1;}
static inline int sigprocmask(int h,const sigset_t *s,sigset_t *old) {(void)h;(void)s;if(old)*old=0;return 0;}
static inline int sigpending(sigset_t *s) {*s=0;return 0;}
static inline int sigaction(int n,const struct sigaction *a,struct sigaction *o) {(void)n;(void)a;(void)o;return 0;}
static inline unsigned alarm(unsigned n) {(void)n;return 0;}
int l25_pipe(int fd[2]);
int l25_pipe_read(int fd,void *buffer,unsigned count);
int l25_pipe_write(int fd,const void *buffer,unsigned count);
int l25_pipe_close(int fd);
int l25_fcntl(int fd,int command,...);
void l25_abort_channel(void);
#define pipe l25_pipe
#define fcntl l25_fcntl
#ifndef F_SETFL
#define F_SETFL 4
#define O_NONBLOCK 0x4000
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT 138
#endif
