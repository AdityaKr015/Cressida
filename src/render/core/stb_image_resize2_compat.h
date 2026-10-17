#pragma once

#if __has_include(<stb/stb_image_resize2.h>)
#include <stb/stb_image_resize2.h>
#elif __has_include(<stb_image_resize2.h>)
#include <stb_image_resize2.h>
#else
#error "stb_image_resize2.h not found"
#endif
