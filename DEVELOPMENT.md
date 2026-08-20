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

## Next: Memory Resilience

Before adding threads, establish and test a memory contract:

- Reserve capacity deliberately for the documented maximum of 10,000 stations and benchmark the cache-locality tradeoff.
- Sort pointers to station entries instead of copying keys and aggregates.
- Catch `std::bad_alloc` separately and return a documented exit code with a stable stderr message.
- Record peak working set, private bytes, virtual size, and configured buffer size.
- Add Windows Job Object tests that run the solution under explicit process-memory limits and verify both successful and controlled-failure paths.
- Demonstrate that peak private memory remains approximately constant across 1M, 10M, 100M, and 1B inputs.
- Run the complete 1B dataset only after these checks pass.

Acceptance requires no uncaught allocation failures, no input-sized heap allocation, byte-identical output, and documented peak-memory measurements.

## Later Milestones

1. Add newline-safe parallel partitioning with thread-local aggregation and an explicit total memory budget. Buffer and table memory must be derived from the selected thread count.
2. Measure scaling at 1, 2, 4, 8, and higher thread counts; stop when throughput or the memory budget no longer improves.
3. Evaluate a custom hash table, branch reduction, or SIMD only when profiling identifies the relevant bottleneck.
4. Produce the final 1B benchmark, architecture explanation, limitations, and résumé-ready results.

Every milestone is committed separately and must include correctness tests, stability evidence, and same-machine before/after measurements.

