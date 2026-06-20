// SPDX-License-Identifier: BSD-2-Clause
//
// lvo-gen — single-file C23 host tool that turns a CaraOS library
// `.conf` (per docs/LVO.md §4) into the four artefact families:
//
//   include/proto/<lib>.h           — application-facing inline stubs
//   include/<lib>/lvo.h             — CARA_IDX_* + _LVO* constants
//   src/<owner>/<lib>_vec.c         — vec table + MakeLibrary TagItems
//   include/aistreoir/lvo_table.gen.h fragment — Phase 9 lookup keys
//
// Stage 1 (B2): parse + validate. Emitters arrive in B3.
//
// CLI:
//   lvo-gen --check <conf>
//     Parse and validate <conf>; exit 0 on success, 1 on errors with
//     diagnostics on stderr.

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============ Diagnostics ===================================================

static const char *g_path = "<unknown>";
static int         g_errors = 0;

[[gnu::format(printf, 2, 3)]]
static void diag(int line, const char *fmt, ...)
{
    fprintf(stderr, "%s:%d: error: ", g_path, line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    g_errors++;
}

// ============ Memory helpers ================================================

[[noreturn]] static void oom(void)
{
    fprintf(stderr, "lvo-gen: out of memory\n");
    exit(2);
}

static void *xcalloc(size_t n, size_t sz)
{
    void *p = calloc(n, sz);
    if (!p) {
        oom();
    }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    void *r = realloc(p, n);
    if (n != 0 && !r) {
        oom();
    }
    return r;
}

static char *xstrndup(const char *s, size_t n)
{
    char *r = (char *)xcalloc(n + 1, 1);
    memcpy(r, s, n);
    return r;
}

static char *xstrdup(const char *s) { return xstrndup(s, strlen(s)); }

// ============ Data model ====================================================

typedef enum {
    FLAVOUR_LOCAL = 1,
    FLAVOUR_SYSCALL,
    FLAVOUR_SERVER,
} Flavour;

typedef struct {
    char *name;
    char *type;
} Arg;

typedef struct {
    char    *name;
    char    *return_type;
    Arg     *args;
    size_t   nargs;
    int      lvo;
    Flavour  flavour;
    char    *impl_symbol;
    bool     is_pad;
    int      ordinal;
    int      line_no;
} Function;

typedef struct {
    char     *library;       // ##library
    char     *base;          // ##base
    char     *base_type;     // ##base_type
    int       bias;          // ##bias  (0 = unset)
    char     *owner;         // ##owner
    char     *spdx;          // ##spdx

    char    **includes;      // ##include lines (each is "<header.h>" or "\"header.h\"")
    size_t    n_includes;
    size_t    cap_includes;

    Function *funcs;
    size_t    nfuncs;
    size_t    cap;
} Library;

// ============ Lexing helpers ================================================

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = 0;
    return s;
}

static bool is_ident_start(int c) { return c == '_' || isalpha(c); }
static bool is_ident_cont (int c) { return c == '_' || isalnum(c); }

static void skip_inline_ws(const char **p)
{
    while (**p == ' ' || **p == '\t') {
        (*p)++;
    }
}

// Read an identifier starting at *p. Returns malloc'd identifier on
// success and advances *p past it; returns NULL on no match.
static char *read_ident(const char **p)
{
    const char *s = *p;
    if (!is_ident_start((unsigned char)*s)) {
        return nullptr;
    }
    const char *q = s + 1;
    while (is_ident_cont((unsigned char)*q)) {
        q++;
    }
    char *out = xstrndup(s, (size_t)(q - s));
    *p = q;
    return out;
}

static bool name_starts_pad(const char *s)
{
    return strncmp(s, "_PAD", 4) == 0;
}

// ============ Parser =========================================================

// Parse a parenthesised arg list `(name1:type1, name2:type2, ...)` or `()`.
// Allocates *args, sets *nargs. Returns true on success.
static bool parse_args(const char **p, int line, Arg **args_out, size_t *nargs_out)
{
    if (**p != '(') {
        diag(line, "expected '(' to start args list");
        return false;
    }
    (*p)++;
    skip_inline_ws(p);

    Arg   *args = nullptr;
    size_t n = 0, cap = 0;

    if (**p == ')') {
        (*p)++;
        *args_out = nullptr;
        *nargs_out = 0;
        return true;
    }

    for (;;) {
        skip_inline_ws(p);
        char *name = read_ident(p);
        if (!name) {
            diag(line, "expected arg name in args list");
            return false;
        }
        skip_inline_ws(p);
        if (**p != ':') {
            diag(line, "expected ':' after arg name '%s'", name);
            free(name);
            return false;
        }
        (*p)++;
        skip_inline_ws(p);

        const char *tstart = *p;
        while (**p && **p != ',' && **p != ')') {
            (*p)++;
        }
        if (!**p) {
            diag(line, "unterminated args list (no closing ')')");
            free(name);
            return false;
        }
        const char *tend = *p;
        while (tend > tstart && isspace((unsigned char)tend[-1])) {
            tend--;
        }
        if (tend == tstart) {
            diag(line, "missing type for arg '%s'", name);
            free(name);
            return false;
        }

        if (n == cap) {
            cap = cap ? cap * 2 : 4;
            args = (Arg *)xrealloc(args, cap * sizeof(*args));
        }
        args[n].name = name;
        args[n].type = xstrndup(tstart, (size_t)(tend - tstart));
        n++;

        if (**p == ')') {
            (*p)++;
            break;
        }
        (*p)++; // ','
    }

    *args_out = args;
    *nargs_out = n;
    return true;
}

// Parse a function row from a (trimmed) line. Returns true on success and
// appends a Function entry to lib->funcs.
static bool parse_func(Library *lib, const char *line_buf, int line)
{
    Function f = { 0 };
    f.line_no = line;

    const char *p = line_buf;
    skip_inline_ws(&p);

    // 1. name
    f.name = read_ident(&p);
    if (!f.name) {
        diag(line, "expected function name at start of row");
        return false;
    }
    skip_inline_ws(&p);

    // 2. return type — everything until '('
    const char *rt_start = p;
    while (*p && *p != '(') {
        p++;
    }
    if (!*p) {
        diag(line, "expected '(' after return type for '%s'", f.name);
        return false;
    }
    const char *rt_end = p;
    while (rt_end > rt_start && isspace((unsigned char)rt_end[-1])) {
        rt_end--;
    }
    if (rt_end == rt_start) {
        diag(line, "missing return type for '%s'", f.name);
        return false;
    }
    f.return_type = xstrndup(rt_start, (size_t)(rt_end - rt_start));

    // 3. args
    if (!parse_args(&p, line, &f.args, &f.nargs)) {
        return false;
    }
    skip_inline_ws(&p);

    // 4. lvo (negative integer, multiple of 6)
    char *endp = nullptr;
    long lvo = strtol(p, &endp, 10);
    if (endp == p) {
        diag(line, "expected LVO offset (negative integer) for '%s'", f.name);
        return false;
    }
    if (lvo >= 0 || lvo % 6 != 0) {
        diag(line, "LVO %ld is not a negative multiple of 6", lvo);
        return false;
    }
    f.lvo = (int)lvo;
    p = endp;
    skip_inline_ws(&p);

    // 5. flavour
    char *flav = read_ident(&p);
    if (!flav) {
        diag(line, "expected flavour (local|syscall|server) for '%s'", f.name);
        return false;
    }
    if      (strcmp(flav, "local")   == 0) { f.flavour = FLAVOUR_LOCAL; }
    else if (strcmp(flav, "syscall") == 0) { f.flavour = FLAVOUR_SYSCALL; }
    else if (strcmp(flav, "server")  == 0) { f.flavour = FLAVOUR_SERVER; }
    else {
        diag(line, "unknown flavour '%s' (expected local|syscall|server)", flav);
        free(flav);
        return false;
    }
    free(flav);
    skip_inline_ws(&p);

    // 6. impl-symbol — rest of line, trimmed.
    const char *imp_start = p;
    const char *imp_end = imp_start + strlen(imp_start);
    while (imp_end > imp_start && isspace((unsigned char)imp_end[-1])) {
        imp_end--;
    }
    if (imp_end == imp_start) {
        diag(line, "missing impl-symbol for '%s'", f.name);
        return false;
    }
    f.impl_symbol = xstrndup(imp_start, (size_t)(imp_end - imp_start));

    f.is_pad = name_starts_pad(f.name);

    // Server-flavour impl-symbol must be `<gleas>::<tag>`.
    if (f.flavour == FLAVOUR_SERVER) {
        const char *cc = strstr(f.impl_symbol, "::");
        bool ok = cc && cc != f.impl_symbol && cc[2] != 0;
        if (ok) {
            // Validate gleas and tag are identifiers.
            for (const char *q = f.impl_symbol; q < cc; q++) {
                if (!is_ident_cont((unsigned char)*q)) {
                    ok = false;
                    break;
                }
            }
            for (const char *q = cc + 2; *q; q++) {
                if (!is_ident_cont((unsigned char)*q)) {
                    ok = false;
                    break;
                }
            }
        }
        if (!ok) {
            diag(line,
                 "server flavour requires '<gleas>::<tag>' impl-symbol; got '%s'",
                 f.impl_symbol);
            return false;
        }
    }

    if (lib->nfuncs == lib->cap) {
        lib->cap = lib->cap ? lib->cap * 2 : 16;
        lib->funcs = (Function *)xrealloc(lib->funcs,
                                          lib->cap * sizeof(*lib->funcs));
    }
    f.ordinal = (int)lib->nfuncs;
    lib->funcs[lib->nfuncs++] = f;
    return true;
}

// Parse a "##key value" directive. line_buf must start with "##".
static bool parse_directive(Library *lib, char *line_buf, int line)
{
    const char *p = line_buf + 2;
    skip_inline_ws(&p);
    char *key = read_ident(&p);
    if (!key) {
        diag(line, "expected directive name after '##'");
        return false;
    }
    skip_inline_ws(&p);

    // Value: rest of line, trimmed. line_buf is mutable (fgets buffer);
    // re-derive a non-const pointer at the same offset so trim() can
    // place its terminating NUL.
    char *val = trim(line_buf + (p - line_buf));
    if (!*val) {
        diag(line, "directive '##%s' has no value", key);
        free(key);
        return false;
    }

    bool ok = true;
    if      (strcmp(key, "library")   == 0) { lib->library   = xstrdup(val); }
    else if (strcmp(key, "base")      == 0) { lib->base      = xstrdup(val); }
    else if (strcmp(key, "base_type") == 0) { lib->base_type = xstrdup(val); }
    else if (strcmp(key, "owner")     == 0) { lib->owner     = xstrdup(val); }
    else if (strcmp(key, "spdx")      == 0) { lib->spdx      = xstrdup(val); }
    else if (strcmp(key, "include")   == 0) {
        // ##include <header.h> or ##include "header.h" — emitted into
        // the generated proto/<lib>.h so types referenced in args
        // resolve.
        if (lib->n_includes == lib->cap_includes) {
            lib->cap_includes = lib->cap_includes ? lib->cap_includes * 2 : 4;
            lib->includes = (char **)xrealloc(
                lib->includes, lib->cap_includes * sizeof(char *));
        }
        lib->includes[lib->n_includes++] = xstrdup(val);
    }
    else if (strcmp(key, "bias") == 0) {
        char *endp = nullptr;
        long b = strtol(val, &endp, 10);
        if (*endp != 0 || b <= 0) {
            diag(line, "##bias must be a positive integer; got '%s'", val);
            ok = false;
        } else if (b % 6 != 0) {
            diag(line, "##bias %ld must be a multiple of 6 (LIB_VECTSIZE)", b);
            ok = false;
        } else {
            lib->bias = (int)b;
        }
    } else if (strcmp(key, "pad_run") == 0) {
        // ##pad_run <count>: synthesize <count> consecutive _PAD rows at
        // the current ordinal. Each row gets impl_symbol
        // Croi_LvoUnimplemented and the LVO arithmetically next per
        // bias. Lets exec.conf elide long runs of unused slots without
        // hand-typing dozens of identical _PAD lines.
        if (!lib->bias) {
            diag(line, "##pad_run requires ##bias to be set first");
            ok = false;
        } else {
            char *endp = nullptr;
            long count = strtol(val, &endp, 10);
            if (*endp != 0 || count <= 0) {
                diag(line, "##pad_run requires positive count; got '%s'", val);
                ok = false;
            } else {
                for (long i = 0; i < count; i++) {
                    if (lib->nfuncs == lib->cap) {
                        lib->cap = lib->cap ? lib->cap * 2 : 16;
                        lib->funcs = (Function *)xrealloc(
                            lib->funcs, lib->cap * sizeof(*lib->funcs));
                    }
                    Function f = { 0 };
                    f.name        = xstrdup("_PAD");
                    f.return_type = xstrdup("void");
                    f.args        = nullptr;
                    f.nargs       = 0;
                    f.flavour     = FLAVOUR_LOCAL;
                    f.impl_symbol = xstrdup("Croi_LvoUnimplemented");
                    f.is_pad      = true;
                    f.ordinal     = (int)lib->nfuncs;
                    f.line_no     = line;
                    if (f.ordinal < 4) {
                        f.lvo = -((f.ordinal + 1) * 6);
                    } else {
                        f.lvo = -(lib->bias + (f.ordinal - 4) * 6);
                    }
                    lib->funcs[lib->nfuncs++] = f;
                }
            }
        }
    } else {
        diag(line, "unknown directive '##%s'", key);
        ok = false;
    }
    free(key);
    return ok;
}

static bool parse_conf(const char *path, Library *lib)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "lvo-gen: cannot open '%s': %s\n", path,
                strerror(errno));
        return false;
    }
    char buf[4096];
    int  line = 0;
    while (fgets(buf, sizeof(buf), fp)) {
        line++;
        // Strip optional trailing newline before trim so error messages
        // see the right text.
        size_t L = strlen(buf);
        if (L && buf[L - 1] == '\n') {
            buf[--L] = 0;
        }
        char *t = trim(buf);

        // Blank or single-# comment.
        if (!*t) {
            continue;
        }
        if (t[0] == '#' && t[1] != '#') {
            continue;
        }
        if (t[0] == '#' && t[1] == '#') {
            (void)parse_directive(lib, t, line);
            continue;
        }
        (void)parse_func(lib, t, line);
    }
    fclose(fp);
    return g_errors == 0;
}

// ============ Validator =====================================================

static void validate(Library *lib)
{
    if (!lib->library)   { diag(0, "missing ##library directive"); }
    if (!lib->base)      { diag(0, "missing ##base directive"); }
    if (!lib->base_type) { diag(0, "missing ##base_type directive"); }
    if (!lib->owner)     { diag(0, "missing ##owner directive"); }
    if (!lib->spdx)      { diag(0, "missing ##spdx directive"); }
    if (!lib->bias)      { diag(0, "missing ##bias directive"); }
    if (g_errors > 0) {
        return;
    }

    if (lib->nfuncs < 4) {
        diag(0, "library has %zu rows; need at least 4 reserved "
                "(Open/Close/Expunge/ExtFunc-or-_PAD*)",
             lib->nfuncs);
        return;
    }

    // Reserved slot names (positions 0..2 must match exactly).
    static const char *const reserved[3] = { "Open", "Close", "Expunge" };
    for (int i = 0; i < 3; i++) {
        const Function *f = &lib->funcs[i];
        if (strcmp(f->name, reserved[i]) != 0) {
            diag(f->line_no,
                 "reserved slot %d must be named '%s', got '%s'",
                 i, reserved[i], f->name);
        }
    }
    // Position 3 = ExtFunc or _PAD*.
    {
        const Function *f = &lib->funcs[3];
        if (strcmp(f->name, "ExtFunc") != 0 && !name_starts_pad(f->name)) {
            diag(f->line_no,
                 "reserved slot 3 must be 'ExtFunc' or '_PAD*', got '%s'",
                 f->name);
        }
    }
    // LVO arithmetic for reserved slots 0..3: -6, -12, -18, -24.
    for (int i = 0; i < 4; i++) {
        const Function *f = &lib->funcs[i];
        int expected = -((i + 1) * 6);
        if (f->lvo != expected) {
            diag(f->line_no,
                 "reserved slot %d ('%s') must have LVO %d, got %d",
                 i, f->name, expected, f->lvo);
        }
    }
    // User-defined slots: ordinal o >= 4 → lvo = -(bias + (o-4)*6).
    for (size_t o = 4; o < lib->nfuncs; o++) {
        const Function *f = &lib->funcs[o];
        int expected = -(lib->bias + ((int)o - 4) * 6);
        if (f->lvo != expected) {
            diag(f->line_no,
                 "ordinal %zu ('%s') must have LVO %d (bias=%d, "
                 "user-index=%zu), got %d",
                 o, f->name, expected, lib->bias, o - 4, f->lvo);
        }
    }

    // Unique names within the library (excluding _PAD* — pads are allowed
    // to repeat as different reserved slots; canonical V36+ libraries
    // sometimes have multiple).
    for (size_t i = 0; i < lib->nfuncs; i++) {
        if (lib->funcs[i].is_pad) {
            continue;
        }
        for (size_t j = i + 1; j < lib->nfuncs; j++) {
            if (lib->funcs[j].is_pad) {
                continue;
            }
            if (strcmp(lib->funcs[i].name, lib->funcs[j].name) == 0) {
                diag(lib->funcs[j].line_no,
                     "duplicate function name '%s' (also at line %d)",
                     lib->funcs[j].name, lib->funcs[i].line_no);
            }
        }
    }
}

// ============ Emitters =======================================================

// "graphics.library" → "graphics" into a static buffer.
static const char *lib_short(const char *libname)
{
    static char buf[64];
    size_t n = strlen(libname);
    static const char suf[] = ".library";
    size_t sufn = sizeof(suf) - 1;
    if (n > sufn && strcmp(libname + n - sufn, suf) == 0) {
        n -= sufn;
    }
    if (n >= sizeof(buf)) {
        n = sizeof(buf) - 1;
    }
    memcpy(buf, libname, n);
    buf[n] = 0;
    return buf;
}

static void to_upper_into(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = (char)toupper(c);
        // Replace non-ident-cont with '_' for include-guard hygiene.
        if (!is_ident_cont(c)) {
            dst[i] = '_';
        }
    }
    dst[i] = 0;
}

// Compute the unique CARA_IDX_/_LVO suffix for a function — `<name>` for
// non-pad rows, `PAD_<ordinal>` for _PAD rows. The leading underscore on
// `_PAD` is dropped so the emitted identifier doesn't end up with a
// reserved `__` (CARA_IDX_ + _PAD_n → CARA_IDX__PAD_n).
static const char *func_idx_suffix(const Function *f, char *buf, size_t cap)
{
    if (f->is_pad) {
        const char *p = f->name;
        while (*p == '_') {
            p++;
        }
        if (!*p) {
            p = "PAD";              // bare "_" or "____" → "PAD"
        }
        snprintf(buf, cap, "%s_%d", p, f->ordinal);
    } else {
        snprintf(buf, cap, "%s", f->name);
    }
    return buf;
}

static void emit_genhdr(FILE *out, const Library *lib, const char *kind)
{
    fprintf(out, "// SPDX-License-Identifier: %s\n", lib->spdx);
    fprintf(out, "// AUTOGENERATED by tools/lvo-gen from %s.conf — %s.\n",
            lib_short(lib->library), kind);
    fprintf(out, "// Do not edit; regenerate with `make`. Source of truth:\n"
                 "// tools/lvo-gen/%s.conf and docs/LVO.md.\n\n",
            lib_short(lib->library));
}

// proto/<short>.h — application-facing inline stubs.
static void emit_proto(const Library *lib, FILE *out)
{
    const char *shortn = lib_short(lib->library);
    char upper[64];
    to_upper_into(upper, sizeof(upper), shortn);

    emit_genhdr(out, lib, "proto/<lib>.h inline stubs");
    fprintf(out, "#ifndef PROTO_%s_H\n", upper);
    fprintf(out, "#define PROTO_%s_H\n\n", upper);
    fprintf(out, "#include <exec/libraries.h>\n");
    fprintf(out, "#include <exec/types.h>\n");
    for (size_t i = 0; i < lib->n_includes; i++) {
        fprintf(out, "#include %s\n", lib->includes[i]);
    }
    // Deliberately NOT including <%s/lvo.h>: a well-written V36+ program
    // includes several <proto/*.h> at once, and the runtime-ordinal /
    // canonical-_LVO constants are not namespaced per library (the _LVO*
    // names are verbatim-AmigaOS and must not be prefixed). So the stubs
    // below index the vec table with the literal ordinal (the symbolic
    // CARA_IDX_<name> is kept in a comment) and lvo.h is left for the
    // single-library link units (vec.c) that need the full constant set.
    fprintf(out, "\n");
    fprintf(out, "// Forward declaration only. The library's own type\n"
                 "// header (e.g. <%s/%sbase.h>) is what user code must\n"
                 "// include to dereference the base struct's fields.\n",
            shortn, shortn);
    fprintf(out, "struct %s;\n", lib->base_type);
    fprintf(out, "extern struct %s *%s;\n\n", lib->base_type, lib->base);

    char idx_buf[128];
    for (size_t i = 0; i < lib->nfuncs; i++) {
        const Function *f = &lib->funcs[i];
        // No user-callable stub for padding or the four reserved library
        // vectors (slots 0..3 — Open/Close/Expunge/ExtFunc). On V36+ those
        // are library-internal (the vec table still points at them, but
        // clients never call them through proto/<lib>.h); emitting them
        // here would also collide across libraries since every library
        // names them identically.
        if (f->is_pad || f->ordinal < 4) {
            continue;
        }
        const char *idx = func_idx_suffix(f, idx_buf, sizeof(idx_buf));

        if (f->flavour == FLAVOUR_SERVER) {
            fprintf(out,
                "// %s — server flavour: marshalling deferred (LVO.md §12.2).\n"
                "[[gnu::error(\"server-flavour stubs not yet wired "
                "(see docs/LVO.md §12.2)\")]]\n"
                "extern %s %s(", f->name, f->return_type, f->name);
            if (f->nargs == 0) {
                fprintf(out, "void");
            } else {
                for (size_t a = 0; a < f->nargs; a++) {
                    fprintf(out, "%s%s %s",
                            a ? ", " : "", f->args[a].type, f->args[a].name);
                }
            }
            fprintf(out, ");\n\n");
            continue;
        }

        // local / syscall — emit a real inline stub.
        bool returns_void = strcmp(f->return_type, "void") == 0;

        fprintf(out, "[[gnu::always_inline]]\n");
        fprintf(out, "static inline %s %s(", f->return_type, f->name);
        if (f->nargs == 0) {
            fprintf(out, "void");
        } else {
            for (size_t a = 0; a < f->nargs; a++) {
                fprintf(out, "%s%s %s",
                        a ? ", " : "", f->args[a].type, f->args[a].name);
            }
        }
        fprintf(out, ")\n{\n");

        fprintf(out, "    typedef %s (*_Cara_Fn)(", f->return_type);
        for (size_t a = 0; a < f->nargs; a++) {
            fprintf(out, "%s, ", f->args[a].type);
        }
        fprintf(out, "struct %s *);\n", lib->base_type);

        fprintf(out,
                "    _Cara_Fn _fn =\n"
                "        (_Cara_Fn)((void **)%s)[-1 - %d /* CARA_IDX_%s */];\n",
                lib->base, f->ordinal, idx);
        fprintf(out, "    %s_fn(", returns_void ? "" : "return ");
        for (size_t a = 0; a < f->nargs; a++) {
            fprintf(out, "%s, ", f->args[a].name);
        }
        fprintf(out, "%s);\n", lib->base);
        fprintf(out, "}\n\n");
    }

    fprintf(out, "#endif // PROTO_%s_H\n", upper);
}

// <short>/lvo.h — CARA_IDX_* + _LVO* constants.
static void emit_lvo_h(const Library *lib, FILE *out)
{
    const char *shortn = lib_short(lib->library);
    char upper[64];
    to_upper_into(upper, sizeof(upper), shortn);

    emit_genhdr(out, lib, "<lib>/lvo.h ordinal & canonical-LVO constants");
    fprintf(out, "#ifndef %s_LVO_H\n", upper);
    fprintf(out, "#define %s_LVO_H\n\n", upper);

    fprintf(out, "// CaraOS runtime ordinals — declaration order in %s.conf.\n",
            shortn);
    fprintf(out, "// These are what the inline stubs in <proto/%s.h> use to\n",
            shortn);
    fprintf(out, "// index into the negative-side function-pointer table.\n");
    char idx_buf[128];
    for (size_t i = 0; i < lib->nfuncs; i++) {
        const Function *f = &lib->funcs[i];
        const char *idx = func_idx_suffix(f, idx_buf, sizeof(idx_buf));
        fprintf(out, "constexpr int CARA_IDX_%-32s = %4d;\n", idx, f->ordinal);
    }

    fprintf(out, "\n// V36+ canonical LVO offsets — header constants and\n");
    fprintf(out, "// Phase 9 binary-translator lookup keys, NOT physical\n");
    fprintf(out, "// memory offsets at runtime on RV64. See docs/LVO.md §3.1.\n");
    for (size_t i = 0; i < lib->nfuncs; i++) {
        const Function *f = &lib->funcs[i];
        const char *idx = func_idx_suffix(f, idx_buf, sizeof(idx_buf));
        fprintf(out, "constexpr int _LVO%-32s = %5d;\n", idx, f->lvo);
    }

    fprintf(out, "\n#endif // %s_LVO_H\n", upper);
}

// <owner>/<libname>_vec.c — vec table + MakeLibrary tag array.
static void emit_vec_c(const Library *lib, FILE *out)
{
    const char *shortn = lib_short(lib->library);

    emit_genhdr(out, lib, "<lib>_vec.c — vec table + MakeLibrary tags");
    fprintf(out, "#include <cara/makelibrary.h>\n");
    fprintf(out, "#include <cara/types.h>\n");
    fprintf(out, "#include <exec/libraries.h>\n");
    fprintf(out, "#include <utility/tagitem.h>\n");
    for (size_t i = 0; i < lib->n_includes; i++) {
        fprintf(out, "#include %s\n", lib->includes[i]);
    }
    fprintf(out, "#include <%s/lvo.h>\n\n", shortn);
    fprintf(out, "struct %s;     // forward; full type lives in the\n"
                 "                // library's hand-written init unit.\n\n",
            lib->base_type);

    // Externs for impl symbols (skip server flavour — those impls live in
    // the receiving Gleas, not the library's link unit).
    fprintf(out, "// External impl symbols — link-time validation that\n"
                 "// the .conf points at real functions.\n");
    char idx_buf[128];
    for (size_t i = 0; i < lib->nfuncs; i++) {
        const Function *f = &lib->funcs[i];
        if (f->flavour == FLAVOUR_SERVER) {
            continue;
        }
        fprintf(out, "extern %s %s(", f->return_type, f->impl_symbol);
        for (size_t a = 0; a < f->nargs; a++) {
            fprintf(out, "%s%s", a ? ", " : "", f->args[a].type);
        }
        fprintf(out, "%sstruct %s *);\n",
                f->nargs ? ", " : "", lib->base_type);
    }
    fprintf(out, "\n");

    // Stub for server-flavour rows so the vec table has *something* to
    // point at until B-server lands. The stub returns 0 (or void).
    bool any_server = false;
    for (size_t i = 0; i < lib->nfuncs; i++) {
        if (lib->funcs[i].flavour == FLAVOUR_SERVER) {
            any_server = true;
            break;
        }
    }
    if (any_server) {
        fprintf(out, "// Placeholder for server-flavour rows until\n"
                     "// docs/LVO.md §12.2 marshalling lands. Returning\n"
                     "// 0 from a real call here is a bug; the application-\n"
                     "// facing proto/<lib>.h declares these with\n"
                     "// [[gnu::error]] so they fail at link time anyway.\n");
        fprintf(out, "extern void Croi_LvoServerStub(void);\n\n");
    }

    // Vec table.
    fprintf(out, "void *%s_lib_vec[] = {\n", shortn);
    for (size_t i = 0; i < lib->nfuncs; i++) {
        const Function *f = &lib->funcs[i];
        const char *idx = func_idx_suffix(f, idx_buf, sizeof(idx_buf));
        const char *tgt = (f->flavour == FLAVOUR_SERVER) ? "Croi_LvoServerStub"
                                                         : f->impl_symbol;
        fprintf(out, "    [CARA_IDX_%s] = (void *)%s,\n", idx, tgt);
    }
    fprintf(out, "};\n\n");

    // const (not constexpr) so the symbol has external linkage
    // and the boot-time MakeLibrary call site in another TU can
    // `extern const usize <lib>_lib_vec_count;` and read it.
    fprintf(out,
            "const usize %s_lib_vec_count =\n"
            "    sizeof(%s_lib_vec) / sizeof(%s_lib_vec[0]);\n\n",
            shortn, shortn, shortn);

    // Tag array — caller fills in MKL_VERSION / MKL_REVISION /
    // MKL_PRIVATE_SIZE / MKL_INIT_FN / MKL_SERVER_PORT_KOBJ before passing
    // to Croi_MakeLibrary.
    fprintf(out,
            "// Croi_MakeLibrary parameter tags. The library's hand-written\n"
            "// init unit overlays MKL_VERSION/REVISION/PRIVATE_SIZE/INIT_FN/\n"
            "// SERVER_PORT_KOBJ before the call. Zero values here mean\n"
            "// 'caller must set'; the kernel rejects construction if a\n"
            "// required tag is still zero (Croi_MakeLibrary semantics).\n");
    fprintf(out, "struct TagItem %s_make_args[] = {\n", shortn);
    fprintf(out, "    { MKL_NAME,         (IPTR)\"%s\" },\n", lib->library);
    fprintf(out, "    { MKL_VEC_TABLE,    (IPTR)%s_lib_vec },\n", shortn);
    fprintf(out, "    { MKL_VEC_COUNT,    (IPTR)%s_lib_vec_count },\n", shortn);
    fprintf(out, "    { MKL_VERSION,      0 },     // caller-overlay\n");
    fprintf(out, "    { MKL_REVISION,     0 },     // caller-overlay\n");
    fprintf(out, "    { MKL_PRIVATE_SIZE, 0 },     // caller-overlay\n");
    fprintf(out, "    { TAG_END,          0 },\n");
    fprintf(out, "};\n");
}

// aistreoir lookup-table fragment — `#include`-able list of
// `{ "<lib>", <lvo>, (void *)<impl_symbol> }` initializers, consumed
// by the aggregator pass that builds include/aistreoir/lvo_table.gen.h.
static void emit_aistreoir(const Library *lib, FILE *out)
{
    emit_genhdr(out, lib, "aistreoir Phase-9 lookup fragment");
    fprintf(out, "// Fragment — included into the aggregated\n"
                 "// include/aistreoir/lvo_table.gen.h via lvo-gen --aggregate.\n");
    fprintf(out, "// Each row maps (library_name, V36+_LVO_offset) → impl symbol\n"
                 "// for the Phase 9 68k → RV64 binary translator.\n\n");
    for (size_t i = 0; i < lib->nfuncs; i++) {
        const Function *f = &lib->funcs[i];
        if (f->flavour == FLAVOUR_SERVER) {
            // Server impls live in a receiving Gleas, not as a link
            // symbol — Phase 9 will need a different translation path.
            continue;
        }
        fprintf(out, "    { \"%s\", %d, (void *)%s },\n",
                lib->library, f->lvo, f->impl_symbol);
    }
}

// ============ Coverage report ===============================================

static int load_lib(const char *path, Library *lib); // defined below

static const char *flavour_str(Flavour f)
{
    switch (f) {
    case FLAVOUR_LOCAL:   return "local";
    case FLAVOUR_SYSCALL: return "syscall";
    case FLAVOUR_SERVER:  return "server";
    }
    return "?";
}

// Per-library stub-coverage tallies. An ABI slot is one of: reserved
// (ordinals 0..3 — the library-internal Open/Close/Expunge/ExtFunc
// vectors, never application-callable), impl (a real local/syscall
// body), server (declared, marshalling deferred), or stub (a per-LVO
// ##pad_run placeholder → Croi_LvoUnimplemented).
typedef struct {
    int    impl;
    int    server;
    int    stub;
    int    reserved;
    int    highest_lvo;
    int    highest_ord;
} CovStats;

static void cov_compute(const Library *lib, CovStats *s)
{
    *s = (CovStats){ 0 };
    for (size_t i = 0; i < lib->nfuncs; i++) {
        const Function *f = &lib->funcs[i];
        if (f->ordinal < 4) {
            s->reserved++;
        } else if (f->is_pad) {
            s->stub++;
        } else if (f->flavour == FLAVOUR_SERVER) {
            s->server++;
        } else {
            s->impl++;
        }
    }
    if (lib->nfuncs) {
        s->highest_lvo = lib->funcs[lib->nfuncs - 1].lvo;
        s->highest_ord = lib->funcs[lib->nfuncs - 1].ordinal;
    }
}

// Coverage % over the user-callable slots (excludes the 4 reserved).
static int cov_pct(const CovStats *s)
{
    int user = s->impl + s->server + s->stub;
    if (user == 0) {
        return 100;
    }
    return (s->impl + s->server) * 100 / user;
}

static void emit_coverage_lib(FILE *out, const Library *lib, const CovStats *s)
{
    fprintf(out, "## %s\n\n", lib->library);
    fprintf(out, "ABI surface declared through LVO %d (ordinal %d). "
                 "%d user-callable slots: **%d impl**, %d server, **%d stub**; "
                 "coverage **%d%%**.\n\n",
            s->highest_lvo, s->highest_ord, s->impl + s->server + s->stub,
            s->impl, s->server, s->stub, cov_pct(s));

    // Implemented rows (skip reserved 0..3 and pads).
    fprintf(out, "### Implemented (%d)\n\n", s->impl + s->server);
    if (s->impl + s->server == 0) {
        fprintf(out, "_none yet_\n\n");
    } else {
        fprintf(out, "| ord | LVO | name | flavour |\n");
        fprintf(out, "|----:|----:|------|---------|\n");
        for (size_t i = 0; i < lib->nfuncs; i++) {
            const Function *f = &lib->funcs[i];
            if (f->ordinal < 4 || f->is_pad) {
                continue;
            }
            fprintf(out, "| %d | %d | `%s` | %s |\n", f->ordinal, f->lvo,
                    f->name, flavour_str(f->flavour));
        }
        fprintf(out, "\n");
    }

    // Stub runs (consecutive ##pad_run slots, collapsed).
    fprintf(out, "### Unimplemented stub slots (%d)\n\n", s->stub);
    fprintf(out, "Per-LVO `##pad_run` placeholders (`Croi_LvoUnimplemented`): "
                 "the ABI slot exists so the vec index stays stable, but the "
                 "call is unimplemented until a row replaces it.\n\n");
    if (s->stub == 0) {
        fprintf(out, "_none — fully declared surface is implemented_\n\n");
    } else {
        fprintf(out, "| ord range | LVO range | count |\n");
        fprintf(out, "|-----------|-----------|------:|\n");
        size_t i = 0;
        while (i < lib->nfuncs) {
            const Function *f = &lib->funcs[i];
            if (f->ordinal < 4 || !f->is_pad) {
                i++;
                continue;
            }
            size_t j = i;
            while (j < lib->nfuncs && lib->funcs[j].is_pad &&
                   lib->funcs[j].ordinal >= 4) {
                j++;
            }
            fprintf(out, "| %d..%d | %d..%d | %d |\n", lib->funcs[i].ordinal,
                    lib->funcs[j - 1].ordinal, lib->funcs[i].lvo,
                    lib->funcs[j - 1].lvo, (int)(j - i));
            i = j;
        }
        fprintf(out, "\n");
    }
}

static int cmd_coverage(const char *out_path, int n_confs, char **conf_paths)
{
    FILE *out = fopen(out_path, "w");
    if (!out) {
        fprintf(stderr, "lvo-gen: cannot open '%s' for writing: %s\n", out_path,
                strerror(errno));
        return 1;
    }

    Library  *libs  = (Library *)xcalloc((size_t)n_confs, sizeof(Library));
    CovStats *stats = (CovStats *)xcalloc((size_t)n_confs, sizeof(CovStats));
    for (int i = 0; i < n_confs; i++) {
        g_errors = 0;
        if (load_lib(conf_paths[i], &libs[i]) != 0) {
            fprintf(stderr, "lvo-gen: coverage aborted — '%s' has %d error%s\n",
                    conf_paths[i], g_errors, g_errors == 1 ? "" : "s");
            fclose(out);
            return 1;
        }
        cov_compute(&libs[i], &stats[i]);
    }

    fprintf(out, "<!-- AUTOGENERATED by `tools/lvo-gen --coverage`. Do not edit by hand.\n");
    fprintf(out, "     Regenerate with `cmake --build build-host --target lvo-coverage`.\n");
    fprintf(out, "     Source of truth: the tools/lvo-gen/*.conf files + docs/LVO.md. -->\n\n");
    fprintf(out, "# CaraOS LVO stub-coverage report\n\n");
    fprintf(out, "Tracks how much of each library's V36+ ABI surface is really\n");
    fprintf(out, "implemented versus declared-but-stubbed. The policy (PHASE3.md\n");
    fprintf(out, "§7 Q4) is **per-LVO granularity**: every unimplemented slot is\n");
    fprintf(out, "an explicit `##pad_run` row pointing at `Croi_LvoUnimplemented`,\n");
    fprintf(out, "so the runtime ordinal of every real function stays frozen as\n");
    fprintf(out, "the surface fills in. A slot is one of:\n\n");
    fprintf(out, "- **impl** — a real `local`/`syscall` function body.\n");
    fprintf(out, "- **server** — declared; `PutMsg` marshalling deferred (LVO.md §12.2).\n");
    fprintf(out, "- **stub** — a per-LVO `##pad_run` placeholder, unimplemented.\n");
    fprintf(out, "- **reserved** — the 4 library-internal vectors "
                 "(Open/Close/Expunge/ExtFunc), not application-callable.\n\n");
    fprintf(out, "Coverage %% = (impl + server) / user-callable slots "
                 "(i.e. excluding the 4 reserved).\n\n");

    fprintf(out, "## Summary\n\n");
    fprintf(out, "| Library | ABI slots | impl | server | stub | reserved | coverage |\n");
    fprintf(out, "|---------|----------:|-----:|-------:|-----:|---------:|---------:|\n");
    for (int i = 0; i < n_confs; i++) {
        const CovStats *s = &stats[i];
        int slots = s->impl + s->server + s->stub + s->reserved;
        fprintf(out, "| %s | %d | %d | %d | %d | %d | %d%% |\n", libs[i].library,
                slots, s->impl, s->server, s->stub, s->reserved, cov_pct(s));
    }
    fprintf(out, "\n");

    for (int i = 0; i < n_confs; i++) {
        emit_coverage_lib(out, &libs[i], &stats[i]);
    }

    fclose(out);
    fprintf(stderr, "lvo-gen: coverage report written to %s (%d librar%s)\n",
            out_path, n_confs, n_confs == 1 ? "y" : "ies");
    return 0;
}

// ============ Entry point ===================================================

[[noreturn]] static void usage_and_exit(int code)
{
    fprintf(stderr,
            "usage:\n"
            "  lvo-gen --check <conf>\n"
            "  lvo-gen --emit <kind> <conf> <out>\n"
            "      <kind> is one of: proto | lvo | vec | aistreoir\n"
            "  lvo-gen --aggregate <out> <frag1> [<frag2> ...]\n"
            "      concatenate aistreoir fragments into the final\n"
            "      include/aistreoir/lvo_table.gen.h\n"
            "  lvo-gen --coverage <out.md> <conf1> [<conf2> ...]\n"
            "      write a Markdown stub-coverage report across the confs\n");
    exit(code);
}

static int cmd_aggregate(int n_frags, char **frag_paths, const char *out_path)
{
    FILE *out = fopen(out_path, "w");
    if (!out) {
        fprintf(stderr, "lvo-gen: cannot open '%s' for writing: %s\n",
                out_path, strerror(errno));
        return 1;
    }
    fprintf(out, "// SPDX-License-Identifier: BSD-2-Clause\n");
    fprintf(out, "// AUTOGENERATED by tools/lvo-gen --aggregate. Do not edit.\n");
    fprintf(out, "// Source: per-library .conf fragments aggregated below.\n");
    fprintf(out, "//\n");
    fprintf(out, "// To consume, a single Phase 9 translator unit declares\n");
    fprintf(out, "// `struct AistreoirLvoEntry { const char *library; int lvo;\n");
    fprintf(out, "//                            void *impl; };` and writes:\n");
    fprintf(out, "//\n");
    fprintf(out, "//   const struct AistreoirLvoEntry aistreoir_lvo_table[] = {\n");
    fprintf(out, "//       #include <aistreoir/lvo_table.gen.h>\n");
    fprintf(out, "//   };\n\n");

    for (int i = 0; i < n_frags; i++) {
        FILE *fp = fopen(frag_paths[i], "r");
        if (!fp) {
            fprintf(stderr, "lvo-gen: cannot open fragment '%s': %s\n",
                    frag_paths[i], strerror(errno));
            fclose(out);
            return 1;
        }
        fprintf(out, "    // ---- from %s ----\n", frag_paths[i]);
        char buf[1024];
        while (fgets(buf, sizeof(buf), fp)) {
            // Skip the fragment's auto-gen header lines (//-prefixed)
            // — we already wrote our own. Pass actual data rows through.
            char *t = buf;
            while (*t == ' ' || *t == '\t') {
                t++;
            }
            if (*t == '/' && t[1] == '/') {
                continue;
            }
            if (*t == '\n' || *t == 0) {
                continue;
            }
            fputs(buf, out);
        }
        fclose(fp);
    }
    fclose(out);
    return 0;
}

static int load_lib(const char *path, Library *lib)
{
    g_path = path;
    parse_conf(path, lib);
    if (g_errors == 0) {
        validate(lib);
    }
    return g_errors == 0 ? 0 : 1;
}

static int cmd_check(const char *path)
{
    Library lib = { 0 };
    int rc = load_lib(path, &lib);
    if (rc != 0) {
        fprintf(stderr, "lvo-gen: %d error%s\n",
                g_errors, g_errors == 1 ? "" : "s");
        return 1;
    }
    fprintf(stderr, "lvo-gen: %s ok (%zu rows, ##bias %d)\n",
            lib.library, lib.nfuncs, lib.bias);
    return 0;
}

static int cmd_emit(const char *kind, const char *conf, const char *out_path)
{
    Library lib = { 0 };
    if (load_lib(conf, &lib) != 0) {
        fprintf(stderr, "lvo-gen: %d error%s; emit aborted\n",
                g_errors, g_errors == 1 ? "" : "s");
        return 1;
    }
    FILE *out = fopen(out_path, "w");
    if (!out) {
        fprintf(stderr, "lvo-gen: cannot open '%s' for writing: %s\n",
                out_path, strerror(errno));
        return 1;
    }
    if      (strcmp(kind, "proto")     == 0) { emit_proto(&lib, out); }
    else if (strcmp(kind, "lvo")       == 0) { emit_lvo_h(&lib, out); }
    else if (strcmp(kind, "vec")       == 0) { emit_vec_c(&lib, out); }
    else if (strcmp(kind, "aistreoir") == 0) { emit_aistreoir(&lib, out); }
    else {
        fprintf(stderr, "lvo-gen: unknown emit kind '%s'\n", kind);
        fclose(out);
        return 2;
    }
    fclose(out);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage_and_exit(2);
    }
    if (strcmp(argv[1], "--check") == 0) {
        if (argc != 3) {
            usage_and_exit(2);
        }
        return cmd_check(argv[2]);
    }
    if (strcmp(argv[1], "--emit") == 0) {
        if (argc != 5) {
            usage_and_exit(2);
        }
        return cmd_emit(argv[2], argv[3], argv[4]);
    }
    if (strcmp(argv[1], "--aggregate") == 0) {
        if (argc < 4) {
            usage_and_exit(2);
        }
        return cmd_aggregate(argc - 3, &argv[3], argv[2]);
    }
    if (strcmp(argv[1], "--coverage") == 0) {
        if (argc < 4) {
            usage_and_exit(2);
        }
        return cmd_coverage(argv[2], argc - 3, &argv[3]);
    }
    usage_and_exit(2);
}
