#include "app_breathon_realtime_internal.h"
#include <stdlib.h>
/* Same pinned Beken CLI ABI as app_breathon_audio_cli.c. */
struct cli_command { const char *name, *help; void (*function)(char *, int, int, char **); };
extern int cli_register_command(const struct cli_command *command);
static void command(char *output, int size, int argc, char **argv)
{
    (void)output; (void)size;
    if (argc == 2) {
        if (!strcmp(argv[1], "start")) { app_breathon_realtime_start(); return; }
        if (!strcmp(argv[1], "stop")) { app_breathon_realtime_stop(); return; }
        if (!strcmp(argv[1], "status")) { app_breathon_realtime_status(); return; }
    }
    if (argc == 5 && !strcmp(argv[1], "config")) {
        char *end = NULL;
        unsigned long port = strtoul(argv[3], &end, 10);
        if (argv[3][0] < '0' || argv[3][0] > '9' || *end || port == 0 || port > 65535 ||
            app_breathon_realtime_config(argv[2], (uint16_t)port, argv[4]) != OPRT_OK) {
            PR_NOTICE("BREATHON_REALTIME config rejected (invalid or busy)");
        } else {
            PR_NOTICE("BREATHON_REALTIME configured storage=KV");
        }
        return;
    }
    PR_NOTICE("breathon_realtime config <IPv4> <port> <LANtoken> | start | stop | status");
}
OPERATE_RET rt_cli_register(void)
{
    static const struct cli_command entry = {"breathon_realtime", "Realtime LAN audio", command};
    return cli_register_command(&entry) == 0 ? OPRT_OK : OPRT_COM_ERROR;
}
