# 1BRC in C++

A measured, incremental C++ implementation of the [1 Billion Row Challenge](https://1brc.dev/). The repository starts with a deliberately portable baseline; later optimizations must preserve byte-for-byte output and justify themselves with repeatable benchmarks.

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

## Optimization roadmap

1. Baseline: `getline`, standard parsing, and `unordered_map`.
2. Integer parser: replace general numeric parsing while retaining the baseline allocation behavior.
3. No-row-allocation lookup: remove temporary substrings and copy station names only once.
4. Compare buffered I/O with Windows file mapping.
5. Add newline-safe chunking and thread-local aggregation.
6. Use profiler evidence to evaluate a custom hash table, branch reduction, or SIMD.

Each version must pass the baseline fixture and differential tests before its performance is reported. Benchmark results from different hardware are not directly comparable.
