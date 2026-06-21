// SPDX-License-Identifier: BSD-2-Clause
//
// console.device (L6.3, docs/CROI_DEVICES.md §2.4) — over the L3.6
// console. CMD_WRITE emits io_Data/io_Length to the kernel log-backed
// `cout` sink (the same place the dos console handler writes, so a dos
// console FileHandle and console.device CMD_WRITE reach one stream);
// CMD_READ returns EOF (stub stdin, like dos). io_Actual reports bytes
// written. The CD_* console specials (raw/cooked, keymap, prwindow) are
// deferred. Registered with the device registry at boot.

#include <cara/device.h>
#include <cara/log.h> // Croi_Log, CARA_LOG_MSG_LEN
#include <cara/types.h>
#include <exec/io.h>

// Emit `len` bytes through the kernel log, chunked to the log record's
// message capacity (mirrors dos handler.c console_write — same sink).
static void console_emit(const char *p, usize len)
{
    char line[CARA_LOG_MSG_LEN];
    usize i = 0;
    do {
        usize n = 0;
        while (i < len && n < sizeof(line) - 1) {
            line[n++] = p[i++];
        }
        line[n] = 0;
        Croi_Log(LOG_LV_INFO, "cout", "%s", line);
    } while (i < len);
}

static void console_beginio(struct IORequest *io)
{
    struct IOStdReq *r = (struct IOStdReq *)io;
    switch (io->io_Command) {
    case CMD_WRITE:
        if (r->io_Length && r->io_Data) {
            console_emit((const char *)r->io_Data, r->io_Length);
        }
        r->io_Actual = r->io_Length;
        io->io_Error = 0;
        break;
    case CMD_READ:
        r->io_Actual = 0; // EOF: no stdin in v0
        io->io_Error = 0;
        break;
    case CMD_CLEAR:
    case CMD_FLUSH:
        io->io_Error = 0;
        break;
    default:
        io->io_Error = IOERR_NOCMD;
        break;
    }
}

static struct CaraDevice g_console_device = {
    .name = "console.device",
    .beginio = console_beginio,
};

void Croi_Console_Init(void)
{
    Croi_Device_Register(&g_console_device);
}
