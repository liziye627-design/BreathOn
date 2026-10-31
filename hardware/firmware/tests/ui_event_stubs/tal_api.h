#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int OPERATE_RET;
#define OPRT_OK 0
#define PR_NOTICE(...) ((void)0)
#define TUYA_CALL_ERR_RETURN(call) do { int result = (call); if (result) return result; } while (0)
