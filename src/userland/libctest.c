// SPDX-License-Identifier: BSD-2-Clause
//
// libctest — exercises the Phase T userland libc (docs/PORTS.md §1.1) from
// a real U-mode Gleas, the way a ported app will: <string.h>, <stdlib.h>
// (malloc over AllocVec + conversions + qsort), <stdio.h> (snprintf + the
// console printf path), <ctype.h>. main() returns LIBCTEST_OK on success
// or the failing check's number; KERNEL_TEST(libctest_smoke) asserts the
// exit code.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIBCTEST_OK 0x1BC0

static int cmp_int(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

int main(void);

int main(void)
{
    char buf[128];

    // 1. string.h
    if (strlen("hello") != 5) {
        return 1;
    }
    if (!(strcmp("abc", "abc") == 0 && strcmp("abc", "abd") < 0 && strcmp("abd", "abc") > 0)) {
        return 2;
    }
    if (strchr("hello", 'l') != (char *)"hello" + 2 ||
        strrchr("hello", 'l') != (char *)"hello" + 3) {
        return 3;
    }
    if (!strstr("a needle here", "needle") || strstr("abc", "xyz")) {
        return 4;
    }
    {
        char a[8];
        strcpy(a, "abc");
        strcat(a, "de");
        if (strcmp(a, "abcde") != 0) {
            return 5;
        }
        char m[6] = { 1, 2, 3, 4, 5, 6 };
        memmove(m + 1, m, 3); // overlap: -> {1,1,2,3,5,6}
        if (!(m[0] == 1 && m[1] == 1 && m[2] == 2 && m[3] == 3 && m[4] == 5)) {
            return 6;
        }
        if (memcmp("abc", "abd", 3) >= 0 || memcmp("abc", "abc", 3) != 0) {
            return 7;
        }
    }

    // 2. snprintf formatting (the printf core)
    int n = snprintf(buf, sizeof buf, "%d|%5d|%-5d|%05d|%x|%X|%o|%c|%s|%p", -42, 7, 7, 42, 255, 255,
                     8, 'Z', "hi", (void *)0x10);
    if (strcmp(buf, "-42|    7|7    |00042|ff|FF|10|Z|hi|0x10") != 0) {
        return 8;
    }
    if (n != (int)strlen("-42|    7|7    |00042|ff|FF|10|Z|hi|0x10")) {
        return 9;
    }
    // truncation: return is the untruncated length; buffer is NUL-terminated.
    {
        char small[4];
        int r = snprintf(small, sizeof small, "abcdef");
        if (r != 6 || strcmp(small, "abc") != 0) {
            return 10;
        }
    }

    // 3. stdlib conversions
    if (atoi("123") != 123 || atoi("-7") != -7) {
        return 11;
    }
    if (strtol("ff", nullptr, 16) != 255 || strtol("0x1A", nullptr, 0) != 26 ||
        strtol("0755", nullptr, 0) != 493) {
        return 12;
    }
    if (abs(-5) != 5 || labs(-9L) != 9L) {
        return 13;
    }

    // 4. malloc / calloc / realloc
    {
        unsigned char *p = (unsigned char *)malloc(100);
        if (!p) {
            return 14;
        }
        for (int i = 0; i < 100; i++) {
            p[i] = (unsigned char)i;
        }
        for (int i = 0; i < 100; i++) {
            if (p[i] != (unsigned char)i) {
                return 15;
            }
        }
        free(p);

        int *z = (int *)calloc(16, sizeof(int));
        if (!z) {
            return 16;
        }
        for (int i = 0; i < 16; i++) {
            if (z[i] != 0) {
                return 17;
            }
        }
        free(z);

        char *s = (char *)malloc(8);
        strcpy(s, "abcdefg");
        char *g = (char *)realloc(s, 64);
        if (!g || strcmp(g, "abcdefg") != 0) {
            return 18;
        }
        free(g);
    }

    // 5. qsort
    {
        int arr[6] = { 5, 2, 8, 1, 9, 3 };
        qsort(arr, 6, sizeof(int), cmp_int);
        if (!(arr[0] == 1 && arr[1] == 2 && arr[2] == 3 && arr[3] == 5 && arr[4] == 8 &&
              arr[5] == 9)) {
            return 19;
        }
    }

    // 6. ctype
    if (!(isdigit('5') && !isdigit('x') && isalpha('x') && isspace(' ') && toupper('a') == 'A' &&
          tolower('Z') == 'z' && isxdigit('F'))) {
        return 20;
    }

    // 7. console printf (visible in the log; not asserted)
    printf("libctest: %d checks passed, ptr=%p\n", 20, (void *)buf);

    return LIBCTEST_OK;
}
