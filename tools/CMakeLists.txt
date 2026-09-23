# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Summon Software Labs.

add_executable(pathobs pathobs_main.cpp)
target_link_libraries(pathobs PRIVATE PathObservatory::core
                      $<BUILD_INTERFACE:pathobs_project_options>)

add_executable(pathobs_node pathobs_node_main.cpp)
target_link_libraries(pathobs_node PRIVATE PathObservatory::core
                      $<BUILD_INTERFACE:pathobs_project_options>)
