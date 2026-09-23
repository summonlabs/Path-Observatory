# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Summon Software Labs.

add_executable(pathobs_tests
  test_main.cpp
  test_foundation.cpp
  test_domain.cpp
  test_determinism.cpp
  test_runtime_persistence.cpp
  test_transport_process.cpp
  test_concurrency.cpp)

target_link_libraries(pathobs_tests PRIVATE PathObservatory::core pathobs_project_options)

# The generated header carries the absolute paths of the sibling executables so
# the independent process transport tests can launch them. file(GENERATE) is
# used because configure_file cannot expand a target file generator expression.
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_paths.hpp" CONTENT
"#ifndef PATHOBS_TEST_PATHS_HPP
#define PATHOBS_TEST_PATHS_HPP
#define PATHOBS_NODE_EXECUTABLE \"$<TARGET_FILE:pathobs_node>\"
#define PATHOBS_CLI_EXECUTABLE \"$<TARGET_FILE:pathobs>\"
#endif
")
target_include_directories(pathobs_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
add_dependencies(pathobs_tests pathobs_node pathobs)

# The suite contains no timeouts of any kind: a hang is a defect and must show
# up as a hang rather than as a flaky pass.
add_test(NAME pathobs.suite COMMAND pathobs_tests)

# ---------------------------------------------------------------------------
# Install and downstream find_package proof.
#
# The install runs as a CTest fixture so the downstream consumer configures
# against a real installed package rather than against the build tree.
# ---------------------------------------------------------------------------

set(PATHOBS_TEST_INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/install-prefix")

add_test(NAME pathobs.install
         COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR}
                 --prefix ${PATHOBS_TEST_INSTALL_PREFIX} --config $<CONFIG>)
set_tests_properties(pathobs.install PROPERTIES FIXTURES_SETUP pathobs_install)

add_test(NAME pathobs.downstream
         COMMAND ${CMAKE_COMMAND}
                 -DPATHOBS_PREFIX=${PATHOBS_TEST_INSTALL_PREFIX}
                 -DPATHOBS_DOWNSTREAM_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/downstream
                 -DPATHOBS_DOWNSTREAM_BINARY=${CMAKE_CURRENT_BINARY_DIR}/downstream-build
                 -DPATHOBS_GENERATOR=${CMAKE_GENERATOR}
                 -DPATHOBS_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                 -DPATHOBS_DOWNSTREAM_ASAN=${PATHOBS_ENABLE_ASAN}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/downstream/run_downstream.cmake)
set_tests_properties(pathobs.downstream PROPERTIES FIXTURES_REQUIRED pathobs_install)
