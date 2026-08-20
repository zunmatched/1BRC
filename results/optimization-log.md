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

## I/O comparison

The I/O variants retain the integer parser, heterogeneous lookup, single-threaded aggregation, and standard `unordered_map`. The buffered reader uses 4 MiB reads and carries a partial record into the next chunk. The mapped reader uses Windows `CreateFileMapping` and scans the mapped bytes directly.

### 10M rows

| Version | Median | Throughput | Relative to `getline` |
|---|---:|---:|---:|
| `onebrc_no_row_allocations` (`getline`) | 0.467847 s | 279.54 MiB/s | 1.00× |
| `onebrc_buffered_io` | 0.297475 s | 439.64 MiB/s | 1.57× |
| `onebrc_mmap` | 0.337525 s | 387.47 MiB/s | 1.39× |

### 100M rows

Dataset: 100,000,000 rows, 1,371,306,708 bytes, random mode, seed 123. The environment and seven-run warm-cache method are unchanged.

| Version | Samples (seconds) | Median | Throughput | Relative to `getline` |
|---|---|---:|---:|---:|
| `onebrc_no_row_allocations` | 5.989484, 5.978071, 5.913806, 5.970379, 6.078575, 5.985376, 5.838688 | 5.978071 s | 218.76 MiB/s | 1.00× |
| `onebrc_buffered_io` | 2.772488, 2.779754, 2.774672, 2.794999, 2.822422, 2.828555, 2.852072 | 2.794999 s | 467.90 MiB/s | 2.14× |
| `onebrc_mmap` | 3.220006, 3.212041, 3.237430, 3.231764, 3.466062, 3.272392, 3.325318 | 3.237430 s | 403.96 MiB/s | 1.85× |

Both I/O variants produced byte-identical output to the previous version at 10M and 100M rows. On this machine, buffered I/O used 13.7% less median time than mapping at 100M, so it is the default foundation for the first parallel implementation. This conclusion applies to warm-cache Windows runs; cold-cache or Linux results may differ.
