# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.20)
project(Pylir VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)

#===------------------------------------------------------------------------===#
# Project Build Options
#===------------------------------------------------------------------------===#

option(PYLIR_BUILD_TESTS "Build tests" ON)
option(PYLIR_BUILD_DOCS "Build documentation" OFF)
option(PYLIR_FUZZER "Build fuzzers" OFF)
option(PYLIR_COVERAGE "Compile with coverage" OFF)
set(PYLIR_SANITIZERS "" CACHE STRING "Compile with given sanitizers")

if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  option(PYLIR_ENABLE_ASSERTIONS "Enable assertions" OFF)
else ()
  option(PYLIR_ENABLE_ASSERTIONS "Enable assertions" ON)
endif ()

option(PYLIR_ENABLE_RTTI "Enable generation of RTTI" OFF)

find_package(LLVM 19.1 REQUIRED CONFIG)
set(MLIR_DIR ${LLVM_LIBRARY_DIR}/cmake/mlir)
set(LLD_DIR ${LLVM_LIBRARY_DIR}/cmake/lld)
find_package(MLIR REQUIRED CONFIG)
find_package(LLD REQUIRED CONFIG)

message(STATUS "Using MLIRConfig.cmake in: ${MLIR_DIR}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Using LLDConfig.cmake in: ${LLD_DIR}")
if ((PYLIR_ENABLE_RTTI AND LLVM_ENABLE_RTTI)
OR (NOT LLVM_ENABLE_RTTI AND NOT PYLIR_ENABLE_RTTI))
message(WARNING "Value of PYLIR_ENABLE_RTTI overwritten by LLVM_ENABLE_RTTI")
endif ()
# Must match LLVMs RTTI setting as it causes linker issues otherwise.
set(PYLIR_ENABLE_RTTI ${LLVM_ENABLE_RTTI} CACHE BOOL "" FORCE)

#===------------------------------------------------------------------------===#
# Dependencies, global options, include and link directories setup.
#===------------------------------------------------------------------------===#

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
# NO_POLICY_SCOPE makes policy changes done in the include affect the caller.
include(CMakePolicies NO_POLICY_SCOPE)
include(HandlePylirOptions)

add_global_compile_options()

add_subdirectory(3rdParty)

set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/bin)
set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/lib)
set(MLIR_BINARY_DIR ${CMAKE_BINARY_DIR})

list(APPEND CMAKE_MODULE_PATH ${MLIR_CMAKE_DIR})
list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
add_project_compile_options()
include(TableGen)
include(AddLLVM)
include(AddMLIR)
include(AddPylir)

set(LLVM_TABLEGEN_EXE "llvm-tblgen")
set(PYLIR_TABLEGEN_EXE "pylir-tblgen")

include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
include_directories(SYSTEM ${MLIR_INCLUDE_DIRS})
include_directories(SYSTEM ${LLD_INCLUDE_DIRS})
# Include directory where the various tablegen utilities place auto generated
# sources and headers.
include_directories(SYSTEM ${PROJECT_BINARY_DIR}/src)
include_directories(SYSTEM 3rdParty)
add_definitions(${LLVM_DEFINITIONS})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(PYLIR_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(PYLIR_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(PYLIR_TOOLS_DIR ${CMAKE_BINARY_DIR}/bin)

# Required by tests and for some build utilities.
find_package(Python3 3.6 COMPONENTS Interpreter REQUIRED)

if (PYLIR_BUILD_DOCS)
  add_subdirectory(docs)
endif ()

if (PYLIR_FUZZER)
  add_subdirectory(fuzzer)
endif ()

add_subdirectory(src)
add_subdirectory(tools)
if (PYLIR_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(unittests)
  add_subdirectory(test)
endif ()

