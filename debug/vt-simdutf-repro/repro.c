// Repro: ghostty-vt.dll (Windows) access-violates on any complete
// multi-byte UTF-8 sequence fed in a single vt_write call.
//
// Build the DLL first:
//   zig build -Demit-lib-vt=true -Doptimize=Debug -Demit-xcframework=false \
//       -Dapp-runtime=none --prefix out
// Then compile (MSVC import lib + headers under out/):
//   zig cc -o repro.exe repro.c -Iout/include out/lib/ghostty-vt.lib
// And run with the DLL beside the exe:
//   copy out/bin/ghostty-vt.dll .
//   repro.exe            (crashes, exit 0xC0000005)
//   repro.exe split      (survives)
//   repro.exe ascii      (survives)
//
// See README.md in this directory for the full findings.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ghostty/vt/terminal.h"

static GhosttyTerminal new_term(uint16_t cols, uint16_t rows) {
    GhosttyTerminal term = 0;
    if (ghostty_terminal_new(0, &term, cols, rows) != GHOSTTY_SUCCESS)
        return 0;
    return term;
}

static int case_single_eacute(void) {
    GhosttyTerminal t = new_term(80, 24);
    if (!t) {
        printf("new failed\n");
        return 1;
    }
    // e-acute: complete 2-byte UTF-8 sequence, one call.
    const uint8_t e[] = {0xC3, 0xA9};
    printf("feed e-acute alone\n");
    fflush(stdout);
    ghostty_terminal_vt_write(t, e, sizeof(e));
    printf("survived: single-eacute\n");
    ghostty_terminal_free(t);
    return 0;
}

static int case_split_eacute(void) {
    GhosttyTerminal t = new_term(80, 24);
    if (!t) {
        printf("new failed\n");
        return 1;
    }
    // Same bytes, split across two calls (scalar slow path).
    const uint8_t a[] = {0xC3}, b[] = {0xA9};
    printf("feed e-acute split across calls\n");
    fflush(stdout);
    ghostty_terminal_vt_write(t, a, sizeof(a));
    ghostty_terminal_vt_write(t, b, sizeof(b));
    printf("survived: split-eacute\n");
    ghostty_terminal_free(t);
    return 0;
}

static int case_ascii(void) {
    GhosttyTerminal t = new_term(80, 24);
    if (!t) {
        printf("new failed\n");
        return 1;
    }
    const uint8_t h[] = "hello";
    printf("feed ascii\n");
    fflush(stdout);
    ghostty_terminal_vt_write(t, h, sizeof(h) - 1);
    printf("survived: ascii\n");
    ghostty_terminal_free(t);
    return 0;
}

static int case_xfffd(void) {
    GhosttyTerminal t = new_term(80, 24);
    if (!t) {
        printf("new failed\n");
        return 1;
    }
    // "x" + U+FFFD, one call.
    const uint8_t b[] = {'x', 0xEF, 0xBF, 0xBD};
    printf("feed x+U+FFFD\n");
    fflush(stdout);
    ghostty_terminal_vt_write(t, b, sizeof(b));
    printf("survived: xfffd\n");
    ghostty_terminal_free(t);
    return 0;
}

int main(int argc, char **argv) {
    const char *which = argc > 1 ? argv[1] : "single-eacute";
    if (!strcmp(which, "split"))
        return case_split_eacute();
    if (!strcmp(which, "ascii"))
        return case_ascii();
    if (!strcmp(which, "xfffd"))
        return case_xfffd();
    return case_single_eacute();
}
