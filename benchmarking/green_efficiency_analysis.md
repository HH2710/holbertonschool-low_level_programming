# Green Efficiency Analysis

## Measurement Methodology

All programs were compiled with `gcc -Wall -Werror -Wextra -pedantic -std=gnu89 -Wno-long-long` and executed on the same machine. Time was measured with `clock()` and converted to seconds with `CLOCKS_PER_SEC`. The baseline loop and the algorithm comparison were run three times each, and the instrumented program once. In `instrumentation_lab.c`, each timer starts right before the measured phase and stops right after it, and `printf` is called only after all measurements, so output time is never counted. The same rule is applied to every phase, which keeps the comparison fair.

## Observed Performance Differences

The baseline loop (100,000,000 iterations) took 0.226445, 0.226188 and 0.226082 seconds, an average of 0.226238 seconds. The runs differ by 0.000363 seconds at most, about 0.16% of the average, so the timing is stable for this workload.

In the comparison program, the naive O(n^2) algorithm took 1.948836, 2.077030 and 1.991428 seconds (average 2.005765). The single-pass O(n) algorithm took 0.000087, 0.000103 and 0.000103 seconds (average 0.000098). The naive algorithm was about 20,500 times slower. The ratio of operations is 1,250,025,000 checks against 50,000, about 25,000, which is the same order of magnitude.

The instrumented program reported TOTAL = 0.000347, BUILD_DATA = 0.000149, PROCESS = 0.000125 and REDUCE = 0.000072 seconds. TOTAL is greater than each phase, and BUILD_DATA was the longest. The three phases add up to 0.000346 seconds, almost exactly TOTAL.

## Relation Between Runtime and Energy Consumption

No power was measured in watts, so no energy value is claimed. Runtime is used as a proxy: a longer runtime means longer CPU activity. The naive algorithm kept the CPU busy for 2.005765 seconds on average, against 0.000098 seconds for the single-pass one. If energy grows with CPU active time, the O(n) version is the more efficient choice, and the gap would add up when the code runs many times.

## Limitations of the Experiment

`clock()` measures CPU time, not energy. Its resolution limits short measurements: the single-pass times vary by about 16% of their average, while the naive times vary by about 6%, so the ratio of 20,500 is approximate. The phases of the instrumented program last under 0.00015 seconds, so their values are the least reliable. Only three runs of two programs and one run of the instrumented program were made, on a single machine, and the programs were compiled without optimization flags, so results may differ in other environments.

## Practical Engineering Takeaway

Replacing an O(n^2) approach by an O(n) one reduced the measured time from about 2 seconds to about 0.0001 seconds for 50,000 elements. Engineers should measure before optimizing, repeat measurements to see variation, keep conditions identical, and state limitations honestly. Choosing the efficient algorithm is a simple way to reduce CPU time, the proxy used here for energy.
