// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

// Block minwindef.h min/max macros to prevent <algorithm> conflict
#define NOMINMAX

#include <array>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

#include <d2d1.h>
#include <d3d11_2.h>
#include <dwrite.h>
#include <dxgi.h>
#include <VersionHelpers.h>

#include <gsl/pointers>
#include <gsl/span>
#include <gsl/gsl_util>
#include <wil/com.h>
#include <wil/result_macros.h>
#include <wil/stl.h>
#include <wil/win32_helpers.h>

// Dynamic Bitset (optional dependency on LibPopCnt for perf at bit counting)
// Variable-size compressed-storage header-only bit flag storage library.
#include <dynamic_bitset.hpp>

// Chromium Numerics (safe math)
#pragma warning(push)
#pragma warning(disable : 4100) // '...': unreferenced formal parameter
#pragma warning(disable : 26812) // The enum type '...' is unscoped. Prefer 'enum class' over 'enum' (Enum.3).
#include <base/numerics/safe_math.h>
#pragma warning(pop)

#include "til.h"
