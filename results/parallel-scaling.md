# Parallel Scaling

## Design

`onebrc_parallel` divides the input into approximately equal byte ranges and advances each internal boundary to the next newline. Each `std::jthread` owns a fixed 256 KiB read buffer and private station map; workers never lock the hot loop. The main thread propagates worker exceptions and merges the maps once after joining. Thread count is explicit or hardware-derived and capped at 32.

The implementation deliberately uses buffered range reads rather than mapping the complete input. An experimental mapped version kept committed memory low but raised the 100M file-backed working set to about 1.30 GiB. Fixed buffers reduced the 24-thread peak working set to 14.62 MiB on 100M.

## 100M Scaling

Environment: AMD Ryzen AI 9 HX 370, 24 logical processors, 31.1 GiB RAM, Windows, MinGW-w64 GCC 13.2.0. Values are medians of five warm-cache Release runs over 1,371,306,708 bytes.

| Threads | Median seconds | Throughput |
|---:|---:|---:|
| 1 | 2.88 | 453.51 MiB/s |
| 2 | 1.54 | 848.21 MiB/s |
| 4 | 0.83 | 1,575.64 MiB/s |
| 8 | 0.65 | 2,007.07 MiB/s |
| 12 | 0.50 | 2,640.87 MiB/s |
| 16 | 0.45 | 2,919.85 MiB/s |
| 24 | 0.36 | 3,610.07 MiB/s |

## 1B Validation

The 24-thread target processed 13,712,587,980 bytes in 4.044860 seconds. Peak working set was 14.52 MiB and peak committed usage was 10.58 MiB. Raw output was byte-for-byte identical to `onebrc_bounded_memory`, with SHA-256 `B76132766D1CA123E37DA87FCC38DFCDB424F0D9319341B649F817684C20EFA5`.

These are initial local measurements, not final competition claims. Repeated 1B runs and profiler evidence are required before selecting further optimizations.
