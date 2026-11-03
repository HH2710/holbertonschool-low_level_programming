# Comparison Algorithms Metrics

Compiled with: gcc -Wall -Werror -Wextra -pedantic -std=gnu89 -Wno-long-long comparison_algorithms.c -o comparison_algorithms

## Raw output

### Run 1
Naive algorithm result: 625025000
Naive algorithm time: 1.948836 seconds
Single-pass algorithm result: 25000
Single-pass algorithm time: 0.000087 seconds

### Run 2
Naive algorithm result: 625025000
Naive algorithm time: 2.077030 seconds
Single-pass algorithm result: 25000
Single-pass algorithm time: 0.000103 seconds

### Run 3
Naive algorithm result: 625025000
Naive algorithm time: 1.991428 seconds
Single-pass algorithm result: 25000
Single-pass algorithm time: 0.000103 seconds

## Averages

| Implementation | Run 1 (s) | Run 2 (s) | Run 3 (s) | Average (s) |
|---|---|---|---|---|
| Naive (A) | 1.948836 | 2.077030 | 1.991428 | 2.005765 |
| Single-pass (B) | 0.000087 | 0.000103 | 0.000103 | 0.000098 |

- Naive average: (1.948836 + 2.077030 + 1.991428) / 3 = 2.005765 seconds
- Single-pass average: (0.000087 + 0.000103 + 0.000103) / 3 = 0.000098 seconds

## Relative difference

2.005765 / 0.000098 = ~20,500

The naive algorithm (A) is ~20,500 times slower than the single-pass algorithm (B).

## Analysis

The naive algorithm restarts the inner loop from index 0 for every value of i, so it performs about n(n+1)/2 = 1.25 billion checks for n = 50000: its complexity is O(n^2). The single-pass algorithm checks each element once, about 50000 checks: O(n). The theoretical ratio of operations is about 25,000, which is close to the measured ratio of ~20,500.

The single-pass time (~100 microseconds) is close to the resolution of clock(), so its measurement is noisy and the ratio is approximate.

Runtime is used as a proxy for CPU activity: the naive version keeps the CPU busy about four orders of magnitude longer, which at scale means more energy consumed for the same kind of task.
