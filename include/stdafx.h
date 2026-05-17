// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently.
// https://learn.microsoft.com/en-us/cpp/build/creating-precompiled-header-files?view=msvc-170

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers.
#endif

#ifndef NOMINMAX
#define NOMINMAX						// Use the C++ standard templated min/max.
#endif

#include <windows.h>

#include "directx/d3dx12.h"			    // From DirectX-Headers | Must be included before Win SDK's D3D12. https://github.com/microsoft/DirectX-Headers#use-on-windows
#include <d3d12.h>
#include <d2d1_3.h>
#include <dwrite.h>
#include <d3d11on12.h>
#include <dxgi1_6.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>

#include <wrl.h>
#include <shellapi.h>
#include <assert.h>

#include <list>
#include <string>
#include <vector>
#include <sstream>
#include <memory>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <thread>
#include <limits>
#include <cstdint>
