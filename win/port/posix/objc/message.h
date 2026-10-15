// objc/message.h stand-in (Windows build): objc_msgSend is only ever cast and called on nil here (see runtime.h).
#pragma once
#include "runtime.h"
static void objc_msgSend(void) {}
