// Force-include <cstdio> before Qt headers to work around MSVC 2022
// /Zc:__cplusplus issue where std::snprintf is not found in qtesttostring.h.
#pragma once
#include <cstdio>
