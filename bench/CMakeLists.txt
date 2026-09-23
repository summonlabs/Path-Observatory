# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Summon Software Labs.

foreach(benchmark bench_compare bench_ingest)
  add_executable(pathobs_${benchmark} ${benchmark}.cpp)
  target_link_libraries(pathobs_${benchmark}
                        PRIVATE PathObservatory::core pathobs_project_options)
endforeach()
