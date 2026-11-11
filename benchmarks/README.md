# KernelTick Performance Benchmarking Framework

A dedicated, statistically rigorous benchmarking suite for **KernelTick** designed to study operating system latency, throughput, and performance consistency in low-latency trading software under Linux.

---

## 1. Overview & Experimental Reliability

Following the source-level validation and reliability audit, the benchmarking suite incorporates specific synchronization, measurement, and statistical protections:

### A. Barrier Synchronization & Start-Time Skew
Producer and consumer threads synchronize before launch using a C++20 barrier (`std::barrier sync_barrier(2)`):
- **Mechanism:** In GCC libstdc++, `std::barrier` is implemented via user-space atomic decrement and Linux futex syscalls.
- **Arrival vs. Execution:** A barrier synchronizes worker thread arrival at a common rendezvous point, but **does not guarantee simultaneous execution**. The thread that arrives second decrements the arrival count and wakes the waiting peer via futex syscall. The woken thread is subject to kernel futex unpark delays and OS / hypervisor vCPU scheduling latency.
- **Measurement & Skew Warning:** Immediately after exiting `arrive_and_wait()`, each thread reads `std::chrono::steady_clock`:
  $$\text{Start Skew} = t_{\text{prod\_start}} - t_{\text{cons\_start}}$$
  If the absolute start-time skew exceeds **5%** of the total pipeline elapsed time, the benchmark emits an explicit warning (`[WARNING: Start skew > 5% of pipeline elapsed time]`). All runs, retries, and outliers are fully retained without filtering.

### B. Accurate Workload Duration Boundaries
To prevent non-trading OS overhead from contaminating throughput calculations:
- **Producer Duration:** $\Delta t_{\text{prod}} = t_{\text{prod\_end}} - t_{\text{prod\_start}}$
- **Consumer Duration:** $\Delta t_{\text{cons}} = t_{\text{cons\_end}} - t_{\text{cons\_start}}$
- **Overall Workload Duration:** 
  $$\text{Duration} = \max(t_{\text{prod\_end}}, t_{\text{cons\_end}}) - \min(t_{\text{prod\_start}}, t_{\text{cons\_start}})$$
  This captures the active concurrent processing interval from the earliest thread release until the last message is consumed, while **strictly excluding** thread creation, join syscalls, and thread destructor delays.

### C. Retry Counters (Polling Iterations vs. OS Preemption)
The queue benchmark instruments thread-local counters to track contention without introducing cache-line bouncing during hot loops:
- **`push_retries`:** Number of times `queue.push()` returned `false` because the ring buffer was full.
- **`empty_pop_retries`:** Number of times `queue.pop()` returned `false` because the ring buffer was empty.
- **Semantic Clarification:** These retry counters record **unsuccessful polling loop iterations** in user space (`cpuPause()`), **not** proof of operating system preemptions or context switches. A high retry count indicates that one thread was actively spinning while waiting for the peer thread to advance its ring buffer position.

### D. Separation of Three Latency Dimensions
1. **Operation Latency (ns):** CPU execution time of an individual method call (`queue.pop()`, `book.addOrder()`, `book.cancelOrder()`). Measured immediately before and after the invocation.
2. **End-to-End Message Latency (ns):** Time elapsed between when a message is assigned an enqueue timestamp by the producer and when it is consumed (`dequeue_ts - enqueue_ts`). Measures ring buffer transit and dwell delay.
3. **Pipeline Throughput:**
   - Producer Throughput: $N / \Delta t_{\text{prod}}$
   - Consumer Throughput: $N / \Delta t_{\text{cons}}$
   - Overall Concurrent Pipeline Throughput: $N / \text{Duration}$

### E. Dead-Code Elimination Defense
All return values and popped messages are passed through `doNotOptimize(val)` (`asm volatile("" : "+m,r"(value) : : "memory")`), preventing the compiler from eliminating side effects or dead stores under `-O3 -flto`.

### F. Cache & Branch Predictor Warm-Up
A dedicated warm-up phase (10,000 operations) primes L1/L2 instruction and data caches, page tables, and branch history tables before critical timed sections begin.

---

## 2. Statistical Methodology & Formulae

### Percentile Calculation Formula (Within-Run)
Within each run, percentiles ($P_{50}, P_{95}, P_{99}, P_{99.9}$) are computed using the standard **NIST / Hyndman & Fan Type 7** linear rank interpolation:

$$\text{Rank}(p) = p \cdot (N - 1)$$

Given lower index $k = \lfloor \text{Rank} \rfloor$, upper index $k+1$, and interpolation weight $w = \text{Rank} - k$:

$$P_p = X[k] \cdot (1 - w) + X[k+1] \cdot w$$

### Limitations of Sample Sizing for Extreme Tails ($P_{99.9}$)
For a workload of $N = 10,000$ observations, computing $P_{99.9}$ yields:

$$\text{Rank}(0.999) = 0.999 \times (10,000 - 1) = 9989.001$$

In a zero-based sorted array $[0 \dots 9999]$, this interpolates between index 9989 and index 9990. Consequently, there are **only 10 observations** above index 9989 out of the entire 10,000-sample dataset.
- **Statistical Reality:** Having only 10 observations in the upper tail does **not** guarantee a statistically reliable estimate of extreme tail latency. A single hypervisor scheduling interruption or host background task can heavily distort these 10 samples.
- **Workload Requirement:** Reliable extreme-tail estimates require at least **$N = 100,000$** ($\approx 100$ tail observations) or **$N = 1,000,000$** ($\approx 1,000$ tail observations) to populate the upper quantiles densely.

### Multi-Run Repetitions & Across-Run Aggregation
To evaluate experimental stability across $R$ independent repetitions (default: 5):
- **Median Throughput:** The central value of sorted run throughputs ($T_{(R+1)/2}$ for odd $R$), representing steady-state performance.
- **Arithmetic Mean Throughput:** $\bar{T} = \frac{1}{R} \sum_{i=1}^R T_i$
- **Unbiased Sample Standard Deviation:**
  $$s = \sqrt{\frac{1}{R - 1} \sum_{i=1}^R (T_i - \bar{T})^2}$$
  Calculated using Bessel's correction ($R - 1$) for unbiased sample variance across small repetition counts.
- **Min & Max Boundaries:** Full range boundaries preserved without discarding slow runs or outliers.
- **Mean Operation $P_{50}$ & Mean End-to-End $P_{50}$:** Arithmetic means of within-run median latencies across repetitions.

> [!NOTE]
> Reporting median throughput does **not** automatically make a benchmark statistically reliable if the underlying runs exhibit massive dispersion. Rather, the combination of Median, Sample Standard Deviation, Min/Max spread, and Retry Counters provides a transparent, complete characterization of experimental variance.

---

## 3. Benchmarks Included

### A. Order Book Benchmark (`OrderBookBenchmark`)
1. **`order_insertion_resting`:** Pure `OrderBook::addOrder()` latency across 40 price levels (20 bids, 20 asks), exercising FIFO queueing and price ladder traversals.
2. **`order_cancellation`:** Pure `OrderBook::cancelOrder()` on pre-populated resting orders, verifying complete cleanup and best price recalculation.
3. **`order_matching_partial_fills`:** Aggressive crossing buy orders executing against pre-populated resting sell depth, exercising partial fills without teardown overhead.
4. **`order_book_mixed_workload`:** Realistic 40% Buy, 40% Sell, 10% Crossing, and 10% Cancellation continuous order flow.

### B. SPSC Queue Benchmark (`QueueBenchmark`)
1. **`spsc_queue_capacity_64k`:** Evaluates lock-free ring buffer performance with 65,536 slots (standard high-absorption buffer).
2. **`spsc_queue_capacity_1k`:** Evaluates lock-free ring buffer performance with 1,024 slots (compact cache footprint, backpressure sensitive).
- Uses `std::barrier` synchronized startup with start skew reporting.
- Measures individual `pop()` operation execution latency, enqueue-to-dequeue dwell latency, and retry counts.

---

## 4. How to Build and Run

### Build
From the repository root:
```bash
cmake -S . -B build
cmake --build build -j2
```

### Run SPSC Queue Benchmark
Run standard 1,000,000-message scale test (5 repetitions):
```bash
./build/bin/QueueBenchmark --size 1000000 --runs 5
```

Run 100,000-message test:
```bash
./build/bin/QueueBenchmark --size 100000 --runs 5
```

Custom CSV output:
```bash
./build/bin/QueueBenchmark --size 1000000 --runs 5 --csv benchmark_results/custom_queue.csv
```

### Run Order Book Benchmark
Run default validation workload (10,000 operations, 3 repetitions):
```bash
./build/bin/OrderBookBenchmark --size 10000 --runs 3
```

---

## 5. CSV Schema

Results are exported to `benchmark_results/`:
- `benchmark_results/order_book_benchmark.csv`
- `benchmark_results/queue_benchmark.csv`

The CSV schema separates individual runs (`RUN`) from aggregate summaries (`AGGREGATE`):

```csv
record_type,benchmark_name,workload_size,run_index,operations,duration_ms,throughput_ops_sec,producer_throughput_ops_sec,consumer_throughput_ops_sec,start_skew_ns,push_retries,empty_pop_retries,op_avg_ns,op_min_ns,op_p50_ns,op_p95_ns,op_p99_ns,op_p99_9_ns,op_max_ns,op_stddev_ns,has_e2e_latency,e2e_avg_ns,e2e_min_ns,e2e_p50_ns,e2e_p95_ns,e2e_p99_ns,e2e_p99_9_ns,e2e_max_ns,e2e_stddev_ns,compiler_config
```

---

## 6. VirtualBox and 2-vCPU Virtualization Limitations

When interpreting results in this environment:
1. **Hypervisor Preemption Jitter:** VirtualBox vCPUs are multiplexed onto host CPU physical cores by the host OS. When the host preempts a vCPU during an SPSC spin-wait, the sibling vCPU spins fruitlessly, causing run-to-run throughput variance.
2. **Absence of Core Pinning & Isolated Cores:** On bare metal with `isolcpus` and thread affinity, lock-free threads execute without preemption. In this virtualized environment, threads migrate across virtual cores.
3. **Timer & TSC Emulation:** Monotonic clocks inside a VM depend on virtualized TSC or paravirtualized timers, which can show intermittent clock skew or quantization jitter under host thread migration.
4. **Shared L3 Cache Contention:** Host processes and virtualized vCPUs share physical host CPU caches, leading to synthetic cache evictions.
5. **Context of Results:** Measurements generated within this VM are intended for **controlled relative comparative analysis** (e.g. comparing container runtimes, scheduler policies, or memory management flags) rather than absolute bare-metal HFT service level agreements.
