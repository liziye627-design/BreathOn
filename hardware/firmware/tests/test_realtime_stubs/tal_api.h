#ifndef TEST_RT_TAL_API_H
#define TEST_RT_TAL_API_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef int OPERATE_RET;
typedef bool BOOL_T;
typedef void *THREAD_HANDLE;
typedef void *MUTEX_HANDLE;
typedef struct { char *thrdname; int priority; uint32_t stackDepth; } THREAD_CFG_T;
enum { OPRT_OK=0, OPRT_INVALID_PARM=-1, OPRT_COM_ERROR=-2, OPRT_MALLOC_FAILED=-3, OPRT_NOT_FOUND=-4 };
#define THREAD_PRIO_2 3
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define Malloc malloc
#define Free free
#define tal_malloc malloc
#define tal_free free
#define PROJECT_VERSION "host-test"
static inline void test_rt_log(const char *fmt, ...) { (void)fmt; }
#define PR_NOTICE(...) test_rt_log(__VA_ARGS__)
#define PR_ERR(...) test_rt_log(__VA_ARGS__)
#define TUYA_CALL_ERR_RETURN(call) do { int result_ = (call); if (result_) return result_; } while (0)
uint32_t tal_system_enter_critical(void);
void tal_system_exit_critical(uint32_t irq);
uint32_t tal_system_get_millisecond(void);
void tal_system_sleep(uint32_t ms);
OPERATE_RET tal_thread_create_and_start(THREAD_HANDLE *, void (*)(void), void (*)(void), void (*)(void *), const void *, const THREAD_CFG_T *);
OPERATE_RET tal_thread_delete(THREAD_HANDLE thread);
OPERATE_RET tal_mutex_create_init(MUTEX_HANDLE *);
OPERATE_RET tal_mutex_lock(MUTEX_HANDLE);
OPERATE_RET tal_mutex_unlock(MUTEX_HANDLE);
OPERATE_RET tal_mutex_release(MUTEX_HANDLE);
#endif
