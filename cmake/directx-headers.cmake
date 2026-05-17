set(DirectXHeaders_VER "v1.619.1")
set(DirectXHeaders_REPO_LINK "https://github.com/microsoft/DirectX-Headers.git")
set(DirectXHeaders_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/directx-headers")

message(STATUS "Looking for DirectX Headers ...")

if(NOT EXISTS "${DirectXHeaders_SOURCE_DIR}")
    message(STATUS "DirectX Headers not found, downloading ...")

    execute_process(
        COMMAND git clone "${DirectXHeaders_REPO_LINK}" "${DirectXHeaders_SOURCE_DIR}"
        RESULT_VARIABLE git_result
        ERROR_VARIABLE git_error
    )
    
    if(NOT git_result EQUAL 0)
        message(FATAL_ERROR "Failed to clone DirectX Headers: ${git_error}")
    endif()

    execute_process(
        COMMAND git checkout ${DirectXHeaders_VER}
        WORKING_DIRECTORY "${DirectXHeaders_SOURCE_DIR}"
        RESULT_VARIABLE git_checkout_result
        ERROR_VARIABLE git_checkout_error
    )
    
    if(NOT git_checkout_result EQUAL 0)
        message(FATAL_ERROR "Failed to checkout branch/tag: ${git_checkout_error}")
    endif()
endif()

message(STATUS "Configuring DirectX Headers ...")

set(DirectXHeaders_INCLUDE_DIR "${DirectXHeaders_SOURCE_DIR}/include")

add_library(DirectX-Headers INTERFACE IMPORTED)

target_include_directories(DirectX-Headers
    INTERFACE
        "${DirectXHeaders_INCLUDE_DIR}"
)

set_target_properties(DirectX-Headers PROPERTIES 
    INTERFACE_COMPILE_FEATURES ""
)

message(STATUS "DirectX Headers imported successfully")
