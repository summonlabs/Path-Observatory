# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Summon Software Labs.
#
# An independent consumer of the installed Path Observatory package. It shares
# nothing with the Path Observatory build tree except the installed package: no
# source, no include path, no target from the parent project.

cmake_minimum_required(VERSION 3.25)

project(PathObservatoryDownstream LANGUAGES CXX)

find_package(PathObservatory 1.0 REQUIRED CONFIG)

# The consumer is built with the same sanitizer instrumentation as the package
# it links against, when the package was built with one.
if(PATHOBS_ASAN)
  if(MSVC)
    add_compile_options(/fsanitize=address)
    add_link_options(/fsanitize=address)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  endif()
endif()

add_executable(downstream_consumer main.cpp)
target_link_libraries(downstream_consumer PRIVATE PathObservatory::core)
set_target_properties(downstream_consumer PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

message(STATUS "downstream consumer using PathObservatory " ${PathObservatory_VERSION})
