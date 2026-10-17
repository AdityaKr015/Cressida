#pragma once

#if __has_include(<stb/stb_image_write.h>)
#include <stb/stb_image_write.h>
#elif __has_include(<stb_image_write.h>)
#include <stb_image_write.h>
#else
#error "stb_image_write.h not found"
#endif
