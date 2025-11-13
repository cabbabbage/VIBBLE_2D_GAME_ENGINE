// Single translation unit to provide stb_image and stb_image_write implementations.
// This resolves linker errors for stbi_* symbols used across the engine.

// Enable implementations exactly once in the build.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

// Do NOT disable GIF support: SourceConfigPanel uses stbi_load_gif_from_memory.
// Ensure we include the local copies bundled under ENGINE/utils.
#include "stb_image.h"
#include "stb_image_write.h"

