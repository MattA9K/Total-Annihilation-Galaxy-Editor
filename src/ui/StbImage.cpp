// The one place that compiles the stb_image implementation. It decodes PNG only.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include "third_party/stb_image.h"
