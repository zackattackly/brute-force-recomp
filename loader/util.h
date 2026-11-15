#pragma once
#include "win32.h"
extern HANDLE g_log;
void out(const char *s);
void hex(u32 v);
void dec(u32 v);
void *memcpy_(void *d, const void *s, u32 n);
void memset_(void *d, int c, u32 n);
u32 rd32(const u8 *p);
void log_flush(void);
