// SPDX-License-Identifier: BSD-2-Clause
//
// Syscall dispatcher. Trap entry has already saved a full frame on the
// dying user task's kstack and S-mode is running with SUM=1, so user
// pointers handed in via a1/a2/... are accessible directly. A real
// implementation would copy_from_user with bounds + page-fault recovery;
// Tier 3 first cut just reads them inline and clamps lengths.

#include <cara/log.h>
#include <cara/sched.h>
#include <cara/syscall.h>
#include <cara/trap.h>
#include <cara/types.h>

static volatile bool g_user_exited = false;
static volatile i64  g_user_exit_status = 0;

bool Croi_Syscall_UserExited(void)         { return g_user_exited; }
i64  Croi_Syscall_UserExitStatus(void)     { return g_user_exit_status; }
void Croi_Syscall_ResetUserExit(void)
{
    g_user_exited = false;
    g_user_exit_status = 0;
}

// Bounded copy from user-VA to a kernel buffer. Caller passes max so
// we never run forever on a malformed pointer.
static u32 copy_user_str(char *dst, u32 dst_cap, const char *user_src,
                         u32 user_max)
{
    if (dst_cap == 0) {
        return 0;
    }
    u32 i = 0;
    u32 cap = dst_cap - 1;
    while (i < cap && i < user_max && user_src[i] != 0) {
        dst[i] = user_src[i];
        i++;
    }
    dst[i] = 0;
    return i;
}

static i64 sys_log_write(i64 level, const char *user_tag, const char *user_msg,
                         i64 msg_len)
{
    if (msg_len < 0 || msg_len > (i64)CARA_LOG_MSG_LEN) {
        return -CARA_EINVAL;
    }
    char tag[CARA_LOG_TAG_LEN + 1] = { 0 };
    (void)copy_user_str(tag, sizeof(tag), user_tag, CARA_LOG_TAG_LEN);

    char msg[CARA_LOG_MSG_LEN];
    u32 n = (u32)msg_len < CARA_LOG_MSG_LEN - 1 ? (u32)msg_len
                                                : CARA_LOG_MSG_LEN - 1;
    for (u32 i = 0; i < n; i++) {
        msg[i] = user_msg[i];
    }
    msg[n] = 0;

    Croi_Log((LogLevel)level, tag, "%s", msg);
    return 0;
}

[[noreturn]] static void sys_exit(i64 status)
{
    g_user_exited = true;
    g_user_exit_status = status;
    LOG_INFO("user", "exit status=%lld", status);
    Croi_TaskExit();
}

i64 Croi_Syscall_Dispatch(struct TrapFrame *frame)
{
    i64 num = (i64)frame->x[17];        // a7
    i64 a0 = (i64)frame->x[10];
    i64 a1 = (i64)frame->x[11];
    i64 a2 = (i64)frame->x[12];
    i64 a3 = (i64)frame->x[13];

    switch (num) {
    case SYS_LOG_WRITE:
        return sys_log_write(a0, (const char *)(uptr)a1,
                             (const char *)(uptr)a2, a3);
    case SYS_EXIT:
        sys_exit(a0);
        // unreachable
    default:
        LOG_WARN("sysc", "unknown syscall a7=%lld", num);
        return -CARA_ENOTFOUND;
    }
}
