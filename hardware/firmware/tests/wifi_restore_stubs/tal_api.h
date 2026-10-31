#ifndef WIFI_RESTORE_TAL_API_H
#define WIFI_RESTORE_TAL_API_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef int OPERATE_RET;
typedef void *THREAD_HANDLE;
typedef void *MUTEX_HANDLE;
typedef struct { char *thrdname; int priority; uint32_t stackDepth; } THREAD_CFG_T;
enum { OPRT_OK=0, OPRT_INVALID_PARM=-1, OPRT_COM_ERROR=-2, OPRT_MALLOC_FAILED=-3, OPRT_NOT_FOUND=-4, OPRT_RESOURCE_NOT_READY=-5 };
int tal_mutex_create_init(MUTEX_HANDLE *);
int tal_mutex_lock(MUTEX_HANDLE);
int tal_mutex_unlock(MUTEX_HANDLE);
#define THREAD_PRIO_2 3
void wifi_test_log(const char *, ...);
#define PR_NOTICE(...) wifi_test_log(__VA_ARGS__)
#define PR_ERR(...) wifi_test_log(__VA_ARGS__)
#define tal_malloc malloc
#define tal_free free
uint32_t tal_system_enter_critical(void);
void tal_system_exit_critical(uint32_t);
void tal_system_sleep(uint32_t);
OPERATE_RET tal_thread_create_and_start(THREAD_HANDLE *, void (*)(void), void (*)(void), void (*)(void *), const void *, const THREAD_CFG_T *);
#endif
