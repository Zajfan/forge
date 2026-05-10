// stb_impl.cpp
// One-and-only place that defines STB implementation macros.
// All other TUs that include stb_image.h must NOT define these macros.

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION

#include <stb_image.h>
#include <stb_image_write.h>
