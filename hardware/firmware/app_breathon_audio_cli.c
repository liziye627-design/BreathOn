#include "app_breathon_audio.h"
/* ABI matches bk_private/bk_cli.h in pinned T5AI 69793c66. */
struct cli_command {
    const char *name;
    const char *help;
    void (*function)(char *, int, int, char **);
};
extern int cli_register_command(const struct cli_command *command);

static void command(char *output, int size, int argc, char **argv)
{
    (void)output; (void)size;
    const char *names[] = {"record", "play", "stop", "tone", "export", "status"};
    if (argc == 2) {
        for (int i = 0; i < 6; ++i) {
            if (strcmp(argv[1], names[i]) == 0) { app_breathon_audio_request(i); return; }
        }
    }
    PR_NOTICE("breathon_audio record|play|stop|tone|export|status");
}

void app_breathon_audio_export(const audio_capture_t *capture)
{
    uint32_t size = AUDIO_WAV_HEADER + capture->bytes;
    uint32_t hash = audio_capture_checksum(capture->wav, size);
    PR_NOTICE("BREATHON_WAV_BEGIN bytes=%u fnv1a=%08x", size, hash);
    static const char digits[] = "0123456789abcdef";
    for (uint32_t offset = 0; offset < size; offset += 128) {
        if (app_breathon_audio_stop_pending()) { PR_NOTICE("BREATHON_WAV_CANCEL"); return; }
        uint32_t count = size - offset;
        if (count > 128) count = 128;
        char hex[257];
        for (uint32_t i = 0; i < count; ++i) {
            uint8_t value = capture->wav[offset + i];
            hex[2 * i] = digits[value >> 4];
            hex[2 * i + 1] = digits[value & 15];
        }
        hex[2 * count] = '\0';
        PR_NOTICE("BREATHON_WAV_DATA %06u %s", offset, hex);
        tal_system_sleep(10);
    }
    PR_NOTICE("BREATHON_WAV_END bytes=%u fnv1a=%08x", size, hash);
}

OPERATE_RET app_breathon_audio_cli_register(void)
{
    static const struct cli_command entry = {"breathon_audio", "Offline audio diagnostics", command};
    return cli_register_command(&entry) == 0 ? OPRT_OK : OPRT_COM_ERROR;
}
