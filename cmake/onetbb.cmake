set(oneTBB_VER "v2022.3.0")
set(oneTBB_REPO_LINK "https://github.com/uxlfoundation/oneTBB.git")
set(oneTBB_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/onetbb")
set(oneTBB_BUILD_DIR "${oneTBB_SOURCE_DIR}/build")
set(oneTBB_INSTALL_DIR "${oneTBB_BUILD_DIR}/install")

message(STATUS "Looking for oneTBB sources ...")

if(NOT EXISTS "${oneTBB_SOURCE_DIR}")
    message(STATUS "oneTBB sources not found, downloading ...")

    execute_process(
        COMMAND git clone "${oneTBB_REPO_LINK}" "${oneTBB_SOURCE_DIR}"
        RESULT_VARIABLE git_result
        ERROR_VARIABLE git_error
    )
    
    if(NOT git_result EQUAL 0)
        message(FATAL_ERROR "Failed to clone oneTBB: ${git_error}")
    endif()

    execute_process(
        COMMAND git checkout ${oneTBB_VER}
        WORKING_DIRECTORY "${oneTBB_SOURCE_DIR}"
        RESULT_VARIABLE git_checkout_result
        ERROR_VARIABLE git_checkout_error
    )
    
    if(NOT git_checkout_result EQUAL 0)
        message(FATAL_ERROR "Failed to checkout branch/tag: ${git_checkout_error}")
    endif()
endif()

function(build_onetbb)
file(MAKE_DIRECTORY ${oneTBB_BUILD_DIR})
file(MAKE_DIRECTORY ${oneTBB_INSTALL_DIR})

# Building for both Debug and Release configuration to allow
# running the Ray Tracer in Debug and Release mode
# VS will detect which version of shared library to use.
set(BUILD_CONFIGS "Debug;Release")

foreach(CONFIG ${BUILD_CONFIGS})
    if(${CONFIG} STREQUAL "Debug")
        set(CONFIG_CMAKE_BUILD_TYPE Debug)
    else()
        set(CONFIG_CMAKE_BUILD_TYPE Release)
    endif()

    message(STATUS "Building oneTBB in ${CONFIG_CMAKE_BUILD_TYPE} mode ...")

    execute_process(
        COMMAND ${CMAKE_COMMAND}
            "-G ${CMAKE_GENERATOR}"
            -DCMAKE_INSTALL_PREFIX=${oneTBB_INSTALL_DIR}
            -DCMAKE_BUILD_TYPE=${CONFIG_CMAKE_BUILD_TYPE}
            -DTBB_TEST=OFF
            -DTBB_EXAMPLES=OFF
            -DTBB_STRICT=OFF
            -DTBB4PY_BUILD=OFF
            -DTBBMALLOC_PROXY_BUILD=OFF
            -DTBB_USE_STATIC_LIB=ON
            ${oneTBB_SOURCE_DIR}
        WORKING_DIRECTORY ${oneTBB_BUILD_DIR}
        RESULT_VARIABLE res
        ERROR_VARIABLE err
    )

    if(NOT res EQUAL 0)
        message(FATAL_ERROR "oneTBB configure (${CONFIG}) failed: ${err}")
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} --build . --config ${CONFIG_CMAKE_BUILD_TYPE} --target install
        WORKING_DIRECTORY ${oneTBB_BUILD_DIR}
        RESULT_VARIABLE res
        ERROR_VARIABLE err
    )

    if(NOT res EQUAL 0)
        message(FATAL_ERROR "oneTBB build (${CONFIG}) failed: ${err}")
    endif()
endforeach()
endfunction()

if(EXISTS "${oneTBB_INSTALL_DIR}/include")
        message(STATUS "oneTBB found in ${oneTBB_INSTALL_DIR}")
else()
        message(STATUS "oneTBB not found, building ....")
        build_onetbb()
endif()

set(oneTBB_LIB_RELEASE "${oneTBB_INSTALL_DIR}/lib/tbb12.lib")
set(oneTBB_DLL_RELEASE "${oneTBB_INSTALL_DIR}/bin/tbb12.dll")

set(oneTBB_LIB_DEBUG "${oneTBB_INSTALL_DIR}/lib/tbb12_debug.lib")
set(oneTBB_DLL_DEBUG "${oneTBB_INSTALL_DIR}/bin/tbb12_debug.dll")

add_library(onetbb SHARED IMPORTED)

set_target_properties(onetbb PROPERTIES
    IMPORTED_IMPLIB "${oneTBB_LIB_RELEASE}"
    IMPORTED_LOCATION "${oneTBB_DLL_RELEASE}"

    IMPORTED_IMPLIB_DEBUG "${oneTBB_LIB_DEBUG}"
    IMPORTED_LOCATION_DEBUG "${oneTBB_DLL_DEBUG}"
)

target_include_directories(onetbb INTERFACE
    "${oneTBB_INSTALL_DIR}/include"
)

target_compile_definitions(onetbb INTERFACE TBB_USE_DYNAMIC_LINKING=1)

message(STATUS "oneTBB imported successfully")
