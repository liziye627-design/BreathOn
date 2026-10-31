#include "tal_api.h"
typedef enum { WORKQ_SYSTEM, WORKQ_HIGHTPRI } WORKQ_SERVICE_E;
typedef void (*WORKQUEUE_CB)(void *);
int tal_workq_schedule(WORKQ_SERVICE_E, WORKQUEUE_CB, void *);
