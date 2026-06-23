# dhrystone — third-party port

`dhrystone.c` is **not CaraOS code**. It is the historical Dhrystone
benchmark, vendored **verbatim** to validate that real third-party
AmigaOS-era C source builds against the CaraOS SDK and runs, unedited
(Phase T, `docs/PORTS.md`).

- **Program:** Dhrystone Benchmark, version C/1.1 (1984).
- **Author:** Reinhold P. Weicker (CACM Vol 27 No 10, 10/84, p. 1013),
  translated from Ada to C by Rick Richardson.
- **Source:** fetched verbatim from
  `https://github.com/Keith-S-Thompson/dhrystone` (`v2.1/dhrystone.c`),
  itself an archive of the 1986 `net.sources` Usenet posting.
- **License:** Dhrystone is freely distributable / public-domain by long
  convention (Weicker placed it in the public domain; it is redistributed
  everywhere). The original posting carries no usage restriction. As
  third-party source it is **exempt from the CaraOS BSD-2 SPDX-per-file
  rule** (like `amiga_docs/`); the upstream header is left intact.

## What CaraOS supplies to build it (no source edits)

Dhrystone C/1.1 is K&R-era: it `#include`s only `<sys/types.h>` +
`<sys/times.h>`, leaves `printf`/`strcpy`/`strcmp`/`exit` implicitly
declared, hardcodes `#define LOOPS 50000` (no stdin) and `#define HZ 100`,
and times with `times(&tms)` reading `tms.tms_utime`. CaraOS builds it with:

- **`cara_port_flags`** — permissive port toolchain flags (`-std=gnu89`,
  warnings off), distinct from the strict `-std=c23 -Werror` CaraOS uses
  for its own code; this is how era-appropriate source compiles unedited.
- **`cara_user_libc`** — the userland libc (`<string.h>`/`<stdlib.h>`/
  `<stdio.h>`), plus `<sys/types.h>` (`time_t`/`clock_t`) and
  `<sys/times.h>` (`struct tms` + `times()`); `times()` reports
  centiseconds (HZ=100) from the monotonic `SYS_CurrentTime` clock, which
  is exactly the granularity the source's `HZ 100` assumes.
- **`libcara_user`** — `_start` (opens exec for `SysBase`, which Dhrystone
  itself never uses) + `memcpy`.

It uses no Amiga library API — it is a pure CPU benchmark — so it is the
minimal "real external program" first port: stdio + stdlib + the clock.
