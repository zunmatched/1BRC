# Benchmark Results

Commit only reproducible benchmark summaries. Local raw reports use the ignored
`*.local.md` suffix.

For every published result, record the commit, compiler and flags, Windows
version, CPU, RAM, storage device, dataset row and byte counts, cache state,
individual samples, median wall-clock time, and throughput. Compare versions on
the same machine and dataset.

Warm-cache results use one unmeasured warm-up followed by at least five runs.
Cold-cache results must be labelled separately and gathered after reboot; the
repository does not attempt unsafe or unreliable programmatic cache eviction.

