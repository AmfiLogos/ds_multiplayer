# Vendored: QuickJS 2026-06-04 (Fabrice Bellard, Charlie Gordon), MIT licence (LICENSE in this folder)

- Source: https://bellard.org/quickjs/quickjs-2026-06-04.tar.xz
- sha256 of the tarball: b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a
- Files copied unmodified: the engine library only (quickjs.c/.h, quickjs-atom.h, quickjs-opcode.h, cutils, dtoa,
  libregexp, libunicode, list.h) + LICENSE, VERSION, readme.txt, Changelog. Not copied: quickjs-libc (no file or
  OS access from the wiki scripts), qjs/qjsc, tests.
- Built by custom/fights/build.sh with -D_GNU_SOURCE -DCONFIG_VERSION="2026-06-04" -fwrapv (as its Makefile).
