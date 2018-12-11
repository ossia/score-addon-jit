cmake_minimum_required(VERSION 3.1)
project(score_addon_jit LANGUAGES CXX)

# Based on the code in
# https://github.com/weliveindetail/JitFromScratch

# General initialization
score_common_setup()
# Source files
set(HDRS
    JitCpp/JitModel.hpp
    JitCpp/Jit.hpp
    JitCpp/JitPlatform.hpp
    JitCpp/JitExecutor.hpp
    JitCpp/ApplicationPlugin.hpp
    score_addon_jit.hpp
)

set(SRCS
    JitCpp/JitModel.cpp
    JitCpp/JitExecutor.cpp
    JitCpp/ApplicationPlugin.cpp
    score_addon_jit.cpp
)

add_library(${PROJECT_NAME} ${SRCS} ${HDRS})

set(LLVM_BUILD_MAIN_SRC_DIR /opt/llvm)
find_package(LLVM REQUIRED PATHS ${LLVM_DIR})
if(LLVM_VERSION MATCHES "7.*" OR LLVM_VERSION MATCHES "8.*")
    message("-- LLVM version: ${LLVM_VERSION}")
else()
    message(FATAL_ERROR "LLVM Version incompatible")
endif()

if(NOT WIN32)
  include(${LLVM_BUILD_BINARY_DIR}/lib/cmake/clang/ClangTargets.cmake)

  if(EXISTS ${LLVM_BUILD_MAIN_SRC_DIR}/tools/clang)
      set(CLANG_SRC_DIR_PREFIX tools) # sources in-tree (default build)
  elseif(EXISTS ${LLVM_BUILD_MAIN_SRC_DIR}/../clang)
      set(CLANG_SRC_DIR_PREFIX ..) # sources out-of-tree (ENABLE_PROJECTS build)
  else()
      message(FATAL_ERROR "Cannot find Clang sources")
  endif()
endif()

# LLVM definitions
separate_arguments(LLVM_DEFINITIONS)
# Project-specific definitions
target_include_directories(score_addon_jit PRIVATE
    ${LLVM_INCLUDE_DIRS}
)
if(NOT WIN32)
  target_include_directories(score_addon_jit PRIVATE
    ${LLVM_BUILD_BINARY_DIR}/tools/clang/include
    ${LLVM_BUILD_MAIN_SRC_DIR}/${CLANG_SRC_DIR_PREFIX}/clang/include
  )
endif()

target_compile_definitions(score_addon_jit PRIVATE
    ${LLVM_DEFINITIONS}
)

target_compile_definitions(score_addon_jit
  PRIVATE
    SCORE_LLVM_VERSION="${LLVM_PACKAGE_VERSION}"
    SCORE_ROOT_SOURCE_DIR="${SCORE_ROOT_SOURCE_DIR}"
    SCORE_ROOT_BINARY_DIR="${SCORE_ROOT_BINARY_DIR}"
)

# Clang dependencies
target_link_libraries(score_addon_jit PRIVATE
    clangARCMigrate
    clangBasic
    clangCodeGen
    clangDriver
    clangFrontend
    clangFrontendTool
    clangRewriteFrontend
    clangARCMigrate
    clangStaticAnalyzerFrontend
    clangParse
    clangSerialization
    clangSema
    clangEdit
    clangStaticAnalyzerCheckers
    clangASTMatchers
    clangStaticAnalyzerCore
    clangAnalysis
    clangAST
    clangRewrite
    clangLex
)

if(WIN32)
  llvm_map_components_to_libnames(LLVM_LIBS
    core
    executionengine
    native
    object
    orcjit
    runtimedyld
    support
  )
  target_link_libraries(score_addon_jit PRIVATE ${LLVM_LIBS} mincore clangCrossTU clangIndex)
else()
  target_link_libraries(score_addon_jit PRIVATE LLVM)
endif()

# Code generation
score_generate_command_list_file(${PROJECT_NAME} "${HDRS}")

# Link
target_link_libraries(${PROJECT_NAME} PUBLIC score_plugin_engine score_plugin_media)

# Target-specific options
setup_score_plugin(${PROJECT_NAME})

# Things to install :
# - lib/clang/${LLVM_PACKAGE_VERSION}
# - libc++
# - Qt headers
# - MinGW headers
# install(DIRECTORY "${_srcDir}/"
#         DESTINATION include/score
#         COMPONENT Devel
#         FILES_MATCHING
#         PATTERN "*.hpp"
#         PATTERN ".git" EXCLUDE
#         PATTERN "tests" EXCLUDE
#         PATTERN "Tests" EXCLUDE
# )
