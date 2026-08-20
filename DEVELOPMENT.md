# Development Plan

## Priorities

Development decisions follow this order:

1. Correctness: valid inputs always produce canonical byte-identical output.
2. Stability: memory use is bounded, failures are diagnosed, and resources are released by RAII.
3. Performance: an optimization is retained only after the first two requirements pass.

Input size must not determine heap size. Implementations may stream or map the input file, but must not materialize the complete dataset in heap memory.

## Completed Milestones

- Portable baseline with deterministic fixtures and CTest.
- Fixed-format integer parsing.
- Allocation-free hot-loop lookup for known stations.
- Single-threaded comparison of `getline`, 4 MiB buffered I/O, and Windows file mapping.
- Reproducible 10M and 100M warm-cache benchmarks.

## Current: Memory Resilience

Completed stability controls:

- The production buffer is fixed at 4 MiB and the input is never materialized in heap memory.
- The station map reserves the documented 10,000-key bound before processing.
- A 10,001st unique station is rejected before any further map allocation.
- Output sorting stores pointers to map entries instead of copying names and aggregates.
- `std::bad_alloc` produces a stable diagnostic and exit code 3.
- Windows Job Object tests cover success and deterministic allocation failure under a 32 MiB process limit.
- The Job runner owns Windows handles through move-only RAII, checks API failures, and enforces a 10-minute timeout.
- `scripts/measure-memory.ps1` records OS-maintained peak working set, committed/pagefile usage, and virtual bytes for the child solution process.

Remaining before adding threads:

- Run the complete 1B input and confirm byte-identical output plus the same bounded-memory behavior.
- Run AddressSanitizer and UndefinedBehaviorSanitizer with a toolchain that provides their runtimes; the installed Strawberry MinGW distribution does not include `libasan` or `libubsan`.

Acceptance requires no uncaught allocation failures, no input-sized heap allocation, byte-identical output, and documented peak-memory measurements.

## Later Milestones

1. Add newline-safe parallel partitioning with thread-local aggregation and an explicit total memory budget. Buffer and table memory must be derived from the selected thread count.
2. Measure scaling at 1, 2, 4, 8, and higher thread counts; stop when throughput or the memory budget no longer improves.
3. Evaluate a custom hash table, branch reduction, or SIMD only when profiling identifies the relevant bottleneck.
4. Produce the final 1B benchmark, architecture explanation, limitations, and résumé-ready results.

Every milestone is committed separately and must include correctness tests, stability evidence, and same-machine before/after measurements.
