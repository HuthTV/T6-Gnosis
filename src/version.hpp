#pragma once
// Release version: the build date (YYYY.MM.DD), written to build\version.inc by
// build.bat for release builds ("build.bat release"); "dev" otherwise.
#if __has_include("version.inc")
#include "version.inc"
#endif
#ifndef GNOSIS_VERSION
#define GNOSIS_VERSION "dev"
#endif
