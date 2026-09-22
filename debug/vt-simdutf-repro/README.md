# Windows `ghostty-vt.dll`: access violation on multi-byte UTF-8

Repro + findings live here (`repro.c`). Scratch debugging material for the
ClawfficeOrg fork — not proposed for upstream merge as-is.

## Symptom

`ghostty_terminal_vt_write()` through the Windows `ghostty-vt.dll` dies
with `0xC0000005` on **any complete multi-byte UTF-8 sequence fed in a
single call**: 2-byte `é`, CJK, PUA/`U+FFFD` — all crash; pure ASCII always
works; the same bytes split across two calls survive.

```
> repro.exe
feed e-acute alone
(exit 0xC0000005)

> repro.exe split
feed e-acute split across calls
survived: split-eacute

> repro.exe ascii
feed ascii
survived: ascii
```

Verified against a Debug (`-Doptimize=Debug`) DLL built from this tree, and
against the older `a887df4`-era DLL built with Zig 0.15.2. Same signature
on both.

## Causal chain (all verified)

1. A complete sequence in one call takes the SIMD ground-state path:
   `Stream.nextSliceCapped` → `simd.vt.utf8DecodeUntilControlSeq`
   (`src/simd/vt.cpp` via `ghostty_simd_decode_utf8_until_control_seq`).
   Split calls stay in the scalar `nextUtf8` slow path and survive.
2. Tracing a Debug DLL with stderr prints shows the crash inside
   `simdutf::convert_utf8_to_utf32_with_errors` — before it returns, on a
   2-byte valid input. Even
   `simdutf::get_active_implementation()->name()` crashes the same way.
3. The faulting instruction (from a ProcDump full dump,
   `ghostty-vt.dll+0xD251`, thread `rcx=0x0`) is a null-`this` virtual call:
   `mov rax, [rcx]` / `call [rax]` with `rcx == 0`. simdutf's
   active-implementation pointer is NULL in the DLL.
4. ASCII input never reaches simdutf (ASCII fast path), which is why
   ASCII-only usage — and the existing test suite — never notice.
5. String scan of the binaries (same sources, toolchain, machine):
   `ghostty-test.exe` contains `westmere`/`fallback` kernel names;
   `ghostty-vt.dll` contains neither (only `haswell` detector strings).
   The scalar `fallback` kernel is unconditional in simdutf — its absence
   means the linker discarded the kernel objects when linking the shared
   lib, leaving simdutf's dispatch table dangling.

## Why upstream suites stay green

- `zig build test` (stream 181/181, terminal 2102/2104, decode 93/93 on
  Windows, incl. the 10k-case `decode simd matches scalar` differential
  test) links simdutf intact into the test binary.
- Additionally, no C-API-level test feeds a complete multi-byte printable
  in a single `vt_write`: the one multibyte C-API test
  (`vt_write split combining mark after base at right edge`) splits the
  sequence across two calls — the surviving shape.

## Root cause (confirmed)

The `.CRT` section of the shipped DLL contains a single pointer where
dozens of C++ dynamic initializers should be registered — the Windows
shared-lib link discards simdutf's `.CRT$XCU` initializer sections
(nothing roots them). Ghostty builds vendored simdutf with
`-DSIMDUTF_NO_LIBCXX`, which flips simdutf into
`SIMDUTF_USE_STATIC_INITIALIZATION=1` (translation-unit-scope statics
with dynamic initializers). With the initializers gone,
`available_implementations_instance` and `active_implementation_instance`
stay zero-filled: `avail=0, active=null` at runtime (traced), and the
first conversion dies in the null-`this` call above. The test EXE runs
its initializers normally, which is why the suites stay green.

## Fix (verified)

One flag in `pkg/simdutf/build.zig`: `-DSIMDUTF_USE_STATIC_INITIALIZATION=0`.
This restores simdutf's default (function-local statics, self-initializing
thread-safely on first call — no load-time initialization required). With
it, the same repro passes every case (`avail=1, active non-null`). This
branch carries that patch (uncommitted state: see `pkg/simdutf/build.zig`;
the `win-vt-simdutf-fix-1.3` branch backports it to the 1.3-era tree for
consumers pinned to the old C API).

Workaround until fixed: build with `-Dsimd=false` (verified: all repro
cases pass; scalar Zig decoder handles everything, just slower).

## Suggested upstream fix

Adopt the `SIMDUTF_USE_STATIC_INITIALIZATION=0` override above for the
Windows shared-lib build. Separately worth investigating why `.CRT$XCU`
initializers are dropped from the DLL link while the EXE link keeps
them — that gap is the deeper bug and may bite other C++ dependencies
too.
