
cmake_minimum_required(VERSION 3.3 FATAL_ERROR)

project(chenxification)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 14)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

list(APPEND CMAKE_PREFIX_PATH "${LLVM_ROOT}/share/llvm/cmake")

link_directories(/usr/local/lib)
find_package(LLVM 4.0 REQUIRED CONFIG)

add_definitions(${LLVM_DEFINITIONS})

include_directories(${LLVM_INCLUDE_DIRS})
#include_directories(${CMAKE_SOURCE_DIR}/include)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

include(HandleLLVMOptions)
include(AddLLVM)

add_subdirectory(chenxification)
