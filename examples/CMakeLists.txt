# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Summon Software Labs.

foreach(example compare_basic ingest_pipeline persistence_recovery)
  add_executable(pathobs_example_${example} ${example}.cpp)
  target_link_libraries(pathobs_example_${example}
                        PRIVATE PathObservatory::core pathobs_project_options)
endforeach()
