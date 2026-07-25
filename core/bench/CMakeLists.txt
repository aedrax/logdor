# Benchmarks: label "bench", disabled unless -DLOGDOR_ENABLE_BENCH=ON so a
# plain `ctest` run stays fast. Corpora are generated once into the build tree
# via CTest fixtures; loggen is idempotent, so re-runs skip generation.

add_executable(bench_index bench_index.cpp)
target_link_libraries(bench_index PRIVATE Logdor::Core)

add_executable(bench_filter bench_filter.cpp)
target_link_libraries(bench_filter PRIVATE Logdor::Core)

add_executable(bench_query bench_query.cpp)
target_link_libraries(bench_query PRIVATE Logdor::Core)

add_executable(bench_merge bench_merge.cpp)
target_link_libraries(bench_merge PRIVATE Logdor::Core)

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

add_test(NAME bench.filter_1g
    COMMAND bench_filter ${BENCH_DATA}/plain-1g.log
            --query transaction --min-mbps 800 --check-cancel-ms 100
            --max-rowset-bytes-per-line 4.5)
add_test(NAME bench.filter_regex_1g
    COMMAND bench_filter ${BENCH_DATA}/plain-1g.log
            --query "transa[ck]tion \\w+" --regex --min-mbps 100)
set_tests_properties(bench.filter_1g bench.filter_regex_1g PROPERTIES
    FIXTURES_REQUIRED benchdata_1g LABELS "bench" TIMEOUT 600)

add_test(NAME bench.generate_logcat_1g
    COMMAND loggen --format logcat --bytes 1G --seed 44
            --out ${BENCH_DATA}/logcat-1g.log)
add_test(NAME bench.query_1g
    COMMAND bench_query ${BENCH_DATA}/logcat-1g.log
            --query "level:error tag:Wifi*"
            --min-extract-mbps 50 --max-warm-ms 500 --check-cancel-ms 150)
set_tests_properties(bench.generate_logcat_1g PROPERTIES
    FIXTURES_SETUP benchdata_logcat_1g LABELS "bench" TIMEOUT 600)
set_tests_properties(bench.query_1g PROPERTIES
    FIXTURES_REQUIRED benchdata_logcat_1g LABELS "bench" TIMEOUT 600)

# Merged timeline: ~18M rows across two inputs (measured 2026-07: 730 ms
# warm, 1.8 GB VmHWM incl. the mmap'd corpus, 27 ms cancel).
add_test(NAME bench.merge_2x1g
    COMMAND bench_merge ${BENCH_DATA}/logcat-1g.log --copies 2
            --max-warm-ms 2500 --max-rss-mb 3000 --check-cancel-ms 500)
set_tests_properties(bench.merge_2x1g PROPERTIES
    FIXTURES_REQUIRED benchdata_logcat_1g LABELS "bench" TIMEOUT 600)

if(NOT LOGDOR_ENABLE_BENCH)
    set_tests_properties(bench.generate_1g bench.index_1g
        bench.filter_1g bench.filter_regex_1g
        bench.generate_logcat_1g bench.query_1g bench.merge_2x1g
        PROPERTIES DISABLED TRUE)
endif()
if(NOT LOGDOR_ENABLE_BENCH OR NOT LOGDOR_BENCH_LARGE)
    set_tests_properties(bench.generate_5g bench.index_5g
        PROPERTIES DISABLED TRUE)
endif()
