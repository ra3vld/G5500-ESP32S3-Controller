// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once

// Starting point before the first SemVer release tag (vMAJOR.MINOR.PATCH).
#define PROJECT_BASE_VERSION "1.0.0"
#define PROJECT_BASE_COMMIT "4e1020de9e5302cd24708595afe8197088f84ea2"

#if __has_include("build_identity.hpp")
#include "build_identity.hpp"
#else
namespace forkrot::firmware {
inline constexpr char VERSION[] = PROJECT_BASE_VERSION "+unknown";
inline constexpr char COMMIT[] = "unknown";
inline constexpr bool DIRTY = false;
}
#endif
