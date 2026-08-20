# Memory Stability

## Contract

`onebrc_bounded_memory` keeps heap demand independent of row count:

- one fixed 4 MiB streaming buffer;
- one hash table bounded by the documented maximum of 10,000 station names;
- station names copied only on first insertion;
- an output vector containing pointers, not copied station entries;
- no container proportional to input bytes or row count.

The executable catches `std::bad_alloc`, writes `error: memory allocation failed` to stderr, and returns exit code 3. Windows tests run child processes in a Job Object. The production build succeeds under a 32 MiB process-memory limit; a test-only build requesting a 64 MiB buffer fails deterministically under the same limit and satisfies the error contract.

## Measurements

Environment: AMD Ryzen AI 9 HX 370, 24 logical processors, 31.1 GiB RAM, Windows, MinGW-w64 GCC 13.2.0. Measurements describe the solution process and exclude the Windows filesystem cache.

| Dataset | Input bytes | Stations | Peak working set | Peak private bytes | Peak virtual bytes |
|---|---:|---:|---:|---:|---:|
| 1M random | 13,714,085 | 16 | 9.02 MiB | 5.01 MiB | 4,158.49 MiB |
| 10M random | 137,134,781 | 16 | 9.03 MiB | 5.02 MiB | 4,158.49 MiB |
| 100M random | 1,371,306,708 | 16 | 9.03 MiB | 5.02 MiB | 4,158.47 MiB |
| 10M maximum-key distribution | 173,999,542 | 10,000 | 9.85 MiB | 5.87 MiB | 4,159.47 MiB |

From 1M to 100M rows, input size increased 100× while peak private bytes changed by about 0.01 MiB. Exercising all 10,000 station keys increased peak private memory by less than 1 MiB. The large but stable virtual-size figure is address space reserved by the Windows/MinGW process and is not resident physical memory.

## Performance and correctness

On the 100M random dataset, the bounded variant produced byte-identical output to `onebrc_buffered_io`. Its seven-run warm-cache median was 2.793150 seconds (468.21 MiB/s), versus 2.794999 seconds (467.90 MiB/s) before the bounded-memory changes. The stability controls introduced no measurable regression in this run.

The remaining acceptance item is a complete 1B run with the same output, memory, and controlled-failure checks.
