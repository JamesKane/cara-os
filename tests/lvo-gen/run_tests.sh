#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
#
# Smoke test for tools/lvo-gen — Phase B5 in the LVO.md plan.
#
# Args: $1 = absolute path to lvo-gen binary
#       $2 = absolute path to this directory (tests/lvo-gen)
#
# Positive: runs lvo-gen --emit on tests/lvo-gen/sample.conf for each
# of {proto, lvo, vec, aistreoir}, diffs against tests/lvo-gen/golden.
# Negative: runs lvo-gen --check on each tests/lvo-gen/bad/*.conf,
# asserts non-zero exit and that stderr contains a phrase the test
# .conf was constructed to trigger.

set -u

LVO_GEN="${1:?usage: $0 <lvo-gen-binary> <test-dir>}"
TDIR="${2:?usage: $0 <lvo-gen-binary> <test-dir>}"

if [[ ! -x "$LVO_GEN" ]]; then
    echo "lvo-gen binary not executable: $LVO_GEN" >&2
    exit 1
fi
if [[ ! -d "$TDIR" ]]; then
    echo "test dir not a directory: $TDIR" >&2
    exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail=0
pass() { echo "  ok  $1"; }
fails() { echo "  FAIL $1"; fail=$((fail + 1)); }

echo "lvo-gen golden tests against $TDIR/sample.conf:"
for kind in proto lvo vec aistreoir; do
    case "$kind" in
        proto)     out="$TMP/sample-proto.h"     ; gold="$TDIR/golden/sample-proto.h" ;;
        lvo)       out="$TMP/sample-lvo.h"       ; gold="$TDIR/golden/sample-lvo.h" ;;
        vec)       out="$TMP/sample-vec.c"       ; gold="$TDIR/golden/sample-vec.c" ;;
        aistreoir) out="$TMP/sample-aistreoir.inc"; gold="$TDIR/golden/sample-aistreoir.inc" ;;
    esac
    "$LVO_GEN" --emit "$kind" "$TDIR/sample.conf" "$out" >/dev/null 2>"$TMP/stderr"
    rc=$?
    if [[ $rc -ne 0 ]]; then
        fails "emit $kind failed (rc=$rc)"
        sed 's/^/    /' "$TMP/stderr"
        continue
    fi
    if ! diff -u "$gold" "$out" >"$TMP/diff" 2>&1; then
        fails "$kind output differs from golden"
        sed 's/^/    /' "$TMP/diff"
        continue
    fi
    pass "$kind matches golden"
done

echo "lvo-gen negative cases:"
# macOS default bash is 3.2 — no associative arrays. Use a case statement.
expected_needle() {
    case "$1" in
        non-monotonic-lvo.conf)   echo "must have LVO -30" ;;
        wrong-reserved-name.conf) echo "reserved slot 0 must be named 'Open'" ;;
        server-without-tag.conf)  echo "server flavour requires" ;;
        missing-directive.conf)   echo "missing ##base directive" ;;
        *)                         echo "" ;;
    esac
}
for conf in "$TDIR"/bad/*.conf; do
    name="$(basename "$conf")"
    "$LVO_GEN" --check "$conf" >"$TMP/stdout" 2>"$TMP/stderr"
    rc=$?
    if [[ $rc -eq 0 ]]; then
        fails "$name: expected non-zero exit, got 0"
        continue
    fi
    needle="$(expected_needle "$name")"
    if [[ -z "$needle" ]]; then
        fails "$name: no expected-needle entry in run_tests.sh"
        continue
    fi
    if ! grep -qF "$needle" "$TMP/stderr"; then
        fails "$name: stderr missing expected '$needle'"
        sed 's/^/    /' "$TMP/stderr"
        continue
    fi
    pass "$name rejected ($needle)"
done

if [[ $fail -ne 0 ]]; then
    echo "lvo-gen tests: $fail failure(s)" >&2
    exit 1
fi
echo "lvo-gen tests: ok"
