// SPDX-License-Identifier: BSD-2-Clause
//
// Cara structured logging.
//
// Every record carries a timestamp (ns since boot), the originating
// hartid, a level, a 4-character source tag, and a printf-formatted
// message. Records flow through a small set of sinks: by default, an
// in-memory ring (dmesg-equivalent, replayable post-hoc) and the
// active console UART. Later sinks (framebuffer text mode, remote
// log) plug in without changing callers.

#ifndef CARA_LOG_H
#define CARA_LOG_H

#include <cara/types.h>

#define CARA_LOG_TAG_LEN 4
#define CARA_LOG_MSG_LEN 112
#define CARA_LOG_RECORD_BYTES 128       // 8+1+1+2+4+112 = 128, no padding
#define CARA_LOG_RING_BYTES (64 * 1024) // 64 KiB → 512 records

typedef enum : u8 {
    LOG_LV_TRACE = 0,
    LOG_LV_DEBUG = 1,
    LOG_LV_INFO  = 2,
    LOG_LV_WARN  = 3,
    LOG_LV_ERROR = 4,
    LOG_LV_FATAL = 5,
} LogLevel;

struct LogRecord {
    u64  ts_ns;
    u8   level;
    u8   hartid;
    u16  msg_len;                       // strlen(msg) at write time
    char tag[CARA_LOG_TAG_LEN];
    char msg[CARA_LOG_MSG_LEN];
};
static_assert(sizeof(struct LogRecord) == CARA_LOG_RECORD_BYTES,
              "LogRecord size drifted; ring math depends on it");

typedef void (*LogEmitFn)(const struct LogRecord *r, void *ctx);

struct LogSink {
    LogEmitFn  emit;
    void      *ctx;
    bool       ansi_capable;
    u8         min_level;
};

// Initialise the log subsystem. Allocates the ring from the kernel
// heap (which must already be active). Registers the ring as the first
// sink. Subsequent Log_RegisterSink calls add UART, framebuffer, etc.
[[nodiscard]] int Log_Init(void);

// Append a sink to the active list. Up to 4 sinks; further calls fail.
[[nodiscard]] int Log_RegisterSink(const struct LogSink *sink);

// Set the runtime minimum level. Records below it are dropped before
// any sink sees them. Default is LOG_LV_TRACE (everything).
void Log_SetMinLevel(LogLevel level);

// Format and emit one record. tag is up to 4 characters and is copied
// verbatim (NUL-padded if shorter). The fmt string supports the same
// conversions as Croi_Print: %s %c %d %u %x %p %lld %llu %llx.
void Croi_Log(LogLevel level, const char *tag, const char *fmt, ...);

#define LOG_TRACE(tag, ...) Croi_Log(LOG_LV_TRACE, (tag), __VA_ARGS__)
#define LOG_DEBUG(tag, ...) Croi_Log(LOG_LV_DEBUG, (tag), __VA_ARGS__)
#define LOG_INFO(tag, ...)  Croi_Log(LOG_LV_INFO,  (tag), __VA_ARGS__)
#define LOG_WARN(tag, ...)  Croi_Log(LOG_LV_WARN,  (tag), __VA_ARGS__)
#define LOG_ERROR(tag, ...) Croi_Log(LOG_LV_ERROR, (tag), __VA_ARGS__)
#define LOG_FATAL(tag, ...) Croi_Log(LOG_LV_FATAL, (tag), __VA_ARGS__)

// Replay the entire ring oldest-to-newest into a sink. Useful for
// dumping post-mortem dmesg into a fresh sink (e.g. when the
// framebuffer console comes up after early boot).
void Log_ReplayInto(const struct LogSink *sink);

// One-letter level tag (T/D/I/W/E/F) for human-readable formatting.
char Log_LevelChar(LogLevel level);

// Built-in human-readable formatter: "[ssss.uuuuuu] L tag: msg\n".
// Writes up to len-1 bytes into out and NUL-terminates.
usize Log_FormatHuman(char *out, usize len, const struct LogRecord *r);

// Built-in sinks. Pass the corresponding pointer as LogSink.ctx.
struct Ns16550;
void Log_Sink_NS16550_Emit(const struct LogRecord *r, void *ctx);

#endif
