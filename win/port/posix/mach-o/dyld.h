/* shim: no dyld on Windows; image walks find nothing (Steam lookups report "not available") */
#pragma once
#include <stdint.h>
static inline uint32_t _dyld_image_count(void) { return 0; }
static inline const char *_dyld_get_image_name(uint32_t i) { (void)i; return 0; }
