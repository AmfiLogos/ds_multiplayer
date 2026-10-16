/* The two JS files the engine runs before the wiki's own scripts, embedded at compile time. */
#include "sg_embed.h"
#pragma clang diagnostic ignored "-Wc23-extensions"
const unsigned char sg_prelude_js[] = {
#embed "sg_prelude.js"
, 0 };
const unsigned int sg_prelude_js_len = sizeof sg_prelude_js - 1;
const unsigned char sg_glue_js[] = {
#embed "sg_glue.js"
, 0 };
const unsigned int sg_glue_js_len = sizeof sg_glue_js - 1;
