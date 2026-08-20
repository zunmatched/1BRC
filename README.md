# 1BRC in C++

A measured, incremental C++ implementation of the [1 Billion Row Challenge](https://1brc.dev/). The repository starts with a deliberately portable baseline; later optimizations must preserve byte-for-byte output and justify themselves with repeatable benchmarks.

The project prioritizes **correctness first, execution stability second, and performance third**. In particular, heap usage must remain bounded independently of input size, and resource-exhaustion failures must be deterministic and diagnosable. See [DEVELOPMENT.md](DEVELOPMENT.md) for milestones and acceptance criteria.

## Requirements

- Windows 10/11
- MinGW-w64 GCC 13 or newer
- CMake 3.20 or newer
- Ninja
- PowerShell 7 recommended

No external C++ or test libraries are required.

## Build and test

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The CLI accepts exactly one input path and writes only the canonical result to stdout:

```powershell
.\build\onebrc_baseline.exe .\tests\fixtures\sample.txt
```

`onebrc_integer_parser.exe` exposes the first isolated optimization. It uses the same aggregation path and output contract, replacing only general-purpose `std::stod` parsing with a fixed-format integer parser:

```powershell
.\build\onebrc_integer_parser.exe .\tests\fixtures\sample.txt
```

`onebrc_no_row_allocations.exe` additionally uses `string_view` parsing and heterogeneous hash lookup. Temperature substrings are never allocated, and a station name is copied only on its first occurrence:

```powershell
.\build\onebrc_no_row_allocations.exe .\tests\fixtures\sample.txt
```

Stage 4 provides two single-threaded I/O variants with the same parser and aggregation logic:

```powershell
.\build\onebrc_buffered_io.exe .\tests\fixtures\sample.txt
.\build\onebrc_mmap.exe .\tests\fixtures\sample.txt
```

The buffered target streams through a 4 MiB buffer and preserves partial records across reads. The Windows-only mapped target uses `CreateFileMapping` and accepts UTF-8 paths. Current warm-cache results favor the buffered target; both remain available for later experiments.

`onebrc_bounded_memory.exe` is the stability-first buffered variant. It reserves the documented 10,000-station bound up front, sorts pointers rather than copied entries, and reports allocation exhaustion as exit code 3:

```powershell
.\build\onebrc_bounded_memory.exe .\tests\fixtures\sample.txt
```

The bound is enforced, not merely reserved: a 10,001st unique station is rejected before another map allocation. Mean rounding is performed with exact integer arithmetic, including negative half ties toward positive infinity.

The Windows parallel target accepts an optional thread count from 1 to 32; otherwise it uses the detected hardware concurrency, capped at 32:

```powershell
.\build\onebrc_parallel.exe .\data\measurements-100m-random.txt 24
```

It moves each nominal range boundary to the next newline, uses one fixed 256 KiB buffer and private aggregate table per worker, performs no locking in the hot loop, and merges once after all `std::jthread` workers finish. The existing single-threaded targets remain unchanged as correctness and performance references.

`onebrc_parallel_flat_map.exe` isolates the station-table optimization. It replaces node-based `std::unordered_map` storage with a fixed 16,384-slot open-addressing table while retaining the parallel parser and I/O path:

```powershell
.\build\onebrc_parallel_flat_map.exe .\data\measurements-1b-random.txt 32
```

The fixed capacity covers the 10,000-station contract at a maximum 61% load. It trades additional bounded memory for cache-local lookup; 32-thread worst-case measurements remain below the project's 64 MiB process budget.

Input records are UTF-8 `station;temperature` lines. Temperatures range from `-99.9` to `99.9`; aggregates are stored as integer tenths and stations are sorted by UTF-8 bytes. Errors go to stderr with a non-zero exit code.

## Generate datasets

Build first, then generate deterministic data outside Git:

```powershell
.\scripts\generate.ps1 -Scale 1M
.\scripts\generate.ps1 -Scale 10M -Mode single
.\scripts\generate.ps1 -Scale 100M -Mode unique10000
.\scripts\generate.ps1 -Scale 1B
```

Supported scales are 1M, 10M, 100M, and 1B rows. `random` is the normal distribution; `single` stresses a maximally skewed key distribution; `unique10000` exercises the maximum station count. A 1B-row file needs roughly 12 GB and is intended for manual full runs.

## Benchmark

Benchmark Release builds with at least five measured warm-cache runs:

```powershell
.\scripts\benchmark.ps1 -InputPath .\data\measurements-1m-random.txt -Runs 5
.\scripts\benchmark.ps1 -InputPath .\data\measurements-1m-random.txt `
  -Executable .\build\onebrc_integer_parser.exe -Runs 5
```

The script performs one unmeasured warm-up, reports the median and MiB/s, captures basic hardware metadata, and writes `results/baseline.local.md`. That local report is ignored until reviewed and renamed. Cold-cache numbers must be recorded separately after reboot because Windows has no reliable unprivileged cache-eviction interface.

Measure OS-maintained peak working set, committed/pagefile usage, and virtual memory separately from the Windows filesystem cache:

```powershell
.\scripts\measure-memory.ps1 `
  -InputPath .\data\measurements-100m-random.txt `
  -Executable .\build\onebrc_parallel.exe `
  -ExtraArguments 24
```

Windows CTest also runs the solution inside a Job Object: the production target must succeed under 32 MiB, while a test-only 64 MiB-buffer build must catch allocation failure under the same limit and return exit code 3.

## Optimization roadmap

1. Baseline: `getline`, standard parsing, and `unordered_map`.
2. Integer parser: replace general numeric parsing while retaining the baseline allocation behavior.
3. No-row-allocation lookup: remove temporary substrings and copy station names only once.
4. I/O comparison: stream through a 4 MiB buffer or use Windows file mapping.
5. Establish a bounded-memory contract, measure peak memory, and test controlled allocation failure.
6. Run the complete 1B dataset with the single-threaded stable implementation.
7. Add newline-safe chunking and thread-local aggregation under an explicit total memory budget. (Completed.)
8. Use profiler evidence to evaluate a custom hash table, branch reduction, or SIMD.

Each version must pass correctness, bounded-memory, and failure-behavior checks before its performance is reported. Benchmark results from different hardware are not directly comparable.
