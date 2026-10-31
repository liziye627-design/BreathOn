/* Links the real SDK netcfg.c, netcfg.h, tuya_list.c and tuya_list.h.
 * Only allocation, executor, config mutex and module callbacks are adapters. */
#include "netcfg.h"
#include "netconn_wifi.h"
#include "tal_workq_service.h"
#include <stdio.h>
#include <windows.h>
/* The pinned SDK exports this count function without a header declaration. */
extern int netcfg_get_register_started_count(void);

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
typedef struct { void *pointer; size_t size; bool live; } allocation_t;
static allocation_t allocations[64];
static unsigned allocation_count, free_count, live_count, cancel_calls;
static bool fail_next_allocation;
static LONG start_active;
static CRITICAL_SECTION memory_lock, config_lock;
static _Thread_local unsigned config_depth;
static _Thread_local bool stopping_thread;
static HANDLE start_entered, start_release, stop_contended;
static bool block_start, fail_lock;
static int start_result, schedule_result;
static unsigned starts, stops;
static int old_args, new_args;
static void *expected_args;
typedef struct { WORKQUEUE_CB callback; void *arg; } job_t;
static job_t jobs[16];
static unsigned job_count;

void netcfg_test_log(const char *format, ...) { (void)format; }
void *netcfg_test_malloc(size_t size)
{
    EnterCriticalSection(&memory_lock);
    if (fail_next_allocation) {
        fail_next_allocation = false; LeaveCriticalSection(&memory_lock); return NULL;
    }
    CHECK(allocation_count < 64 && size <= 4096);
    void *pointer = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(pointer); memset(pointer, 0xa5, size);
    allocations[allocation_count++] = (allocation_t){pointer, size, true}; ++live_count;
    LeaveCriticalSection(&memory_lock); return pointer;
}
void netcfg_test_free(void *pointer)
{
    if (!pointer) return;
    EnterCriticalSection(&memory_lock);
    CHECK(InterlockedCompareExchange(&start_active, 0, 0) == 0);
    for (unsigned i = 0; i < allocation_count; ++i) {
        if (allocations[i].pointer != pointer) continue;
        CHECK(allocations[i].live);
        allocations[i].live = false; ++free_count; --live_count;
        /* A stale handler dereference faults deterministically; pages are
         * quarantined until process cleanup, never recycled into a new job. */
        DWORD previous;
        CHECK(VirtualProtect(pointer, 4096, PAGE_NOACCESS, &previous));
        LeaveCriticalSection(&memory_lock); return;
    }
    CHECK(false);
}
OPERATE_RET netconn_wifi_config_lock(void)
{
    CHECK(!config_depth);
    if (fail_lock) return OPRT_COM_ERROR;
    if (stopping_thread) {
        CHECK(!TryEnterCriticalSection(&config_lock));
        SetEvent(stop_contended);
    }
    EnterCriticalSection(&config_lock); ++config_depth; return OPRT_OK;
}
void netconn_wifi_config_unlock(void)
{
    CHECK(config_depth == 1); --config_depth; LeaveCriticalSection(&config_lock);
}
int tal_workq_schedule(WORKQ_SERVICE_E service, WORKQUEUE_CB callback, void *arg)
{
    CHECK(service == WORKQ_HIGHTPRI);
    if (schedule_result) return schedule_result;
    CHECK(job_count < 16); jobs[job_count++] = (job_t){callback, arg}; return OPRT_OK;
}
int tal_workq_cancel(WORKQ_SERVICE_E service, WORKQUEUE_CB callback, void *arg)
{
    CHECK(service == WORKQ_HIGHTPRI); ++cancel_calls;
    for (unsigned i = 0; i < job_count;) {
        if (jobs[i].callback == callback && (!arg || jobs[i].arg == arg)) {
            memmove(jobs + i, jobs + i + 1, (--job_count - i) * sizeof(jobs[0]));
        } else ++i;
    }
    return OPRT_OK;
}
static job_t dequeue(void)
{
    CHECK(job_count); job_t job = jobs[0];
    memmove(jobs, jobs + 1, --job_count * sizeof(jobs[0])); return job;
}
static void execute(job_t job) { job.callback(job.arg); CHECK(!config_depth); }
static int finish_cb(int type, netcfg_info_t *info) { (void)type; (void)info; return OPRT_OK; }
static int start_cb(int type, netcfg_finish_cb_t finish, void *args)
{
    CHECK(config_depth == 1);
    CHECK(type == NETCFG_TUYA_WIFI_AP || type == NETCFG_TUYA_BLE);
    CHECK(finish == finish_cb && args == expected_args); ++starts;
    if (block_start) {
        InterlockedExchange(&start_active, 1); SetEvent(start_entered);
        CHECK(WaitForSingleObject(start_release, 5000) == WAIT_OBJECT_0);
        InterlockedExchange(&start_active, 0);
    }
    return start_result;
}
static int stop_cb(int type)
{
    CHECK(config_depth == 1 && !InterlockedCompareExchange(&start_active, 0, 0));
    CHECK(type == NETCFG_TUYA_WIFI_AP || type == NETCFG_TUYA_BLE); ++stops; return OPRT_OK;
}
static void init_session(void *args)
{
    expected_args = args;
    CHECK(netconn_wifi_config_lock() == OPRT_OK);
    CHECK(netcfg_init() == OPRT_OK);
    CHECK(netcfg_register(NETCFG_TUYA_WIFI_AP, start_cb, stop_cb) == OPRT_OK);
    netconn_wifi_config_unlock();
}
static int enqueue(int type, void *args)
{
    CHECK(netconn_wifi_config_lock() == OPRT_OK);
    int result = netcfg_start(type, finish_cb, args);
    netconn_wifi_config_unlock(); return result;
}
static void stop_all(void)
{
    CHECK(netconn_wifi_config_lock() == OPRT_OK);
    CHECK(netcfg_stop(NETCFG_STOP_ALL_CFG_MODULE) == OPRT_OK);
    netconn_wifi_config_unlock();
}
static void test_stale(bool reinit)
{
    init_session(&old_args); CHECK(enqueue(NETCFG_TUYA_WIFI_AP, &old_args) == OPRT_OK);
    job_t old = dequeue(); /* Already dequeued: queue cancellation cannot help. */
    stop_all();
    if (reinit) init_session(&new_args);
    unsigned before = free_count; execute(old);
    CHECK(starts == 0 && free_count == before + 1);
    if (reinit) {
        CHECK(netcfg_get_register_started_count() == 0);
        CHECK(enqueue(NETCFG_TUYA_WIFI_AP, &new_args) == OPRT_OK);
        execute(dequeue()); CHECK(starts == 1 && netcfg_get_register_started_count() == 1);
        stop_all(); CHECK(stops == 1);
    }
}
static void test_queued_drain(void)
{
    init_session(&old_args);
    CHECK(netconn_wifi_config_lock() == OPRT_OK);
    CHECK(netcfg_register(NETCFG_TUYA_BLE, start_cb, stop_cb) == OPRT_OK);
    netconn_wifi_config_unlock();
    CHECK(enqueue(NETCFG_TUYA_WIFI_AP, &old_args) == OPRT_OK);
    CHECK(enqueue(NETCFG_TUYA_BLE, &old_args) == OPRT_OK);
    stop_all(); CHECK(job_count == 2 && live_count == 2 && cancel_calls == 0);
    while (job_count) execute(dequeue());
    CHECK(starts == 0 && stops == 0);
}
static DWORD WINAPI execute_thread(void *arg) { execute(*(job_t *)arg); return 0; }
static DWORD WINAPI stop_thread(void *arg) { (void)arg; stopping_thread = true; stop_all(); return 0; }
static void test_inflight(void)
{
    init_session(&old_args); CHECK(enqueue(NETCFG_TUYA_WIFI_AP, &old_args) == OPRT_OK);
    job_t job = dequeue(); block_start = true;
    start_entered = CreateEvent(NULL, TRUE, FALSE, NULL);
    start_release = CreateEvent(NULL, TRUE, FALSE, NULL);
    stop_contended = CreateEvent(NULL, TRUE, FALSE, NULL);
    CHECK(start_entered && start_release && stop_contended);
    HANDLE start = CreateThread(NULL, 0, execute_thread, &job, 0, NULL); CHECK(start);
    CHECK(WaitForSingleObject(start_entered, 5000) == WAIT_OBJECT_0);
    HANDLE stop = CreateThread(NULL, 0, stop_thread, NULL, 0, NULL); CHECK(stop);
    CHECK(WaitForSingleObject(stop_contended, 5000) == WAIT_OBJECT_0);
    EnterCriticalSection(&memory_lock); CHECK(free_count == 0 && live_count == 3); LeaveCriticalSection(&memory_lock);
    CHECK(stops == 0); SetEvent(start_release);
    CHECK(WaitForSingleObject(start, 5000) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(stop, 5000) == WAIT_OBJECT_0);
    CHECK(starts == 1 && stops == 1);
    CloseHandle(start); CloseHandle(stop); CloseHandle(start_entered); CloseHandle(start_release); CloseHandle(stop_contended);
}
static void test_failure(const char *name)
{
    if (!strcmp(name, "session-allocation-failure")) {
        fail_next_allocation = true; CHECK(netcfg_init() == OPRT_MALLOC_FAILED); return;
    }
    if (!strcmp(name, "handler-allocation-failure")) {
        CHECK(netcfg_init() == OPRT_OK); fail_next_allocation = true;
        CHECK(netcfg_register(NETCFG_TUYA_WIFI_AP, start_cb, stop_cb) == OPRT_MALLOC_FAILED);
        stop_all(); return;
    }
    init_session(&old_args);
    if (!strcmp(name, "job-allocation-failure")) {
        fail_next_allocation = true; CHECK(enqueue(NETCFG_TUYA_WIFI_AP, &old_args) == OPRT_MALLOC_FAILED);
        CHECK(job_count == 0 && live_count == 2);
    } else if (!strcmp(name, "job-schedule-failure")) {
        schedule_result = OPRT_COM_ERROR; unsigned before = free_count;
        CHECK(enqueue(NETCFG_TUYA_WIFI_AP, &old_args) == OPRT_COM_ERROR);
        CHECK(job_count == 0 && live_count == 2 && free_count == before + 1);
    } else {
        CHECK(enqueue(NETCFG_TUYA_WIFI_AP, &old_args) == OPRT_OK);
        job_t job = dequeue();
        if (!strcmp(name, "job-lock-failure")) fail_lock = true;
        else if (!strcmp(name, "module-start-failure")) start_result = OPRT_COM_ERROR;
        else if (!strcmp(name, "handler-removed")) CHECK(netcfg_unregister(NETCFG_TUYA_WIFI_AP) == OPRT_OK);
        else CHECK(false);
        execute(job); fail_lock = false;
        CHECK(starts == (!strcmp(name, "module-start-failure") ? 1u : 0u));
        CHECK(netcfg_get_register_started_count() == 0);
    }
    stop_all(); CHECK(stops == 0);
}
int main(int argc, char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    CHECK(argc == 2);
    InitializeCriticalSection(&memory_lock); InitializeCriticalSection(&config_lock);
    const char *name = argv[1];
    if (!strcmp(name, "dequeued-after-stop")) test_stale(false);
    else if (!strcmp(name, "dequeued-after-reinit")) test_stale(true);
    else if (!strcmp(name, "queued-stale-drain")) test_queued_drain();
    else if (!strcmp(name, "inflight-start-stop")) test_inflight();
    else test_failure(name);
    CHECK(!job_count && !live_count && allocation_count == free_count && !config_depth);
    CHECK(cancel_calls == 0);
    for (unsigned i = 0; i < allocation_count; ++i) CHECK(VirtualFree(allocations[i].pointer, 0, MEM_RELEASE));
    DeleteCriticalSection(&memory_lock); DeleteCriticalSection(&config_lock);
    printf("PASS real netcfg lifecycle: %s; allocations=%u frees=%u, no stale start/UAF/leak\n", name, allocation_count, free_count);
    return 0;
}
