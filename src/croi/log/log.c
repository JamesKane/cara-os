// SPDX-License-Identifier: BSD-2-Clause
//
// Cara structured logging — record format, ring buffer, sink dispatch,
// and the printf-flavoured Croi_Log entry point. The ring is allocated
// from the kernel heap during Log_Init; sinks are registered after.

#include <cara/alloc.h>
#include <cara/log.h>
#include <cara/time.h>
#include <cara/types.h>

#include <stdarg.h>

#define MAX_SINKS 4
#define RECORDS_PER_RING (CARA_LOG_RING_BYTES / CARA_LOG_RECORD_BYTES)

static struct LogRecord *g_ring = nullptr; // CARA_LOG_RING_BYTES
static u32 g_ring_head = 0;                // monotonic, mod = ring index
static u32 g_ring_total = 0;               // total records appended
static struct LogSink g_sinks[MAX_SINKS];
static u32 g_n_sinks = 0;
static u8 g_min_level = (u8)LOG_LV_TRACE;

// ----- printf-flavoured formatter into a fixed-size buffer ------------------

struct BufWriter {
    char *buf;
    usize cap;
    usize used;
};

static void bw_putc(struct BufWriter *bw, char c)
{
    if (bw->used + 1 < bw->cap) {
        bw->buf[bw->used] = c;
    }
    bw->used++;
}

static void bw_str(struct BufWriter *bw, const char *s)
{
    if (!s) {
        s = "(null)";
    }
    while (*s) {
        bw_putc(bw, *s++);
    }
}

static void bw_dec_u64(struct BufWriter *bw, u64 v)
{
    char tmp[24];
    int n = 0;
    if (v == 0) {
        bw_putc(bw, '0');
        return;
    }
    while (v > 0) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        bw_putc(bw, tmp[--n]);
    }
}

static void bw_dec_i64(struct BufWriter *bw, i64 v)
{
    if (v < 0) {
        bw_putc(bw, '-');
        bw_dec_u64(bw, (u64)(-(v + 1)) + 1u);
        return;
    }
    bw_dec_u64(bw, (u64)v);
}

static void bw_hex_u64(struct BufWriter *bw, u64 v, int min_digits)
{
    char tmp[16];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    }
    while (v > 0) {
        u32 d = (u32)(v & 0xFu);
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
        v >>= 4;
    }
    while (n < min_digits) {
        tmp[n++] = '0';
    }
    while (n > 0) {
        bw_putc(bw, tmp[--n]);
    }
}

static void bw_dec_pad(struct BufWriter *bw, u64 v, int width, char pad)
{
    char tmp[24];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    }
    while (v > 0) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n < width) {
        tmp[n++] = pad;
    }
    while (n > 0) {
        bw_putc(bw, tmp[--n]);
    }
}

static usize bw_format_v(char *out, usize len, const char *fmt, va_list ap)
{
    struct BufWriter bw = { .buf = out, .cap = len, .used = 0 };
    while (*fmt) {
        char c = *fmt++;
        if (c != '%') {
            bw_putc(&bw, c);
            continue;
        }
        bool is_long_long = false;
        if (*fmt == 'l' && fmt[1] == 'l') {
            is_long_long = true;
            fmt += 2;
        } else if (*fmt == 'l' || *fmt == 'z') {
            is_long_long = true;
            fmt++;
        }
        char conv = *fmt++;
        switch (conv) {
        case 's':
            bw_str(&bw, va_arg(ap, const char *));
            break;
        case 'c':
            bw_putc(&bw, (char)va_arg(ap, int));
            break;
        case 'd':
            if (is_long_long) {
                bw_dec_i64(&bw, va_arg(ap, i64));
            } else {
                bw_dec_i64(&bw, (i64)va_arg(ap, int));
            }
            break;
        case 'u':
            if (is_long_long) {
                bw_dec_u64(&bw, va_arg(ap, u64));
            } else {
                bw_dec_u64(&bw, (u64)va_arg(ap, unsigned int));
            }
            break;
        case 'x':
            if (is_long_long) {
                bw_hex_u64(&bw, va_arg(ap, u64), 0);
            } else {
                bw_hex_u64(&bw, (u64)va_arg(ap, unsigned int), 0);
            }
            break;
        case 'p': {
            void *p = va_arg(ap, void *);
            bw_putc(&bw, '0');
            bw_putc(&bw, 'x');
            bw_hex_u64(&bw, (u64)(uptr)p, 16);
            break;
        }
        case '%':
            bw_putc(&bw, '%');
            break;
        case '\0':
            goto done;
        default:
            bw_putc(&bw, '?');
            break;
        }
    }
done:
    if (bw.cap > 0) {
        bw.buf[bw.used < bw.cap ? bw.used : bw.cap - 1] = 0;
    }
    return bw.used;
}

// ----- log subsystem --------------------------------------------------------

static void ring_emit(const struct LogRecord *r, void *ctx)
{
    (void)ctx;
    g_ring[g_ring_head % RECORDS_PER_RING] = *r;
    g_ring_head++;
    g_ring_total++;
}

[[nodiscard]] int Log_Init(void)
{
    g_ring = (struct LogRecord *)Croi_Alloc(CARA_LOG_RING_BYTES);
    if (!g_ring) {
        return CARA_ENOMEM;
    }
    g_ring_head = 0;
    g_ring_total = 0;
    g_n_sinks = 0;
    g_min_level = (u8)LOG_LV_TRACE;

    struct LogSink ring_sink = {
        .emit = ring_emit,
        .ctx = nullptr,
        .ansi_capable = false,
        .min_level = (u8)LOG_LV_TRACE,
    };
    g_sinks[g_n_sinks++] = ring_sink;
    return CARA_EOK;
}

[[nodiscard]] int Log_RegisterSink(const struct LogSink *sink)
{
    if (!sink || !sink->emit) {
        return CARA_EINVAL;
    }
    if (g_n_sinks >= MAX_SINKS) {
        return CARA_ENOMEM;
    }
    g_sinks[g_n_sinks++] = *sink;
    return CARA_EOK;
}

void Log_SetMinLevel(LogLevel level)
{
    g_min_level = (u8)level;
}

char Log_LevelChar(LogLevel level)
{
    static const char tbl[] = "TDIWEF";
    return level <= LOG_LV_FATAL ? tbl[level] : '?';
}

void Croi_Log(LogLevel level, const char *tag, const char *fmt, ...)
{
    if ((u8)level < g_min_level || g_n_sinks == 0) {
        return;
    }

    struct LogRecord r = {
        .ts_ns = Croi_Time_Now(),
        .level = (u8)level,
        .hartid = 0, // SMP later
        .msg_len = 0,
    };
    for (u32 i = 0; i < CARA_LOG_TAG_LEN; i++) {
        r.tag[i] = (tag && tag[i]) ? tag[i] : 0;
    }

    va_list ap;
    va_start(ap, fmt);
    usize n = bw_format_v(r.msg, CARA_LOG_MSG_LEN, fmt, ap);
    va_end(ap);
    r.msg_len = (u16)(n < CARA_LOG_MSG_LEN ? n : CARA_LOG_MSG_LEN - 1);

    for (u32 i = 0; i < g_n_sinks; i++) {
        if ((u8)level >= g_sinks[i].min_level) {
            g_sinks[i].emit(&r, g_sinks[i].ctx);
        }
    }
}

void Log_ReplayInto(const struct LogSink *sink)
{
    if (!sink || !sink->emit) {
        return;
    }
    u32 count = g_ring_total < RECORDS_PER_RING ? g_ring_total : RECORDS_PER_RING;
    u32 start = g_ring_total < RECORDS_PER_RING ? 0 : g_ring_head % RECORDS_PER_RING;
    for (u32 i = 0; i < count; i++) {
        const struct LogRecord *r = &g_ring[(start + i) % RECORDS_PER_RING];
        if (r->level >= sink->min_level) {
            sink->emit(r, sink->ctx);
        }
    }
}

static const char *level_color(LogLevel level)
{
    switch (level) {
    case LOG_LV_TRACE:
        return "\x1b[90m"; // bright black / gray
    case LOG_LV_DEBUG:
        return "\x1b[36m"; // cyan
    case LOG_LV_INFO:
        return "\x1b[32m"; // green
    case LOG_LV_WARN:
        return "\x1b[33m"; // yellow
    case LOG_LV_ERROR:
        return "\x1b[31m"; // red
    case LOG_LV_FATAL:
        return "\x1b[1;31m"; // bold red
    }
    return "";
}

usize Log_FormatHuman(char *out, usize len, const struct LogRecord *r, bool ansi)
{
    if (!out || len == 0) {
        return 0;
    }
    struct BufWriter bw = { .buf = out, .cap = len, .used = 0 };
    u64 sec = r->ts_ns / 1000000000ull;
    u64 usec = (r->ts_ns / 1000ull) % 1000000ull;
    bw_putc(&bw, '[');
    bw_dec_pad(&bw, sec, 5, ' ');
    bw_putc(&bw, '.');
    bw_dec_pad(&bw, usec, 6, '0');
    bw_putc(&bw, ']');
    bw_putc(&bw, ' ');
    if (ansi) {
        bw_str(&bw, level_color((LogLevel)r->level));
    }
    bw_putc(&bw, Log_LevelChar((LogLevel)r->level));
    if (ansi) {
        bw_str(&bw, "\x1b[0m");
    }
    bw_putc(&bw, ' ');
    for (u32 i = 0; i < CARA_LOG_TAG_LEN && r->tag[i]; i++) {
        bw_putc(&bw, r->tag[i]);
    }
    bw_putc(&bw, ':');
    bw_putc(&bw, ' ');
    for (u16 i = 0; i < r->msg_len; i++) {
        bw_putc(&bw, r->msg[i]);
    }
    bw_putc(&bw, '\n');
    if (bw.cap > 0) {
        bw.buf[bw.used < bw.cap ? bw.used : bw.cap - 1] = 0;
    }
    return bw.used;
}
