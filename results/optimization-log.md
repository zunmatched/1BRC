# Optimization Log

## Test environment

- Date: 2026-08-20
- OS: Windows, warm filesystem cache
- CPU: AMD Ryzen AI 9 HX 370, 24 logical processors
- RAM: 31.1 GiB
- Compiler: MinGW-w64 GCC 13.2.0
- Build: C++23, CMake Release, Ninja
- Dataset: 10,000,000 rows, 137,134,781 bytes, random mode, seed 123
- Method: one unmeasured warm-up followed by seven measured runs; median wall-clock time reported

Cold-cache and cross-machine results are not compared here. Storage was not recorded for this warm-cache run.

## Results

| Version | Change from previous | Samples (seconds) | Median | Throughput | Relative to baseline |
|---|---|---|---:|---:|---:|
| `onebrc_baseline` | `std::getline`, `std::stod`, temporary strings | 4.329392, 4.340001, 4.344523, 4.290165, 4.183301, 4.180832, 4.356702 | 4.329392 s | 30.21 MiB/s | 1.00× |
| `onebrc_integer_parser` | Fixed-format integer parser | 0.554041, 0.592380, 0.549324, 0.551487, 0.554684, 0.557248, 0.544094 | 0.554041 s | 236.05 MiB/s | 7.81× |
| `onebrc_no_row_allocations` | `string_view` parsing and heterogeneous station lookup | 0.470556, 0.463524, 0.466407, 0.463198, 0.471293, 0.472230, 0.467847 | 0.467847 s | 279.54 MiB/s | 9.25× |

The allocation change reduced median time by 15.6% relative to the integer-parser version. All versions produced byte-identical output on the 10M dataset, single-station distribution, and 10,000-station distribution.
