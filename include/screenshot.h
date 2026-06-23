#pragma once
#include <string>

// ---------------------------------------------------------------------------
// Screenshots (Track H1)
// ---------------------------------------------------------------------------
// A tiny, dependency-free PNG writer plus a framebuffer-capture helper. PNG is
// emitted with uncompressed (stored) zlib blocks — no zlib/libpng needed — so
// the game (and the headless screenshot tour) can drop real .png files that the
// rendering-review pass can open directly.

// Write an 8-bit RGB image (row-major, top-to-bottom, 3 bytes/pixel) as a PNG.
// Returns true on success.
bool writePNG(const std::string& path, int width, int height,
              const unsigned char* rgb);

// Read back the current GL draw buffer (width × height) and save it as a PNG
// under `screenshots/`, flipping it the right way up. `tag` is folded into the
// filename (e.g. "tier3"). Returns the path written, or "" on failure. Must be
// called with a current GL context — e.g. from the main loop just before the
// buffer swap.
std::string saveFramebufferPNG(int width, int height, const std::string& tag = "");
