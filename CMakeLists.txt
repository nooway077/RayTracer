cmake_minimum_required(VERSION 4.0.0)

message(STATUS "Configuring from ${CMAKE_CURRENT_SOURCE_DIR}")

# Fail early if not Win32 & MSVC (target platform is Windows).
if(NOT WIN32 AND MSVC)
    message(FATAL_ERROR "This project is Windows only and requires MSVC.")
endif()

# Enable filters in Visual Studio for grouping files.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Remove the default MinSizeRel and RelWithDebInfo build configurations.
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

# Make Windows headers use wide character set.
add_definitions(/D UNICODE /D _UNICODE)

# Define the project.
project(RayTracer
	VERSION 1.0.0
	LANGUAGES CXX CUDA
)

# Set global compiler options.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    # 1. Add compile-time flags for Release only.
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /arch:AVX2 /GL")

    # 2. Add link-time flags for Release only.
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG /INCREMENTAL:NO")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG /INCREMENTAL:NO")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Module path for custom CMake modules.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Add custom modules.
include(directx-headers)
include(cuda)
include(onetbb)

# Demo application executable.
add_executable(RayTracerDemo WIN32)

# Add source files and headers.
target_sources(RayTracerDemo PRIVATE 
    "sources/stdafx.cpp"
    "sources/Main.cpp"
    "sources/Win32Application.cpp"
    "sources/DXApp.cpp"
    "sources/UILayer.cpp"
    "sources/RayTracerDemo.cpp"

    "include/stdafx.h"
    "include/Win32Application.h"
    "include/DXApp.h"
    "include/Helpers.h"
    "include/UILayer.h"
    "include/StepTimer.h"
    "include/GPUTimer.h"
    "include/RayTracerDemo.h"

    "sources/Camera.cpp"
    "sources/BVHBuilder.cpp"
    "sources/RayTracerCore.cpp"
    "sources/CPURenderer.cpp"
    "sources/Scene.cpp"

    "include/CPUTypes.h"
    "include/CPURenderer.h"
    "include/AABB.h"
    "include/Ray.h"
    "include/Material.h"
    "include/Sphere.h"
    "include/Quad.h"
    "include/Light.h"
    "include/Camera.h"
    "include/BVH.h"
    "include/Scene.h"
    "include/RayTracerCore.h"
    "include/BVHBuilder.h"

    "include/RenderConfig.h"
    "include/Telemetry.h"
    "sources/Telemetry.cpp"

    "include/CudaRenderer.h"
    "sources/CudaRenderer.cpp"
    "include/CudaTypes.cuh"
    "sources/RayTracerCuda.cu"
    "include/RayTracerCuda.cuh"
)

# Filters for Visual Studio to group files.
source_group("Source Files\\Util" FILES 
    "sources/DXApp.cpp"
    "sources/UILayer.cpp"
    "sources/Win32Application.cpp"
    "sources/Telemetry.cpp"
)

source_group("Header Files\\Util" FILES 
    "include/Win32Application.h"
    "include/DXApp.h"
    "include/Helpers.h"
    "include/UILayer.h"
    "include/StepTimer.h"
    "include/GPUTimer.h"
    "include/Telemetry.h"
)

source_group("Header Files\\Core\\Common" FILES 
    "include/AABB.h"
    "include/Ray.h"
    "include/Material.h"
    "include/Sphere.h"
    "include/Quad.h"
    "include/Light.h"
    "include/Camera.h"
    "include/BVH.h"
    "include/Scene.h"
)

source_group("Header Files\\Core\\CPU" FILES 
    "include/CPUTypes.h"
    "include/CPURenderer.h"
    "include/RayTracerCore.h"
)

source_group("Header Files\\Core" FILES 
    "include/BVHBuilder.h"
    "include/RenderConfig.h"
)

source_group("Source Files\\Core\\Common" FILES 
    "sources/Camera.cpp"
    "sources/Scene.cpp"
)

source_group("Source Files\\Core\\CPU" FILES 
    "sources/RayTracerCore.cpp"
    "sources/CPURenderer.cpp"
)

source_group("Source Files\\Core\\CUDA" FILES 
    "sources/CudaRenderer.cpp"
    "sources/RayTracerCuda.cu"
)

source_group("Source Files\\Core" FILES 
    "sources/BVHBuilder.cpp"
)

source_group("Header Files\\Core\\CUDA" FILES 
    "include/CudaTypes.cuh"
    "include/CudaRenderer.h"
    "include/RayTracerCuda.cuh"
)

# Enable precompiled header (stdafx.h)
# https://learn.microsoft.com/en-us/cpp/build/creating-precompiled-header-files?view=msvc-170
# /Yc on stdafx.cpp, /Yu on all others.
set_source_files_properties("sources/stdafx.cpp" PROPERTIES COMPILE_FLAGS "/Yc\"stdafx.h\"")
get_target_property(SOURCES RayTracerDemo SOURCES)
list(FILTER SOURCES INCLUDE REGEX "\\.cpp$")      # Filter for .cpp files
list(REMOVE_ITEM SOURCES "sources/stdafx.cpp")    # Exclude stdafx.cpp

# Use precompiled hearder when compiling source files.
foreach(file IN LISTS SOURCES)
    set_source_files_properties(${file} PROPERTIES COMPILE_FLAGS "/Yu\"stdafx.h\"")
endforeach()

# Set PCH file for target in Visual Studio.
set_target_properties(RayTracerDemo PROPERTIES
    VS_GLOBAL_PrecompiledHeaderFile "stdafx.h"
    VS_GLOBAL_PrecompiledHeaderOutputFile "$(IntDir)$(TargetName).pch"
)

# Set interfaces.
target_include_directories(RayTracerDemo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Link system-wide and third-party libraries.
target_link_libraries(RayTracerDemo
    PRIVATE
    DirectX-Headers
    CUDA::cudart
    d2d1.lib    # Direct2D
    dwrite.lib  # DirectWrite
    d3d11.lib   # Core D3D11 (for D3D11On12)
    d3d12.lib   # Core D3D12
    dxgi.lib    # DXGI interface
    dxguid.lib
    dwrite.lib
    ole32.lib
    uuid.lib
    onetbb
)

# Add shaders.
set(DXC_EXE "dxc")
set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")
set(SHADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders")

file(GLOB SHADER_SOURCES "${SHADER_DIR}/*.hlsl")
file(GLOB SHADER_INCLUDES "${SHADER_DIR}/*.hlsli")

# Helper to compile one shader at a time.
function(compile_hlsl in_file out_file entry profile)
  add_custom_command(
    OUTPUT ${out_file}
    COMMAND ${DXC_EXE} -T ${profile} -E ${entry} -Fo ${out_file} -I ${SHADER_DIR} ${in_file}
    DEPENDS ${in_file} ${SHADER_INCLUDES}
    COMMENT "Compiling shader ${in_file} -> ${out_file}"
    VERBATIM
  )
  # Expose generated file to caller scope.
  set(${ARGV3} ${out_file} PARENT_SCOPE)
endfunction()

set(GENERATED_SHADERS "")

compile_hlsl("${SHADER_DIR}/FullscreenPass.hlsl" "${SHADER_OUT}/FullscreenPassPS.cso" "PSMain" "ps_6_5")
list(APPEND GENERATED_SHADERS "${SHADER_OUT}/FullscreenPassPS.cso")

compile_hlsl("${SHADER_DIR}/FullscreenPass.hlsl" "${SHADER_OUT}/FullscreenPassVS.cso" "VSMain" "vs_6_5")
list(APPEND GENERATED_SHADERS "${SHADER_OUT}/FullscreenPassVS.cso")

# Custom target to build all shaders.
add_custom_target(Shaders ALL DEPENDS ${GENERATED_SHADERS})
add_dependencies(RayTracerDemo Shaders)

# Add shader sources (.hlsl/.hlsli) to target for IDE grouping.
target_sources(RayTracerDemo PRIVATE 
    ${SHADER_SOURCES}
    ${SHADER_INCLUDES}
)

# Mark .hlsl files as non-source.
foreach(file ${SHADER_SOURCES})
	set_source_files_properties(${file} PROPERTIES HEADER_FILE_ONLY TRUE)
endforeach()

# Organize shader sources in VS Solution Explorer.
source_group("Assets/Shaders" FILES ${SHADER_SOURCES} ${SHADER_INCLUDES})

# Custom post build commands.
add_custom_command(TARGET RayTracerDemo POST_BUILD

    # 1. Copy the compiled shaders (*.cso) to the binary directory.
    COMMAND ${CMAKE_COMMAND} -E copy ${GENERATED_SHADERS} $<TARGET_FILE_DIR:RayTracerDemo>
    COMMAND_EXPAND_LISTS

    # 2. Copy debug/release tbb dll into the binary directory.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<$<CONFIG:Debug>:${oneTBB_DLL_DEBUG}>$<$<CONFIG:Release>:${oneTBB_DLL_RELEASE}>"
    $<TARGET_FILE_DIR:RayTracerDemo>
)

# Set startup project in Visual Studio to RayTracerDemo.
set_property(DIRECTORY ${PROJECT_ROOT_DIR} PROPERTY VS_STARTUP_PROJECT RayTracerDemo)
