#pragma once

// The port's version, shown in the window's title, at the top of the log and
// in the F11 menu. The build scripts write version_number.h (not kept in
// git) from the VERSION.txt in the game folder, one higher at every build.
#if __has_include("version_number.h")
#include "version_number.h"
#endif
#ifndef COD3_VERSION
#define COD3_VERSION "dev"
#endif

#define COD3_WIDEN2(text) L##text
#define COD3_WIDEN(text) COD3_WIDEN2(text)
#define COD3_VERSION_W COD3_WIDEN(COD3_VERSION)
