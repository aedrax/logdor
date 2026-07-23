# Benchmarks: label "bench", disabled unless -DLOGDOR_ENABLE_BENCH=ON so a
# plain `ctest` run stays fast. Corpora are generated once into the build tree
# via CTest fixtures; loggen is idempotent, so re-runs skip generation.

add_executable(bench_index bench_index.cpp)
target_link_libraries(bench_index PRIVATE Logdor::Core)

set(BENCH_DATA ${CMAKE_BINARY_DIR}/bench-data)

add_test(NAME bench.generate_1g
    COMMAND loggen --format plain --bytes 1G --seed 42 --long-line-every 100000
            --long-line-bytes 1M --empty-line-every 5000
            --out ${BENCH_DATA}/plain-1g.log)
add_test(NAME bench.index_1g
    COMMAND bench_index ${BENCH_DATA}/plain-1g.log
            --min-mbps 1000 --max-bytes-per-line 4.5 --check-cancel-ms 100)
set_tests_properties(bench.generate_1g PROPERTIES
    FIXTURES_SETUP benchdata_1g LABELS "bench" TIMEOUT 600)
set_tests_properties(bench.index_1g PROPERTIES
    FIXTURES_REQUIRED benchdata_1g LABELS "bench" TIMEOUT 600)

add_test(NAME bench.generate_5g
    COMMAND loggen --format plain --bytes 5G --seed 43 --long-line-every 100000
            --long-line-bytes 1M --empty-line-every 5000
            --out ${BENCH_DATA}/plain-5g.log)
add_test(NAME bench.index_5g
    COMMAND bench_index ${BENCH_DATA}/plain-5g.log
            --min-mbps 850 --max-bytes-per-line 4.5 --check-cancel-ms 100)
set_tests_properties(bench.generate_5g PROPERTIES
    FIXTURES_SETUP benchdata_5g LABELS "bench" TIMEOUT 3600)
set_tests_properties(bench.index_5g PROPERTIES
    FIXTURES_REQUIRED benchdata_5g LABELS "bench" TIMEOUT 3600)

if(NOT LOGDOR_ENABLE_BENCH)
    set_tests_properties(bench.generate_1g bench.index_1g
        PROPERTIES DISABLED TRUE)
endif()
if(NOT LOGDOR_ENABLE_BENCH OR NOT LOGDOR_BENCH_LARGE)
    set_tests_properties(bench.generate_5g bench.index_5g
        PROPERTIES DISABLED TRUE)
endif()
