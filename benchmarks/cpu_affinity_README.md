# KernelTick Phase 5.1: CPU Affinity Benchmarking

An empirical study of operating system CPU affinity and thread placement on **KernelTick**, an experimental Linux-based low-latency trading engine designed to study operating system latency, throughput, and performance consistency.

---

## 1. Overview & Objective

In high-frequency trading (HFT) and ultra-low-latency messaging architectures, thread placement and CPU core locality play a defining role in performance. When two cooperating threads (such as a market data feed handler and an order matching engine) communicate via a lock-free Single-Producer Single-Consumer (SPSC) queue:

- If both threads are scheduled onto the **same CPU core**, they must time-slice execution. When the producer fills the queue and spins in user space (`cpuPause()`), it starves the consumer thread from being scheduled on that core, inducing catastrophic tail latency.
- If threads are allowed to freely **migrate across CPU cores**, migration invalidates L1/L2 data and instruction caches, forces inter-processor interrupts (IPIs), and triggers cache coherency invalidation traffic.

The objective of **KernelTick Phase 5.1** is to empirically evaluate:
1. **Experiment A (Default Linux Scheduling):** Producer and consumer threads are managed by the standard Linux Completely Fair Scheduler (CFS) with no CPU affinity constraints.
2. **Experiment B (CPU Affinity):** The producer thread is strictly pinned to one available CPU (`CPU 0`), and the consumer thread is strictly pinned to a distinct available CPU (`CPU 1`) using Linux `pthread_setaffinity_np`.

---

## 2. Linux CPU Affinity Mechanisms

### API Implementation Details
- **CPUs Detection:** Rather than hardcoding CPU IDs or relying on `std::thread::hardware_concurrency()`, `QueueBenchmark` dynamically interrogates the process CPU mask via `sched_getaffinity(0, sizeof(cpu_set_t), &cpuset)`.
- **Worker Pinning:** Each worker thread configures its affinity mask using `pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset)`.
- **Pre-Barrier Enforcement:** Worker threads apply affinity **before** entering the synchronization barrier (`std::barrier sync_barrier(2)`). This guarantees that both threads are physically placed on their designated cores before the monotonic clock starts.
- **Verification & Error Handling:** Return codes from affinity syscalls are explicitly checked. If fewer than 2 CPUs are available or if affinity configuration fails, the benchmark reports a fatal error and terminates rather than silently running unpinned.
- **Main Coordinator Thread:** The main benchmark coordinator thread remains unpinned, leaving standard OS scheduling intact for I/O and process monitoring.

---

## 3. Benchmark Methodology

- **Workload:** 1,000,000 messages per run.
- **Repetitions:** 5 independent runs per configuration.
- **Capacities Evaluated:**
  - **1K Capacity (1,024 slots / 1,023 items):** Highly sensitive to backpressure and thread preemption.
  - **64K Capacity (65,536 slots / 65,535 items):** Large buffer that absorbs message bursts.
- **Metrics Recorded:**
  - Overall pipeline throughput ($N / \text{Pipeline Duration}$).
  - Producer and consumer throughputs.
  - Operation execution latency ($P_{50}, P_{95}, P_{99}, P_{99.9}$ via NIST Type 7 linear interpolation).
  - End-to-end message dwell latency ($P_{50}, P_{95}, P_{99}, P_{99.9}$).
  - Failed push (`push_retries`) and empty pop (`empty_pop_retries`) contention counters.
  - Producer-consumer start-time skew.

---

## 4. Empirical Benchmark Results

Executed on Debian Linux (Kernel 6.12, GCC 15.2.0 `-O3 -march=native C++20`, 2 vCPUs inside VirtualBox on a 13th Gen Intel Core i7-13620H host):

### Summary Comparison Table (Averages Across 5 Repetitions)

| Queue Capacity | Scheduling Mode | Producer / Consumer CPU | Mean Throughput (ops/s) | Median Throughput (ops/s) | Throughput StdDev (ops/s) | Op $P_{50}$ | Op $P_{99}$ | E2E $P_{50}$ | Total Push Retries (5 Runs) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **1K Capacity** | **Default Linux** | Unpinned (`-1, -1`) | 3,662,916 | 3,158,133 | +/- 3,465,577 | 49 ns | 140 ns | 72 µs | 859,572,202 |
| | **CPU Affinity** | Pinned (`0, 1`) | **5,348,178** | **5,306,553** | **+/- 1,220,102** | 93 ns | 271 ns | 142 µs | **3,132,885** |
| | *Delta (%)* | | **+46.0% (Mean)** | **+68.0% (Median)** | **-64.8% (Variance)** | | | | **-99.6% (Retries)** |
| **64K Capacity** | **Default Linux** | Unpinned (`-1, -1`) | **10,774,319** | **11,131,676** | +/- 799,926 | 57 ns | 156 ns | 5.00 ms | 1,717,604 |
| | **CPU Affinity** | Pinned (`0, 1`) | 5,620,099 | 6,439,483 | +/- 1,637,460 | 74 ns | 231 ns | 8.52 ms | 2,953,953 |
| | *Delta (%)* | | **-47.8% (Mean)** | **-42.2% (Median)** | +104.7% (Variance) | | | | +72.0% (Retries) |

---

## 5. Deep Technical Analysis & Findings

### 1. Where CPU Affinity Succeeded: Backpressure-Sensitive Workloads (1K Buffer)
In the 1K capacity queue, CPU affinity delivered a **decisive improvement**:
- **Throughput Gain:** Mean throughput increased from **3.66M ops/s to 5.35M ops/s (+46.0%)**, and median throughput jumped from **3.16M ops/s to 5.31M ops/s (+68.0%)**.
- **Elimination of Catastrophic Preemption Stalls:** Under Default Linux scheduling, Run #1 suffered a catastrophic **193.56-second pipeline freeze** with a throughput of only 5,166 ops/s and **848.5 million push retries**! This occurred because the Linux CFS scheduler placed both the producer and consumer on the same vCPU; while one thread spun in `cpuPause()`, the other thread was starved of CPU time slices.
- **Contention Reduction:** By pinning the producer to CPU 0 and consumer to CPU 1, total push retries across all 5 runs collapsed by **99.6%** (from 859M down to 3.1M).
- **Variance Stabilization:** Standard deviation dropped by **nearly 3x** (from 3.47M down to 1.22M), providing much greater runtime determinism.

### 2. Where CPU Affinity Hurt: Burst-Absorbing Buffers (64K Buffer)
In the 64K capacity queue, CPU affinity **reduced throughput by 47.8%** (from 10.77M ops/s down to 5.62M ops/s):
- **Why?** In a large 64K ring buffer, the queue rarely encounters boundary conditions (full or empty). Under default Linux scheduling, the OS kernel is free to float threads across vCPUs dynamically to match host OS time slices.
- When strict affinity was enforced in this virtualized environment, guest threads were permanently pinned to specific vCPUs. When the host OS or hypervisor interrupted one vCPU, the guest thread could not be migrated by the guest kernel to the idle vCPU, resulting in pipeline stalls that halved throughput.

---

## 6. VirtualBox & Hypervisor Caveats: Why Two vCPUs $\ne$ Two Physical Cores

A critical insight of this experiment is that **two virtual CPUs inside a VirtualBox VM do not behave like two isolated physical cores on bare metal**:

1. **Host-Level Thread Multiplexing:** VirtualBox guest vCPUs are regular threads scheduled by the host operating system kernel. The host may preempt a vCPU thread to execute host background tasks, browser processes, or audio servers.
2. **Heterogeneous CPU Topology (P-Cores vs E-Cores):** On modern hybrid processors (such as the 13th Gen Intel Core i7-13620H), the host kernel assigns threads across Performance (P) cores and Efficiency (E) cores. A guest vCPU pinned to CPU 1 might be backed by an E-core or an uncoordinated hyperthread, creating an artificial performance asymmetry between producer and consumer.
3. **No Direct Inter-Processor Interrupts (IPIs):** On bare metal, cache coherency (MESI) and store buffers operate via physical silicon buses. In a virtual machine, cross-vCPU memory updates are mediated by shadow page tables and hypervisor memory virtualization, adding latency jitter.

---

## 7. Conclusions & Roadmap Recommendation

- **CPU affinity is not a universal performance accelerator.**
- In **tight, backpressure-heavy pipelines (1K buffer)**, affinity is **essential** to prevent devastating co-scheduling starvation and core thrashing (+46% to +68% throughput, 99.6% fewer retries).
- In **large buffers (64K)** inside a virtual machine, rigid pinning can impair performance (-48%) by preventing beneficial OS thread balancing during hypervisor preemption.

### Next Step
With Phase 5.1 (CPU Affinity) fully validated and documented, the project is ready to proceed to **Phase 5.2 — Real-Time Scheduling Policies (`SCHED_FIFO` vs `SCHED_OTHER`)**.
