# KernelTick Phase 6: Experiment Inventory & Deliverable Audit

**Project:** KernelTick — A Linux-Based Low-Latency Trading Engine for Studying Operating System Performance  
**Date of Audit:** November 4, 2026  
**Phase:** Phase 6 — Final Analysis and Documentation (Inventory & Audit Stage)  
**Deliverable Document:** `benchmarks/PHASE6_EXPERIMENT_INVENTORY.md`

---

## 1. Executive Summary

The KernelTick project is an experimental, Linux-based low-latency electronic trading engine designed to study operating system performance, thread scheduling, synchronization mechanics, and hardware-software interaction in financial computing. Over five development phases, the engine advanced from baseline benchmarking to memory optimization, concurrency synchronization, CPU core affinity, real-time thread scheduling, and virtual memory locking.

In accordance with Phase 6 objectives, this document establishes a consolidated inventory of all project deliverables, conducts a source-level and mathematical audit of experimental data across all phases, verifies consistency between raw CSV records and published reports, and identifies information gaps to address prior to producing the final engineering report.

> [!IMPORTANT]
> **Audit Integrity Standard:** No benchmark implementations were altered, no raw experimental datasets were modified or regenerated, and no results were fabricated. All aggregates reported herein have been mathematically verified against individual run records in the project CSV files.

---

## 2. Project Deliverables Audit

### 2.1 Benchmark Source Files

| Source File | Primary Executable Target | Language / Standard | Responsibilities & Capabilities |
| :--- | :--- | :--- | :--- |
| [`benchmarks/order_book_benchmark.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/order_book_benchmark.cpp) | `OrderBookBenchmark` | C++20 (`-O3 -march=native`) | Evaluates limit order book insertion (40 price levels), order cancellation, crossing trades with partial fills, and mixed trading order flow. |
| [`benchmarks/queue_benchmark.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/queue_benchmark.cpp) | `QueueBenchmark` | C++20 (`-O3 -march=native`) | Core SPSC queue benchmarking framework supporting buffer capacities (1K, 64K), CPU affinity (`pthread_setaffinity_np`), POSIX real-time scheduling (`SCHED_FIFO`), and memory locking (`mlockall`). |
| [`benchmarks/memory_benchmark.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/memory_benchmark.cpp) | `MemoryBenchmark` | C++20 (`-O3 -march=native`) | Evaluates dynamic allocation (`new`/`delete` via glibc ptmalloc) against a preallocated lock-free object pool across 4 discrete experiments (allocation, batch free, reuse, sustained churn). |
| [`benchmarks/concurrency_benchmark.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/concurrency_benchmark.cpp) | `ConcurrencyBenchmark` | C++20 (`-O3 -march=native`) | Comparative study of inter-thread synchronization: mutex-protected bounded queue (`MutexQueue`) versus lock-free single-producer single-consumer ring buffer (`SPSCQueue`). |
| [`benchmarks/benchmark_utils.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/benchmark_utils.hpp) | Header Library | C++20 | Shared benchmarking infrastructure: monotonic clocking (`std::chrono::steady_clock`), Hyndman–Fan Type 7 rank interpolation percentiles, barrier skew tracking, compiler clobbers (`doNotOptimize`), CSV serialization, and rusage accounting. |

### 2.2 Benchmark CSV Data Files

All raw experimental measurements are preserved in the [`benchmark_results/`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results) directory:

| CSV Destination File | Total Lines | `RUN` Records | `AGGREGATE` Records | Fields per Record | Schema Version & Coverage |
| :--- | :---: | :---: | :---: | :---: | :--- |
| [`benchmark_results/order_book_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/order_book_benchmark.csv) | 17 | 12 | 4 | 25 | Phase 2 Order Book baseline (4 workloads $\times$ 3 runs). |
| [`benchmark_results/queue_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/queue_benchmark.csv) | 9 | 6 | 2 | 34 | Phase 2 Queue baseline (2 capacities $\times$ 3 runs). |
| [`benchmark_results/memory_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/memory_benchmark.csv) | 61 | 50 | 10 | 34 | Phase 3 Memory allocations (10 configurations $\times$ 5 runs). |
| [`benchmark_results/concurrency_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/concurrency_benchmark.csv) | 25 | 20 | 4 | 34 | Phase 4 Concurrency comparison (4 configurations $\times$ 5 runs). |
| [`benchmark_results/cpu_affinity_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/cpu_affinity_benchmark.csv) | 25 | 20 | 4 | 37 | Phase 5.1 CPU Affinity (4 configurations $\times$ 5 runs). |
| [`benchmark_results/scheduling_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/scheduling_benchmark.csv) | 25 | 20 | 4 | 40 | Phase 5.2 Scheduling Policy (4 configurations $\times$ 5 runs). |
| [`benchmark_results/memory_locking_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/memory_locking_benchmark.csv) | 25 | 20 | 4 | 47 | Phase 5.3 Memory Locking & Page Faults (4 configurations $\times$ 5 runs). |
| **Total Experimental Records** | **187** | **148** | **32** | — | **100% complete coverage across all phases.** |

### 2.3 Benchmark README Documents

| Documentation File | Phase Addressed | Key Focus Areas |
| :--- | :--- | :--- |
| [`benchmarks/README.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/README.md) | Phase 2 Baseline | Methodology foundations: barrier synchronization, start-time skew warnings, duration boundaries, separation of operation vs. dwell latency, dead-code elimination, Hyndman–Fan Type 7 percentiles, and VirtualBox virtualization notice. |
| [`benchmarks/memory_benchmark_README.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/memory_benchmark_README.md) | Phase 3 Memory | Dynamic heap (`new`/`delete` / ptmalloc) vs. preallocated object pool (`OrderMemoryPool`). Documents architectural fixes for stack overflow, ABA counter, placement new, and fallback tracking. |
| [`benchmarks/concurrency_benchmark_README.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/concurrency_benchmark_README.md) | Phase 4 Concurrency | Mutex-protected bounded queue (`MutexQueue`) vs. lock-free Vyukov ring buffer (`SPSCQueue`). Analysis of cache-line contention, bus invalidation traffic, and tail latency divergence. |
| [`benchmarks/cpu_affinity_README.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/cpu_affinity_README.md) | Phase 5.1 CPU Affinity | Default Linux Completely Fair Scheduler vs. thread pinning (`pthread_setaffinity_np`). Asymmetric effects across 1K backpressure-sensitive vs. 64K burst-absorbing queues under hypervisor multiplexing. |
| [`benchmarks/scheduling_benchmark_README.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/scheduling_benchmark_README.md) | Phase 5.2 Scheduling Policy | `SCHED_OTHER` vs. POSIX real-time `SCHED_FIFO` (priority 10). Permission model (`cap_sys_nice`), preemption elimination, variance reduction, and kernel policy verification via `pthread_getschedparam`. |
| [`benchmarks/memory_locking_benchmark_README.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/memory_locking_benchmark_README.md) | Phase 5.3 Memory Locking | Linux virtual memory locking (`mlockall`). Wire-locking mechanics, `RLIMIT_MEMLOCK` limits, `cap_ipc_lock` resolution, qualified page-fault analysis, workload-dependent trade-offs, and VirtualBox hypervisor boundaries. |

### 2.4 Correctness Test Suites

1. **Order Book & Matching Engine Unit Test Suite:**
   - **File:** [`tests/order_book_tests.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/tests/order_book_tests.cpp)
   - **Target:** `OrderBookTests`
   - **Status:** **144 test assertions**, 100% passing (0 failures).
   - **Coverage:** Order insertion, rejection of duplicate order IDs, partial quantity fills, multi-order level clearing, order cancellation at best bid/ask, order book reconstruction, trade execution verification, and crossed book resolution.
2. **Pre-Benchmark Concurrency Verification:**
   - **Location:** Embedded in [`benchmarks/concurrency_benchmark.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/concurrency_benchmark.cpp).
   - **Scope:** Automated verification of boundary conditions (full rejection, empty rejection, strict FIFO ordering) and 100,000-message concurrent monotonic integrity test for both `SPSCQueue` and `MutexQueue`.
3. **Pre-Benchmark Memory Pool Verification:**
   - **Location:** Embedded in [`benchmarks/memory_benchmark.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/memory_benchmark.cpp).
   - **Scope:** Automated verification of pool allocation boundaries, fallback allocation tracking, multi-cycle slot recycling, and clean placement-new default initialization.

### 2.5 Production Implementation Files

- **Order Matching Engine:**
  - [`include/order_matching/OrderBook.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/order_matching/OrderBook.hpp) & [`src/order_matching/OrderBook.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/src/order_matching/OrderBook.cpp): Fast limit order book using doubly linked lists per price level and price index ladders.
  - [`include/order_matching/Order.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/order_matching/Order.hpp): Cache-friendly 32-byte POD order representation with explicit price-time priority timestamps.
  - [`include/order_matching/Matcher.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/order_matching/Matcher.hpp) & [`src/order_matching/Matcher.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/src/order_matching/Matcher.cpp): Deterministic continuous double-auction matching engine with trade generation.
- **Utilities & Concurrency Primitives:**
  - [`include/utils/SPSCQueue.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/utils/SPSCQueue.hpp): Lock-free, bounded single-producer single-consumer ring buffer with `alignas(64)` cache-line separation and C++20 acquire/release memory orders.
  - [`include/utils/MutexQueue.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/utils/MutexQueue.hpp): Thread-safe bounded queue synchronized via `std::mutex` with matching non-blocking retry interface.
  - [`include/utils/MemoryPool.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/utils/MemoryPool.hpp): Lock-free, cache-aligned object pool utilizing a 64-bit atomic Treiber stack with 32-bit ABA generation counter.
  - [`include/utils/Timestamp.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/utils/Timestamp.hpp): High-resolution monotonic and hardware TSC timestamping utilities.
  - [`include/utils/AsyncLogger.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/utils/AsyncLogger.hpp): Non-blocking asynchronous logging subsystem.
  - [`include/utils/LinuxOptimizations.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/utils/LinuxOptimizations.hpp): Linux-specific kernel tuning wrappers (HugePages, scheduler policy, memory locks).
  - [`include/utils/Benchmark.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/utils/Benchmark.hpp): Initial benchmarking harness.
- **Market Data & Networking Infrastructure:**
  - [`include/market_data/MarketDataHandler.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/market_data/MarketDataHandler.hpp) & [`src/market_data/MarketDataHandler.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/src/market_data/MarketDataHandler.cpp): ITCH market data parser and dispatcher.
  - [`include/networking/EpollServer.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/networking/EpollServer.hpp), `Server.hpp`, `UDPServer.hpp`, `Client.hpp`: Linux `epoll`-based edge-triggered network I/O servers.

### 2.6 Existing Project Documentation

- [`README.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/README.md): High-level repository architecture, feature summary, and initial build guide.
- [`HFT_OPTIMIZATIONS.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/HFT_OPTIMIZATIONS.md): Theoretical discussion of Linux kernel optimizations, CPU cache architectures, and networking tuning.
- [`todo.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/todo.md): Historical milestone tracker.

---

## 3. Structured Experiment Inventory

### Experiment 1: Baseline Performance Benchmarking (Phase 2)

- **Objective:** Establish baseline operation latency, throughput, and queue dwell dynamics for core order book operations and inter-thread lock-free queueing before applying any OS-level tuning.
- **Compared Configurations:**
  - *Order Book Operations:* Insertion of resting limit orders (`order_insertion_resting`), order cancellation (`order_cancellation`), aggressive crossing matching with partial fills (`order_matching_partial_fills`), and realistic continuous mixed flow (`order_book_mixed_workload`).
  - *SPSC Queue Sizing:* Large absorption capacity (64K slots / 65,536 elements) vs. compact backpressure-sensitive capacity (1K slots / 1,024 elements).
- **Workload & Repetitions:**
  - Order Book: 10,000 operations per workload $\times$ 3 independent repetitions (12 measured runs).
  - Queue: 1,000,000 messages per run $\times$ 3 independent repetitions (6 measured runs).
- **Metrics Collected:** Overall throughput (ops/s), operation latency ($P_{50}, P_{95}, P_{99}, P_{99.9}$, mean, min, max, std dev via Hyndman–Fan Type 7 rank interpolation), end-to-end message dwell latency, start-time barrier skew, push full retries, and pop empty retries.
- **Primary Source Files:**
  - Benchmark Code: [`benchmarks/order_book_benchmark.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/order_book_benchmark.cpp) and [`benchmarks/queue_benchmark.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/queue_benchmark.cpp)
  - CSV Data: [`benchmark_results/order_book_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/order_book_benchmark.csv) and [`benchmark_results/queue_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/queue_benchmark.csv)
  - Documentation: [`benchmarks/README.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/README.md)
- **Empirical Findings Summary:**
  - Pure resting order insertion achieved **12,546,900 ops/s** with median operation latency of **47.3 ns**.
  - Order cancellation achieved **11,599,608 ops/s** with median latency of **62.0 ns**.
  - Complex matching with partial fills required order book tree updates and trade record generation, reducing throughput to **546,668 ops/s** with median latency of **1,313.2 ns**.
  - Baseline unpinned SPSC queue throughput averaged **6.81M ops/s** (64K) and **6.54M ops/s** (1K), with high run-to-run dispersion due to unconstrained Linux scheduling.
- **Key Limitations:** Workload size of 10,000 operations for order book testing limits statistical density in the extreme tail ($P_{99.9}$ has only 10 observations). Threads executed unpinned under standard Linux CFS scheduling.

---

### Experiment 2: Memory Allocation Strategy Benchmarking (Phase 3)

- **Objective:** Evaluate how memory allocation mechanisms affect latency, throughput, and tail determinism in order lifecycle processing, comparing runtime heap allocation against a preallocated object pool.
- **Compared Configurations:**
  - *Strategy A (Dynamic Allocation):* Runtime heap management via standard C++ `new Order()` and `delete order`, serviced by glibc `ptmalloc3` with thread-local caching (`tcache`).
  - *Strategy B (Memory Pool):* Preallocated, 64-byte cache-aligned object pool (`OrderMemoryPool`) utilizing a lock-free atomic Treiber stack with 32-bit ABA generation counter and in-place placement new/destructor invocations.
- **Workload & Repetitions:** 100,000 operations per experiment $\times$ 5 independent repetitions across 4 dedicated experiments (50 measured runs):
  1. *Exp 1:* Pure allocation speed (timed loop allocations, post-timed deallocation).
  2. *Exp 2:* Independent batch allocation followed by batch deallocation.
  3. *Exp 3:* Repeated allocation and recycling across 10 successive cycles ($10 \times 10,000$ orders).
  4. *Exp 4:* Sustained continuous order flow churn with a 1,000-order sliding window.
- **Metrics Collected:** Operation throughput (ops/s), operation latency percentiles ($P_{50}, P_{95}, P_{99}, P_{99.9}$, mean, min, max, std dev), fallback allocation tracking, and recycling correctness.
- **Primary Source Files:**
  - Benchmark Code: [`benchmarks/memory_benchmark.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/memory_benchmark.cpp)
  - CSV Data: [`benchmark_results/memory_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/memory_benchmark.csv)
  - Documentation: [`benchmarks/memory_benchmark_README.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/memory_benchmark_README.md)
- **Empirical Findings Summary:**
  - *Exp 1 (Allocation):* MemoryPool delivered **6.87M ops/s** vs. **3.88M ops/s** for Dynamic (1.77x speedup); $P_{99}$ latency dropped from 3,944 ns to 93 ns (**42x tail reduction**).
  - *Exp 2 (Batch Alloc/Free):* Allocation speedup was 2.01x (6.77M vs. 3.38M ops/s); deallocation speedup was 1.16x (6.94M vs. 5.99M ops/s).
  - *Exp 3 (Recycling Across Cycles):* MemoryPool achieved **6.11M ops/s** vs. **4.00M ops/s** (1.53x speedup); $P_{99.9}$ tail latency collapsed from 3,694 ns to 136 ns (**27x reduction**), demonstrating immunity to heap fragmentation.
  - *Exp 4 (Sliding Window Churn):* Dynamic allocation achieved slightly higher raw throughput (7.65M vs. 6.88M ops/s) due to glibc tcache L1-cache hits on small 1,000-order working sets, but MemoryPool retained superior tail determinism ($P_{99.9}$ was 132 ns for pool vs. 196 ns for dynamic).
- **Key Limitations:** In Exp 4, small working sets fit entirely inside glibc's tcache bins, masking dynamic heap arena lock contention that emerges under multi-threaded cross-core order destruction. Single-threaded allocation testing does not exercise multi-threaded pool contention.

---

### Experiment 3: Concurrency Synchronization Benchmarking (Phase 4)

- **Objective:** Quantify the performance penalty, lock contention, and latency jitter induced by mutual exclusion primitives compared to a lock-free ring buffer in concurrent producer-consumer messaging.
- **Compared Configurations:**
  - *Strategy A (Mutex-Protected Queue):* Bounded FIFO queue synchronized via `std::mutex` and `std::lock_guard` with non-blocking polling interface (`MutexQueue`).
  - *Strategy B (Lock-Free SPSC Queue):* Single-Producer Single-Consumer queue using Dmitry Vyukov's ring buffer algorithm, C++20 atomic memory orders (`acquire`/`release`), and 64-byte cache-line separation (`alignas(64)`).
- **Workload & Repetitions:** 1,000,000 messages per run $\times$ 5 independent repetitions across 2 capacities (1K and 64K slots), yielding 20 measured runs.
- **Metrics Collected:** Total pipeline throughput (ops/s), worker execution durations, start-time barrier skew, operation execution latency ($P_{50}, P_{95}, P_{99}, P_{99.9}$), end-to-end message dwell latency ($P_{50}, P_{95}, P_{99}, P_{99.9}$), push retries (full buffer), and empty pop retries (starvation).
- **Primary Source Files:**
  - Benchmark Code: [`benchmarks/concurrency_benchmark.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/concurrency_benchmark.cpp)
  - CSV Data: [`benchmark_results/concurrency_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/concurrency_benchmark.csv)
  - Documentation: [`benchmarks/concurrency_benchmark_README.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/concurrency_benchmark_README.md)
- **Empirical Findings Summary:**
  - *1K Capacity:* Lock-free SPSC achieved **7.30M ops/s vs. 2.11M ops/s (3.46x speedup)**. End-to-end dwell latency at $P_{99}$ was slashed from 3.67 ms down to 344 µs (**10.7x latency compression**). MutexQueue suffered 7.42M push retries and 3.45M empty pop retries due to lock serialization on full/empty boundaries.
  - *64K Capacity:* Lock-free SPSC achieved **10.14M ops/s vs. 3.71M ops/s (2.74x speedup)**. E2E dwell latency at $P_{99}$ dropped from 90.2 ms down to 8.30 ms (**10.9x lower dwell latency**).
- **Key Limitations:** Threads executed unpinned under standard Linux CFS scheduling. MutexQueue was evaluated in a non-blocking spin-retry configuration; blocking waits with condition variables were intentionally excluded to maintain identical retry-based interface semantics.

---

### Experiment 4: CPU Affinity Benchmarking (Phase 5.1)

- **Objective:** Evaluate the impact of thread core affinity on queue throughput, cache coherency invalidation, preemption stalls, and run-to-run consistency.
- **Compared Configurations:**
  - *Experiment A (Default Linux Scheduling):* Producer and consumer threads managed dynamically by Linux Completely Fair Scheduler (CFS) with unconstrained CPU migration (`producer_cpu = -1, consumer_cpu = -1`).
  - *Experiment B (CPU Affinity):* Producer thread pinned to CPU 0, Consumer thread pinned to CPU 1 via `pthread_setaffinity_np` before barrier synchronization (`producer_cpu = 0, consumer_cpu = 1`).
- **Workload & Repetitions:** 1,000,000 messages per run $\times$ 5 independent repetitions across 2 capacities (1K and 64K slots), yielding 20 measured runs.
- **Metrics Collected:** Overall pipeline throughput, worker throughputs, start skew, operation latencies, E2E dwell latencies, failed push retries, and empty pop retries.
- **Primary Source Files:**
  - Benchmark Code: [`benchmarks/queue_benchmark.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/queue_benchmark.cpp)
  - CSV Data: [`benchmark_results/cpu_affinity_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/cpu_affinity_benchmark.csv)
  - Documentation: [`benchmarks/cpu_affinity_README.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/cpu_affinity_README.md)
- **Empirical Findings Summary:**
  - *1K Backpressure-Sensitive Queue:* CPU affinity delivered a decisive improvement. Mean throughput increased from **3.66M to 5.35M ops/s (+46.0%)**, median throughput increased from **3.16M to 5.31M ops/s (+68.0%)**, and run-to-run standard deviation dropped by 64.8%. Under default scheduling, Run #1 suffered a catastrophic **193.56-second freeze** (5,166 ops/s) with 848.5 million push retries due to CFS co-scheduling both threads onto the same vCPU. Pinned execution reduced total push retries by **99.6%** (from 859.6M to 3.1M).
  - *64K Burst-Absorbing Queue:* CPU affinity **reduced** throughput by **-47.8%** (from 10.77M to 5.62M ops/s). In this 2-vCPU virtualized environment, rigid pinning prevented the guest kernel from migrating threads when the underlying host OS preempted one virtual CPU, whereas floating threads allowed CFS to take advantage of whichever vCPU was active.
- **Key Limitations:** VirtualBox guest execution. Two virtual CPUs running as host OS threads on a heterogeneous Intel Core i7-13620H processor (P-cores vs. E-cores) introduce asymmetric core performance and hypervisor preemption that differ substantially from bare-metal isolated physical cores (`isolcpus`).

---

### Experiment 5: Linux Thread Scheduling Policy Optimization (Phase 5.2)

- **Objective:** Evaluate the impact of POSIX real-time static priority scheduling (`SCHED_FIFO`) against general-purpose time-sharing (`SCHED_OTHER` / CFS) on throughput, preemption jitter, and run consistency.
- **Compared Configurations:**
  - *Experiment A (`SCHED_OTHER`):* Normal Linux dynamic time-slice scheduling (CFS/EEVDF, nice level 0). Threads are subject to involuntary preemption when dynamic `vruntime` expires or kernel background tasks run.
  - *Experiment B (Genuine `SCHED_FIFO`):* POSIX.1b real-time first-in, first-out scheduling with fixed priority 10 applied to both worker threads via `pthread_setschedparam`. Threads run with non-preemptive priority over all normal tasks. Both threads pinned to separate cores (Producer on CPU 0, Consumer on CPU 1).
- **Workload & Repetitions:** 1,000,000 messages per run $\times$ 5 independent repetitions across 2 capacities (1K and 64K slots), yielding 20 measured runs.
- **Metrics Collected:** Pipeline throughput, worker throughputs, start skew, operation latencies, E2E dwell latencies, push retries, empty pop retries, and runtime verification of assigned scheduling policy via `pthread_getschedparam`.
- **Primary Source Files:**
  - Benchmark Code: [`benchmarks/queue_benchmark.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/queue_benchmark.cpp)
  - CSV Data: [`benchmark_results/scheduling_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/scheduling_benchmark.csv)
  - Documentation: [`benchmarks/scheduling_benchmark_README.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/scheduling_benchmark_README.md)
- **Empirical Findings Summary:**
  - *64K Capacity:* Mean throughput increased from **7.14M to 10.98M ops/s (+53.8%)**, median throughput increased from **7.32M to 10.96M ops/s (+49.6%)**, and throughput standard deviation collapsed by **52.5%** (from 1.67M down to 793K ops/s). Involuntary CFS context switch drops (which had dragged Run #2 under `SCHED_OTHER` down to 4.65M ops/s) were completely eliminated. Push retries dropped by **-40.9%** and empty pop starvation loops dropped by **-95.5%** (from 161.6K to 7.3K).
  - *1K Capacity:* Mean throughput increased from **7.53M to 11.21M ops/s (+48.9%)**, median throughput rose from **7.99M to 11.38M ops/s (+42.4%)**, push retries fell by **-49.2%**, and empty pop starvation retries dropped by **-80.2%** (from 365.8K to 72.5K).
- **Key Limitations:** Requires elevated process privileges. An initial attempt in a restricted sandbox resulted in `EPERM` fallback to `SCHED_OTHER`. Resolved cleanly by provisioning `cap_sys_nice+ep` to the binary without root execution and stripped immediately after testing. VirtualBox host preemption can still interrupt the host thread backing the guest vCPU.

---

### Experiment 6: Linux Memory Locking & Page Fault Analysis (Phase 5.3)

- **Objective:** Evaluate the impact of Linux virtual memory locking (`mlockall`) on page fault suppression, throughput, and latency consistency in an isolated SPSC queue pipeline.
- **Compared Configurations:**
  - *Experiment A (Normal Mode — Unlocked):* Standard Linux demand paging without memory locking. Anonymous memory pages are mapped and faulted into physical RAM on first access; swapper retains authority to page out anonymous memory.
  - *Experiment B (Locked Mode — `mlockall`):* All present and future process virtual memory pages are wire-locked into physical RAM frames using `mlockall(MCL_CURRENT | MCL_FUTURE)`. Operating system kernel is prohibited from paging out or swapping process memory.
  - *Strict Isolation:* Both configurations executed strictly under `SCHED_OTHER` (priority 0) with identical CPU affinity (Producer on CPU 0, Consumer on CPU 1) to prevent confounding memory locking with real-time preemption suppression.
- **Workload & Repetitions:** 1,000,000 messages per run $\times$ 5 independent repetitions across 2 capacities (1K and 64K slots), yielding 20 measured runs.
- **Metrics Collected:** Pipeline throughput, worker throughputs, start skew, operation latencies, E2E dwell latencies, push retries, empty pop retries, and process resource page-fault counters (`ru_minflt`, `ru_majflt`) sampled via `getrusage(RUSAGE_SELF)` outside timed sections.
- **Primary Source Files:**
  - Benchmark Code: [`benchmarks/queue_benchmark.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/queue_benchmark.cpp)
  - CSV Data: [`benchmark_results/memory_locking_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/memory_locking_benchmark.csv)
  - Documentation: [`benchmarks/memory_locking_benchmark_README.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/memory_locking_benchmark_README.md)
- **Empirical Findings Summary:**
  - *Workload-Dependent Performance Trade-Offs (Not Universally Beneficial):*
    - **64K Capacity:** Mean throughput improved by **+7.2%** (7.30M to 7.83M ops/s), median throughput rose by **+13.3%** (7.17M to 8.12M ops/s), and tail latency ($P_{99}$) compressed by **-12.9%** (155 ns to 135 ns). Push retries dropped by -17.3% and empty pop retries dropped by -66.7%. However, median ($P_{50}$) operation latency **increased** from 60 ns to 76 ns (+26.7%).
    - **1K Capacity:** Mean throughput **decreased by -3.3%** (8.72M to 8.43M ops/s) and median throughput **decreased by -9.4%** (8.96M to 8.12M ops/s). Median ($P_{50}$) operation latency improved (91 ns to 70 ns), but tail latency ($P_{99}$) **worsened** (157 ns to 170 ns), and empty pop retries increased by +68.0%. Peak single-run throughput reached **11.29M ops/s** in Run #4.
  - *Page-Fault Dynamics:* In Run #1, locked mode observed 185 minor page faults vs. 216 in normal mode. Across Runs #2 through #5, **$\Delta$ minor page faults was exactly 0** in both configurations due to heap and stack page reuse. Major page faults were 0 across all runs because available RAM (~867 MB) exceeded the ~20 MB working set.
- **Key Limitations:** `mlockall` prevents eligible locked pages from being swapped out, but does **not** eliminate hardware MMU page table walks or CPU TLB misses. Furthermore, in VirtualBox, guest-level memory locking operates strictly in the guest virtual-to-physical address space; it cannot guarantee host physical memory residency or eliminate hypervisor-level memory management effects.

---

## 4. Data Consistency Audit

A comprehensive verification script was executed against all 7 CSV files to validate arithmetic consistency between raw `RUN` rows and published `AGGREGATE` rows:

### 4.1 Aggregate Reproduction from Raw Run Rows

| Dataset CSV | Metric Audited | Aggregate Formula Applied | Max Discrepancy (CSV vs. Calculated) | Audit Status |
| :--- | :--- | :--- | :---: | :---: |
| `order_book_benchmark.csv` | Mean Throughput (ops/s) | $\bar{T} = \frac{1}{3} \sum_{i=1}^3 T_i$ | $0.0033\text{ ops/s}$ (rounding) | **VERIFIED** |
| `order_book_benchmark.csv` | Mean Operation $P_{50}$ (ns) | $\bar{P}_{50} = \frac{1}{3} \sum_{i=1}^3 P_{50, i}$ | $0.0000\text{ ns}$ | **VERIFIED** |
| `queue_benchmark.csv` | Mean Throughput (ops/s) | $\bar{T} = \frac{1}{3} \sum_{i=1}^3 T_i$ | $0.0033\text{ ops/s}$ (rounding) | **VERIFIED** |
| `queue_benchmark.csv` | Retry Totals (Push/Pop) | $\sum_{i=1}^3 \text{Retries}_i$ | $0\text{ retries}$ (exact integer match) | **VERIFIED** |
| `memory_benchmark.csv` | Mean Throughput (10 configs) | $\bar{T} = \frac{1}{5} \sum_{i=1}^5 T_i$ | $0.0040\text{ ops/s}$ (rounding) | **VERIFIED** |
| `memory_benchmark.csv` | Mean Operation $P_{50}$ (ns) | $\bar{P}_{50} = \frac{1}{5} \sum_{i=1}^5 P_{50, i}$ | $0.0000\text{ ns}$ | **VERIFIED** |
| `concurrency_benchmark.csv` | Mean Throughput (4 configs) | $\bar{T} = \frac{1}{5} \sum_{i=1}^5 T_i$ | $0.0040\text{ ops/s}$ (rounding) | **VERIFIED** |
| `concurrency_benchmark.csv` | Retry Totals (4 configs) | $\sum_{i=1}^5 \text{Retries}_i$ | $0\text{ retries}$ (exact integer match) | **VERIFIED** |
| `cpu_affinity_benchmark.csv` | Mean Throughput (4 configs) | $\bar{T} = \frac{1}{5} \sum_{i=1}^5 T_i$ | $0.0060\text{ ops/s}$ (rounding) | **VERIFIED** |
| `cpu_affinity_benchmark.csv` | Retry Totals (4 configs) | $\sum_{i=1}^5 \text{Retries}_i$ | $0\text{ retries}$ (exact integer match) | **VERIFIED** |
| `scheduling_benchmark.csv` | Mean Throughput (4 configs) | $\bar{T} = \frac{1}{5} \sum_{i=1}^5 T_i$ | $0.0040\text{ ops/s}$ (rounding) | **VERIFIED** |
| `scheduling_benchmark.csv` | Retry Totals (4 configs) | $\sum_{i=1}^5 \text{Retries}_i$ | $0\text{ retries}$ (exact integer match) | **VERIFIED** |
| `memory_locking_benchmark.csv` | Mean Throughput (4 configs) | $\bar{T} = \frac{1}{5} \sum_{i=1}^5 T_i$ | $0.0060\text{ ops/s}$ (rounding) | **VERIFIED** |
| `memory_locking_benchmark.csv` | Page Fault Totals ($\Delta \text{MinFlt}$) | $\sum_{i=1}^5 \Delta \text{MinFlt}_i$ | $0\text{ faults}$ (exact integer match) | **VERIFIED** |

### 4.2 Units and Dimension Consistency

1. **Throughput vs. Latency Separation:**
   - **Throughput:** Recorded strictly in operations per second (`ops/sec`), computed as $N / \text{Pipeline Duration (sec)}$.
   - **Operation Latency:** Recorded strictly in integer nanoseconds (`ns`), representing CPU execution time of individual function calls.
   - **End-to-End Latency:** Recorded strictly in nanoseconds (`ns`), representing ring buffer dwell time from enqueue timestamp to dequeue timestamp.
   - At no point were throughput and latency metrics mixed, inverted, or conflated in any CSV or documentation table.
2. **Execution Mode Integrity:**
   - In `scheduling_benchmark.csv`, all 10 `spsc_64k_sched_fifo` and `spsc_1k_sched_fifo` runs recorded `scheduling_policy = SCHED_FIFO` and `scheduling_priority = 10`, verified via `pthread_getschedparam()`. No `EPERM` fallback runs are included in the dataset.
   - In `memory_locking_benchmark.csv`, all 10 `spsc_64k_locked` and `spsc_1k_locked` runs recorded `memlock_enabled = 1` and `scheduling_policy = SCHED_OTHER`, verified via `/proc/self/status` (`VmLck: 3948 kB`).
   - In `cpu_affinity_benchmark.csv`, default runs recorded `producer_cpu = -1, consumer_cpu = -1` and affinity runs recorded `producer_cpu = 0, consumer_cpu = 1`.

---

## 5. Identification of Missing Information & Deliverable Gaps

Before authoring the final Phase 6 comprehensive report, the following five technical gaps must be resolved:

```
+-----------------------------------------------------------------------------------+
|                        PHASE 6 DOCUMENTATION GAPS TO ADDRESS                      |
+-----------------------------------------------------------------------------------+
| 1. System Architecture & Component Interaction Diagram                            |
|    - Visual flow from Network/Market Data to SPSC Queue, Matcher, and MemoryPool |
+-----------------------------------------------------------------------------------+
| 2. Consolidated Experimental Methodology Specification                             |
|    - Unified mathematical formalization of clocking, durations, and quantiles    |
+-----------------------------------------------------------------------------------+
| 3. Complete Hardware & Virtualization Profile Specification                       |
|    - Host CPU model, guest kernel configuration, cache topology, timer source     |
+-----------------------------------------------------------------------------------+
| 4. Cross-Phase Trade-Off & Optimization Synthesis Matrix                         |
|    - Direct comparative table of all 5 optimization layers and their interactions |
+-----------------------------------------------------------------------------------+
| 5. Step-by-Step Clean-Room Reproducibility Guide                                  |
|    - Exact build commands, capability setup, benchmark invocation, and cleanup   |
+-----------------------------------------------------------------------------------+
```

### Gap 1: Unified System Architecture Diagram
- **Current State:** Architecture is described verbally across multiple README files, but lacks an integrated architectural diagram illustrating how market data feed handlers, `SPSCQueue`, `MutexQueue`, `OrderMemoryPool`, `OrderBook`, `Matcher`, and the Linux kernel interface interact.
- **Requirement for Final Report:** Create a structured ASCII / Mermaid architectural flow diagram tracing the order lifecycle from incoming network message to order book price level insertion, matching trade output, and asynchronous logging.

### Gap 2: Consolidated Experimental Methodology Specification
- **Current State:** Methodological principles (C++20 barrier synchronization, start-time skew warnings, duration boundaries, Hyndman–Fan Type 7 rank interpolation, `doNotOptimize` clobbers, `getrusage` outside timed paths) are described piecemeal across individual phase READMEs.
- **Requirement for Final Report:** Synthesize these rules into a single, cohesive, formal methodology chapter detailing equations, timing mechanisms, and validation criteria.

### Gap 3: Detailed Hardware & Virtualization Profile
- **Current State:** The execution environment is referred to as "VirtualBox Linux Guest (2 vCPUs)".
- **Requirement for Final Report:** Provide the exact system profile:
  - Host Processor: 13th Gen Intel Core i7-13620H (10 cores / 16 threads: 6 P-cores up to 4.9 GHz, 4 E-cores up to 3.6 GHz, 24 MB Intel Smart Cache).
  - Hypervisor: Oracle VirtualBox 7.x, 2 virtual CPUs allocated, ACPI enabled, nested paging / EPT enabled.
  - Guest OS: Debian / Kali GNU/Linux Rolling x86_64, Linux Kernel `6.19.14+kali-amd64 #1 SMP PREEMPT_DYNAMIC`.
  - Memory: 2,058,400 kB (~2.01 GB) RAM, 999,420 kB swap.
  - Compiler Toolchain: GCC 15.2.0, GNU ld 2.44, C++20 standard, flags: `-O3 -march=native -mtune=native -flto -ffast-math -funroll-loops -fno-exceptions -fno-rtti`.
  - Monotonic Clock Source: `clock_gettime(CLOCK_MONOTONIC)` / `std::chrono::steady_clock` (paravirtualized KVM/tsc clock source).

### Gap 4: Cross-Phase Optimization Synthesis & Interaction Matrix
- **Current State:** Each phase analyzes its optimization in isolation.
- **Requirement for Final Report:** Provide an overarching synthesis matrix comparing all optimization layers across both small (1K) and large (64K) buffer capacities, contrasting throughput gains, tail latency effects, retry reductions, and implementation complexities.

### Gap 5: Clean-Room Reproducibility Instructions
- **Current State:** Build instructions are scattered across multiple markdown files.
- **Requirement for Final Report:** Provide a single, consolidated shell script / command sequence that builds all targets, runs correctness test suites, applies necessary file capabilities, executes all 6 benchmark suites, and exports clean CSVs.

---

## 6. Summary Cross-Phase Benchmark Comparison

The table below unifies the primary performance results across all experimental phases for the 1,000,000-message queue workloads:

| Phase | Optimization Layer | Capacity | Primary Benchmark Variant | Mean Throughput (ops/s) | Median Throughput (ops/s) | Op Latency $P_{50}$ | Op Latency $P_{99}$ | Push Retries (Total) | Pop Empty Retries (Total) |
| :---: | :--- | :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Phase 4** | Synchronization | 1K | MutexQueue (Bounded Mutex) | 2,106,730 | 1,947,144 | 77 ns | 1,046 ns | 7,419,554 | 3,449,807 |
| **Phase 4** | Synchronization | 1K | SPSCQueue (Lock-Free Atomics) | **7,298,283** *(+246%)* | **6,989,897** | 69 ns | 162 ns | **4,112,943** | **1,635,048** |
| **Phase 4** | Synchronization | 64K | MutexQueue (Bounded Mutex) | 3,705,156 | 4,159,856 | 48 ns | 833 ns | 8,958,279 | 132,683 |
| **Phase 4** | Synchronization | 64K | SPSCQueue (Lock-Free Atomics) | **10,143,779** *(+174%)* | **10,210,258** | 51 ns | 149 ns | **2,482,967** | **165,803** |
| **Phase 5.1** | CPU Pinning | 1K | Default Linux CFS (Unpinned) | 3,662,916 | 3,158,133 | 49 ns | 140 ns | 859,572,202 | 1,448,362,982 |
| **Phase 5.1** | CPU Pinning | 1K | CPU Affinity (Pinned CPU 0, 1) | **5,348,178** *(+46%)* | **5,306,553** | 93 ns | 271 ns | **3,132,885** *(-99.6%)* | **1,131,541** |
| **Phase 5.1** | CPU Pinning | 64K | Default Linux CFS (Unpinned) | **10,774,319** | **11,131,676** | 57 ns | 156 ns | **1,717,604** | **426,793** |
| **Phase 5.1** | CPU Pinning | 64K | CPU Affinity (Pinned CPU 0, 1) | 5,620,100 *(-48%)* | 6,439,483 | 74 ns | 231 ns | 2,953,953 | 177,969 |
| **Phase 5.2** | Scheduling Policy | 1K | `SCHED_OTHER` (CFS Normal) | 7,529,222 | 7,994,561 | 83 ns | 158 ns | 5,693,596 | 365,814 |
| **Phase 5.2** | Scheduling Policy | 1K | `SCHED_FIFO` (Real-Time Prio 10) | **11,209,401** *(+49%)* | **11,383,794** | **66 ns** | **153 ns** | **2,894,289** *(-49%)* | **72,497** *(-80%)* |
| **Phase 5.2** | Scheduling Policy | 64K | `SCHED_OTHER` (CFS Normal) | 7,141,395 | 7,324,097 | 56 ns | 147 ns | 4,454,003 | 161,592 |
| **Phase 5.2** | Scheduling Policy | 64K | `SCHED_FIFO` (Real-Time Prio 10) | **10,981,501** *(+54%)* | **10,958,271** | 70 ns | 164 ns | **2,630,194** *(-41%)* | **7,262** *(-95%)* |
| **Phase 5.3** | Virtual Memory | 1K | Normal Mode (Unlocked) | **8,721,639** | **8,960,065** | 91 ns | **157 ns** | 3,704,931 | **651,032** |
| **Phase 5.3** | Virtual Memory | 1K | Locked Mode (`mlockall`) | 8,431,807 *(-3%)* | 8,115,337 | **70 ns** | 170 ns | **3,623,212** | 1,093,538 |
| **Phase 5.3** | Virtual Memory | 64K | Normal Mode (Unlocked) | 7,301,222 | 7,165,565 | **60 ns** | 155 ns | 4,297,649 | 419,072 |
| **Phase 5.3** | Virtual Memory | 64K | Locked Mode (`mlockall`) | **7,828,590** *(+7%)* | **8,120,419** | 76 ns | **135 ns** *(-13%)* | **3,552,949** *(-17%)* | **139,718** *(-67%)* |

---

## 7. Next Steps for Phase 6 Final Reporting

1. **Synthesize the Final Engineering Report:** Author the overarching KernelTick Final Technical Report combining architecture, methodology, empirical findings, and production recommendations.
2. **Incorporate Architecture Diagrams:** Embed clear structural diagrams illustrating data flow through the trading engine pipeline and OS interface layers.
3. **Consolidate Production Guidelines:** Provide concrete recommendations for bare-metal HFT deployment (core isolation, tickless kernel, HugePages, NUMA binding, `SCHED_FIFO`, `mlockall`).
4. **Final Repository Packaging & Verification:** Execute a final clean build, test suite execution, and verify tree cleanliness.
