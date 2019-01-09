cmake_minimum_required(VERSION 3.1)
project(score_addon_jit LANGUAGES CXX)

# Based on the code in
# https://github.com/weliveindetail/JitFromScratch

# General initialization
score_common_setup()
find_package(Qt5 5.12 REQUIRED COMPONENTS QuickWidgets)

# Source files
set(HDRS
    JitCpp/AddonCompiler.hpp
    JitCpp/NodeCompiler.hpp
    JitCpp/EditScript.hpp
    JitCpp/ClangDriver.hpp
    JitCpp/JitModel.hpp
    JitCpp/Jit.hpp
    JitCpp/JitUtils.hpp
    JitCpp/JitPlatform.hpp
    JitCpp/JitExecutor.hpp
    JitCpp/ApplicationPlugin.hpp
    JitCpp/MetadataGenerator.hpp

    PluginSettings/PluginSettings.hpp
    PluginSettings/PluginSettingsModel.hpp
    PluginSettings/PluginSettingsPresenter.hpp
    PluginSettings/PluginSettingsView.hpp
    PluginSettings/PluginItemModel.hpp
    PluginSettings/FileDownloader.hpp
    score_addon_jit.hpp
)

set(SRCS
    JitCpp/AddonCompiler.cpp
    JitCpp/JitModel.cpp
    JitCpp/JitExecutor.cpp
    JitCpp/ClangDriver.cpp
    JitCpp/ApplicationPlugin.cpp

    PluginSettings/PluginSettingsModel.cpp
    PluginSettings/PluginSettingsPresenter.cpp
    PluginSettings/PluginSettingsView.cpp
    PluginSettings/PluginItemModel.cpp
    PluginSettings/FileDownloader.cpp
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

# LLVM definitions
separate_arguments(LLVM_DEFINITIONS)

# Project-specific definitions
target_include_directories(score_addon_jit PRIVATE
    ${LLVM_INCLUDE_DIRS}
)
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
set(CLANG_LIBS
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
    clangCrossTU
    clangIndex
)

llvm_map_components_to_libnames(LLVM_LIBS
  core
  executionengine
  native
  object
  orcjit
  runtimedyld
  support
)

if(WIN32)
  target_link_libraries(score_addon_jit PRIVATE -Wl,--start-group ${LLVM_LIBS} ${CLANG_LIBS} -Wl,--end-group mincore)
elseif(APPLE)
  target_link_libraries(score_addon_jit PRIVATE ${CLANG_LIBS} Polly PollyISL PollyPPCG ${LLVM_LIBS})
else()
  target_link_libraries(score_addon_jit PRIVATE ${CLANG_LIBS} ${LLVM_LIBS})
endif()

# Code generation
score_generate_command_list_file(${PROJECT_NAME} "${HDRS}")

# Link
target_link_libraries(${PROJECT_NAME} PUBLIC score_plugin_engine score_plugin_media score_plugin_library Qt5::QuickWidgets)

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
