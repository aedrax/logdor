// bench_index: performance gate for line indexing.
// Finalized once LineIndexer lands; the CTest wiring and thresholds are fixed.
//
// Usage: bench_index <logfile> --min-mbps N --max-bytes-per-line X --check-cancel-ms N

#include <logdor/Version.h>

#include <cstdio>

int main()
{
    std::fprintf(stderr, "bench_index: not implemented yet (core %s)\n",
                 qPrintable(logdor::coreVersion()));
    return 2;
}
