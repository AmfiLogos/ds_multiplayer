/* bld_glue.js, embedded at compile time (same way as custom/fights/sg_embed.c). */
#include "bld_js.h"
#pragma clang diagnostic ignored "-Wc23-extensions"
const unsigned char bld_glue_js[] = {
#embed "bld_glue.js"
, 0 };
const unsigned int bld_glue_js_len = sizeof bld_glue_js - 1;
