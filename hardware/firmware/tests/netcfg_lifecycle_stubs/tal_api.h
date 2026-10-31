#ifndef NETCFG_TEST_TAL_API_H
#define NETCFG_TEST_TAL_API_H
#include "tuya_cloud_types.h"
void *netcfg_test_malloc(size_t);
void netcfg_test_free(void *);
void netcfg_test_log(const char *, ...);
#define tal_malloc netcfg_test_malloc
#define tal_free netcfg_test_free
#define PR_DEBUG(...) netcfg_test_log(__VA_ARGS__)
#define PR_ERR(...) netcfg_test_log(__VA_ARGS__)
#define PR_NOTICE(...) netcfg_test_log(__VA_ARGS__)
#endif
