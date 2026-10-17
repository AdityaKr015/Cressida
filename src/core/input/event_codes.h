#pragma once

#if __has_include(<linux/input-event-codes.h>)
#include <linux/input-event-codes.h>
#elif __has_include(<dev/evdev/input-event-codes.h>)
#include <dev/evdev/input-event-codes.h>
#else
#error "No evdev input event codes header is available"
#endif
