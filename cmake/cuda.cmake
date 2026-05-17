find_package(CUDAToolkit QUIET)

if(NOT CUDAToolkit_FOUND)
    message(FATAL_ERROR "CUDA compiler not found, to build this application please install it")
endif()

message(STATUS "CUDA compiler found: ${CUDAToolkit_VERSION}")

enable_language(CUDA)

set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_ARCHITECTURES 75 80 86)
set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)
