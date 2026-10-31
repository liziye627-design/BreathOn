#include "tal_api.h"
int tal_kv_set(const char *, const uint8_t *, size_t);
int tal_kv_get(const char *, uint8_t **, size_t *);
int tal_kv_free(uint8_t *);
