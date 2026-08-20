# Repository Guidelines

## Project Structure & Module Organization

This repository implements the 1 Billion Row Challenge in C++. Keep the portable, correct implementation separate from later optimized variants:

- `src/`: C++ sources, beginning with `baseline.cpp`; name experimental implementations by technique, such as `mmap.cpp` or `parallel.cpp`.
- `tests/`: small deterministic inputs and expected outputs. Never commit the generated billion-row data file.
- `scripts/`: dataset generation and benchmark helpers.
- `results/`: reproducible benchmark tables and profiler notes.
- `CMakeLists.txt`: build targets and compiler options.

Keep challenge submissions self-contained even if local test and benchmark tooling uses multiple files.

## Build, Test, and Development Commands

Use an out-of-source CMake build:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Run an implementation with an explicit input path, for example:

```powershell
.\build\onebrc_baseline.exe .\tests\fixtures\sample.txt
```

Benchmark only Release builds. Record the compiler, flags, CPU, storage, input size, and median of at least five runs in `results/`.

## Coding Style & Naming Conventions

Target C++23 unless a documented optimization requires otherwise. Use four-space indentation, braces on the same line, `snake_case` for functions and variables, and `PascalCase` for types. Prefer fixed-width integer types for temperature aggregates; parse tenths into integers rather than using floating point. Use RAII, `std::string_view` only with clearly valid lifetimes, and comments that explain performance tradeoffs rather than restating code. Format changed C++ files with `clang-format` using the repository configuration when one is added.

## Testing Guidelines

Every optimization must match the baseline byte-for-byte. Add cases for negative temperatures, rounding, UTF-8 and long station names, repeated stations, missing final newlines, and chunk boundaries. Name fixtures descriptively, such as `tests/negative-temperatures.txt`. Keep correctness tests small; performance datasets belong outside Git.

## Commit & Pull Request Guidelines

No project history exists yet, so use concise Conventional Commits, for example `perf: add integer temperature parser` or `test: cover split-line boundaries`. Keep each commit buildable and isolate optimizations so their impact is measurable. Pull requests should describe the approach, correctness checks, benchmark methodology, before/after results, hardware details, and any portability limitations. Link relevant issues and credit techniques adapted from other 1BRC implementations.
