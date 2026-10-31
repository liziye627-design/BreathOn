#ifndef APP_BREATHON_WIFI_JSON_H
#define APP_BREATHON_WIFI_JSON_H
#include "netconn_wifi.h"

/* The only accepted schema is two bounded strings, s/p. No heap, recursion,
 * permissive truncation, or allocator-global hooks in the credential path. */
#define WIFI_JSON_CAPACITY 640
static inline void wifi_secret_wipe(void *data, size_t size)
{
    volatile uint8_t *p = data;
    while (size--) *p++ = 0;
}
typedef struct { const uint8_t *data; size_t length, pos; } wifi_json_reader;
static inline bool wifi_json_take(wifi_json_reader *r, uint8_t ch)
{
    while (r->pos < r->length && (r->data[r->pos] == ' ' || r->data[r->pos] == '\t' ||
           r->data[r->pos] == '\r' || r->data[r->pos] == '\n')) ++r->pos;
    if (r->pos == r->length || r->data[r->pos] != ch) return false;
    ++r->pos; return true;
}
static inline bool wifi_json_hex4(wifi_json_reader *r, uint32_t *value)
{
    *value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (r->pos == r->length) return false;
        uint8_t c = r->data[r->pos++];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        *value = (*value << 4) | digit;
    }
    return true;
}
static inline bool wifi_json_byte(char *out, size_t capacity, size_t *used, uint8_t c)
{
    if (*used + 1 >= capacity) return false;
    out[(*used)++] = (char)c; return true;
}
static inline bool wifi_json_string(wifi_json_reader *r, char *out, size_t capacity)
{
    size_t used = 0;
    if (!wifi_json_take(r, '"')) return false;
    while (r->pos < r->length) {
        uint8_t c = r->data[r->pos++];
        if (c == '"') { out[used] = 0; return true; }
        if (c < 0x20) return false;
        if (c == '\\') {
            if (r->pos == r->length) return false;
            c = r->data[r->pos++];
            switch (c) {
            case '"': case '\\': case '/': break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u': {
                uint32_t cp, low;
                if (!wifi_json_hex4(r, &cp) || !cp) return false;
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (r->length - r->pos < 6 || r->data[r->pos++] != '\\' ||
                        r->data[r->pos++] != 'u' || !wifi_json_hex4(r, &low) ||
                        low < 0xdc00 || low > 0xdfff) return false;
                    cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                } else if (cp >= 0xdc00 && cp <= 0xdfff) return false;
                if (cp >= 0x10000) {
                    if (!wifi_json_byte(out, capacity, &used, 0xf0 | (cp >> 18)) ||
                        !wifi_json_byte(out, capacity, &used, 0x80 | ((cp >> 12) & 0x3f))) return false;
                } else if (cp >= 0x800) {
                    if (!wifi_json_byte(out, capacity, &used, 0xe0 | (cp >> 12))) return false;
                }
                if (cp >= 0x80) {
                    if (!wifi_json_byte(out, capacity, &used, cp < 0x800 ? 0xc0 | (cp >> 6) : 0x80 | ((cp >> 6) & 0x3f))) return false;
                }
                c = cp < 0x80 ? cp : 0x80 | (cp & 0x3f);
                break;
            }
            default: return false;
            }
        }
        if (!wifi_json_byte(out, capacity, &used, c)) return false;
    }
    return false;
}
static inline bool wifi_json_parse(const uint8_t *data, size_t length, netconn_wifi_info_t *info)
{
    wifi_json_reader r = {data, length, 0};
    unsigned fields = 0;
    char key[2] = {0};
    memset(info, 0, sizeof(*info));
    if (!data || !length || length > WIFI_JSON_CAPACITY || memchr(data, 0, length) ||
        !wifi_json_take(&r, '{')) goto fail;
    for (unsigned i = 0; i < 2; ++i) {
        if (i && !wifi_json_take(&r, ',')) goto fail;
        if (!wifi_json_string(&r, key, sizeof(key)) || !wifi_json_take(&r, ':')) goto fail;
        unsigned field = key[0] == 's' ? 1 : key[0] == 'p' ? 2 : 0;
        if (!field || (fields & field)) goto fail;
        fields |= field;
        if (!wifi_json_string(&r, field == 1 ? info->ssid : info->pswd,
                              field == 1 ? sizeof(info->ssid) : sizeof(info->pswd))) goto fail;
    }
    if (!wifi_json_take(&r, '}') || !info->ssid[0]) goto fail;
    /* Consume only JSON whitespace, requiring the reported KV bound. */
    (void)wifi_json_take(&r, 0);
    if (r.pos != r.length) goto fail;
    wifi_secret_wipe(key, sizeof(key)); return true;
fail:
    wifi_secret_wipe(key, sizeof(key)); wifi_secret_wipe(info, sizeof(*info)); return false;
}
static inline bool wifi_json_encode(const netconn_wifi_info_t *info, char *out, size_t capacity, size_t *length)
{
    static const char hex[] = "0123456789abcdef";
    size_t used = 0;
    *length = 0; memset(out, 0, capacity);
    if (!info->ssid[0] || !memchr(info->ssid, 0, sizeof(info->ssid)) ||
        !memchr(info->pswd, 0, sizeof(info->pswd))) goto fail;
    const char *values[] = {info->ssid, info->pswd};
    for (unsigned field = 0; field < 2; ++field) {
        const char *prefix = field ? ",\"p\":\"" : "{\"s\":\"";
        for (const char *p = prefix; *p; ++p)
            if (!wifi_json_byte(out, capacity, &used, *p)) goto fail;
        for (const uint8_t *p = (const uint8_t *)values[field]; *p; ++p) {
            if (*p == '"' || *p == '\\') {
                if (!wifi_json_byte(out, capacity, &used, '\\')) goto fail;
            } else if (*p < 0x20) {
                const char *prefix_control = "\\u00";
                for (const char *q = prefix_control; *q; ++q)
                    if (!wifi_json_byte(out, capacity, &used, *q)) goto fail;
                if (!wifi_json_byte(out, capacity, &used, hex[*p >> 4]) ||
                    !wifi_json_byte(out, capacity, &used, hex[*p & 15])) goto fail;
                continue;
            }
            if (!wifi_json_byte(out, capacity, &used, *p)) goto fail;
        }
        if (!wifi_json_byte(out, capacity, &used, '"')) goto fail;
    }
    if (!wifi_json_byte(out, capacity, &used, '}')) goto fail;
    *length = used; return true;
fail:
    wifi_secret_wipe(out, capacity); return false;
}
#endif
