// reagentlive version, the one place it is written (CMake reads it from here).
// 0.x: the API and the wire layout can still change between releases. Agent
// and client are built from the same revision; the layout check in the
// handshake refuses a mismatched pair, whatever the version says.
#pragma once

#define REAGENTLIVE_VERSION_MAJOR 0
#define REAGENTLIVE_VERSION_MINOR 2
#define REAGENTLIVE_VERSION_PATCH 0

#define REAGENTLIVE_VERSION_TEXT_(x) #x
#define REAGENTLIVE_VERSION_TEXT(x) REAGENTLIVE_VERSION_TEXT_(x)

namespace reagentlive {
inline constexpr const char* Version = REAGENTLIVE_VERSION_TEXT(REAGENTLIVE_VERSION_MAJOR) "."
    REAGENTLIVE_VERSION_TEXT(REAGENTLIVE_VERSION_MINOR) "." REAGENTLIVE_VERSION_TEXT(REAGENTLIVE_VERSION_PATCH);
}
