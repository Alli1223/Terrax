// The launcher's own version. Local/dev builds keep "dev", which disables the
// in-app launcher self-update (so you don't get prompted to "update" a local
// build). The release CI overwrites this file with the tag being built, e.g.
//   #define TERRAX_LAUNCHER_VERSION "v0.2.0"
// so a released launcher knows its version and can offer to update itself.
#pragma once

#ifndef TERRAX_LAUNCHER_VERSION
#define TERRAX_LAUNCHER_VERSION "dev"
#endif
