# amiCalc — third-party port

`amicalc.c` is **not CaraOS code**. It is a real third-party AmigaOS GUI
application, vendored **verbatim** to validate that a well-written V36+ C
program builds against the CaraOS SDK and runs, unedited — the first GUI app
of Phase T (`docs/PORTS.md` §3, T.4).

- **Program:** amiCalc — a Workbench/Intuition scientific calculator.
- **Author / upstream:** `713avo` (© 2025 moneyland).
- **Source:** fetched verbatim from
  `https://github.com/713avo/amiCalc` — file `amicalc.c`, pinned at commit
  `b90c1fbe532af69c52d3dfbffff55cee42df302f` (2025-12-30).
- **License:** **MIT** (see the adjacent `LICENSE`, copied verbatim from
  upstream). As third-party source it keeps its own license and is **exempt
  from the CaraOS BSD-2 SPDX-per-file rule** (like `ports/dhrystone/` and
  `amiga_docs/`); the upstream file is left byte-for-byte intact. Only the
  CaraOS build glue (`CMakeLists.txt`) is BSD-2.

## Why it was chosen

amiCalc's GUI is **stock `intuition` + `graphics` only** — it `OpenWindow`s,
draws its own button grid + display with `RectFill`/`Text`, and runs a
`WaitPort`/`GetMsg` IDCMP mouse loop doing its own hit-testing. No
`gadtools`, no MUI, no slider/listview/palette, no `DrawImage`. That maps
directly onto the CaraOS GUI surface that already works, so the GUI validates
immediately; the substrate it forces is *numerical* — see below.

## What CaraOS supplies to build it (no source edits)

amiCalc `#include`s `<exec/types.h>`, `<intuition/intuition.h>`,
`<graphics/*.h>`, `<clib/*_protos.h>` (all CaraOS V36+ ABI headers + the
generated proto stubs) plus `<math.h>`, `<stdlib.h>`, `<stdio.h>`,
`<string.h>`. CaraOS builds it with:

- **`cara_port_flags`** — the permissive port toolchain (era-appropriate
  warnings off), distinct from the strict `-std=c23 -Werror` CaraOS uses for
  its own code.
- **`cara_user_libc`** — the userland libc, including the T.4.2 float
  support: `sprintf("%.15g", …)` for the display and `strtod` for input.
- **`cara_libm`** — the ~10 transcendentals it references (`sin cos tan asin
  acos atan exp log pow sqrt`) + `<math.h>` (`M_PI`, `M_E`), our own
  implementations (no third-party libm linked, `docs/PRINCIPLES.md` §2).
  T.4.3.
- **U-mode FPU** (T.4.1) — `double` math runs on the hardware FPU, enabled
  for U-mode and saved/restored across context switches.
- **`libcara_user`** — `_start` (opens exec for `SysBase`) + `memcpy`/`memset`.
