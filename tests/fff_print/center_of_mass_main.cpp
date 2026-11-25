#define CATCH_CONFIG_NO_WINDOWS_SEH
#include <catch_main.hpp>

// Core model code uses NanoSVG, whose implementation normally lives in the GUI.
// Provide it here so these processor tests do not need the GUI library.
#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
