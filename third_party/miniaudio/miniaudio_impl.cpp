// The single translation unit that compiles the miniaudio implementation.
// Keep MINIAUDIO_IMPLEMENTATION defined ONLY here. miniaudio is third-party, so
// its own warnings are silenced rather than fixed.
#if defined(_MSC_VER)
#  pragma warning(push, 0)
#endif

#define MA_NO_ENCODING            // playback only — we never write audio files
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#if defined(_MSC_VER)
#  pragma warning(pop)
#endif
