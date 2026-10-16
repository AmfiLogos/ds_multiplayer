// mp_dlsym.h - dlsym(RTLD_DEFAULT, name) that also finds symbols of images loaded RTLD_LOCAL (Mac only).
// The mod loader opens its modules (multiplayer.dylib, custom-modes*.dylib) with RTLD_LOCAL, and RTLD_DEFAULT does not
// search those images: the multiplayer mod's lookups of its own functions (Teams, menu, FE start) and Custom Modes'
// lookups into multiplayer came back NULL under the loader (gate13 2026-10-16). The rig injects with DYLD_INSERT
// (global), so the rig never saw it. Every file that calls dlsym(RTLD_DEFAULT, ...) gets this through rig.h (or includes
// it): the call text stays the same (win/port/gen_def.py reads `dlsym(RTLD_DEFAULT, "name")` for the Windows exports).
// Order: RTLD_DEFAULT, then this image, then every other non-system image (handles cached; rebuilt when the image count
// changes). Other handles pass straight through.
#pragma once
#ifdef __APPLE__
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <string.h>
static inline void *mp_dlsym_(void *h, const char *name) {
    void *p = (dlsym)(h, name);
    if (p || h != RTLD_DEFAULT || !name) return p;
    static void *self, *img[64]; static int nimg = -1; static uint32_t seen;
    if (!self) { Dl_info i; if (dladdr((const void *)&mp_dlsym_, &i) && i.dli_fname) self = dlopen(i.dli_fname, RTLD_NOLOAD | RTLD_LAZY); }
    if (self && (p = (dlsym)(self, name))) return p;
    uint32_t n = _dyld_image_count();
    if (n != seen) {                                    /* app and mod images only (not /System, /usr/lib) */
        for (int k = 0; k < nimg; k++) if (img[k]) dlclose(img[k]);
        nimg = 0; seen = n;
        for (uint32_t k = 0; k < n && nimg < 64; k++) {
            const char *f = _dyld_get_image_name(k);
            if (!f || !strncmp(f, "/System/", 8) || !strncmp(f, "/usr/lib/", 9)) continue;
            void *hk = dlopen(f, RTLD_NOLOAD | RTLD_LAZY); if (hk && hk != self) img[nimg++] = hk; else if (hk) dlclose(hk);
        }
    }
    for (int k = 0; k < nimg; k++) if ((p = (dlsym)(img[k], name))) return p;
    return NULL;
}
#define dlsym(h, n) mp_dlsym_((h), (n))
#endif
