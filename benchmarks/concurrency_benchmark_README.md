# KernelTick Phase 4: Concurrency Synchronization Benchmarking

A rigorous empirical comparison of synchronization mechanisms for **KernelTick**, an experimental Linux-based low-latency trading engine designed to study operating system latency, throughput, and performance consistency.

---

## 1. Overview & Objective

In modern financial exchanges and electronic market-making systems, inter-thread messaging between network/market data feed handlers (producers) and matching/order book engines (consumers) represents a critical latency choke point.

The objective of **KernelTick Phase 4** is to study the performance trade-offs between two primary synchronization paradigms:
1. **Strategy A (Mutex-Protected Bounded Queue):** A thread-safe bounded FIFO queue synchronized via a standard POSIX/glibc mutex (`std::mutex` and `std::lock_guard`).
2. **Strategy B (Lock-Free SPSC Queue):** A bounded Single-Producer Single-Consumer queue based on Dmitry Vyukov's ring buffer design, utilizing C++20 atomics (`acquire`/`release` memory orders) and cache-line separation.

The investigation evaluates how queue capacity (1K vs 64K) and lock contention affect throughput, operation latency, end-to-end dwell latency, retry counts, and tail latency consistency ($P_{50}, P_{95}, P_{99}, P_{99.9}$).

---

## 2. Synchronization Mechanisms & Architectural Comparison

### Strategy A: Mutex-Protected Queue (`MutexQueue`)
- **Implementation:** Defined in [`include/utils/MutexQueue.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/utils/MutexQueue.hpp).
- **Interface:** Exposes a non-blocking, retry-based interface matching the SPSC queue (`push()` returns `false` when full; `pop()` returns `false` when empty). No condition variables or blocking kernel sleeps are used.
- **Synchronization Cost:**
  - Every `push()` and `pop()` must acquire and release `mutex_`.
  - In Linux (NPTL / glibc), `std::mutex` is implemented using an atomic compare-and-swap (CAS) fast path in user space. If uncontended, it acquires the lock with atomic instruction overhead.
  - **Cache Line Contention:** Both the producer and consumer threads must read and modify the same cache line containing the mutex lock word. In multi-core systems, the cache-coherency protocol (MESI) forces frequent cache-line invalidation and transfer between cores.
  - **Backpressure Thrashing:** Under high contention (especially in small queues where the queue frequently becomes full or empty), threads repeatedly attempt `lock_guard` acquisitions in spin loops, driving up bus traffic and causing lock contention spikes.

### Strategy B: Lock-Free SPSC Queue (`SPSCQueue`)
- **Implementation:** Defined in [`include/utils/SPSCQueue.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/utils/SPSCQueue.hpp).
- **Single-Writer Principle:**
  - Only the producer modifies `write_pos_`.
  - Only the consumer modifies `read_pos_`.
- **Memory Ordering:**
  - Producer writes data into `buffer_[current_write]` and commits `write_pos_` with `std::memory_order_release`. It reads `read_pos_` with `std::memory_order_acquire`.
  - Consumer reads data from `buffer_[current_read]` and commits `read_pos_` with `std::memory_order_release`. It reads `write_pos_` with `std::memory_order_acquire`.
- **Cache-Line Alignment (`alignas(64)`):**
  - `write_pos_`, `buffer_`, and `read_pos_` reside on separate 64-byte hardware cache lines. This guarantees **zero false sharing** between cores.
- **Zero Lock Contention:** Neither thread ever blocks or acquires an exclusive lock on the other's state variables.

---

## 3. Benchmark Methodology

The concurrency benchmark suite is implemented in [`benchmarks/concurrency_benchmark.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/concurrency_benchmark.cpp) and executed via the `ConcurrencyBenchmark` target.

### Experimental Controls
1. **Barrier Synchronization (`std::barrier`):** Producer and consumer synchronize their rendezvous before launch. The exit timestamp skew ($t_{\text{prod\_start}} - t_{\text{cons\_start}}$) is recorded and evaluated against a strict 5% pipeline warning threshold.
2. **Accurate Duration Boundaries:** Timed intervals run strictly from the earliest worker release to the latest worker completion:
   $$\text{Duration} = \max(t_{\text{prod\_end}}, t_{\text{cons\_end}}) - \min(t_{\text{prod\_start}}, t_{\text{cons\_start}})$$
   Thread creation and joining overheads are completely excluded.
3. **Dual Latency Tracking:**
   - **Operation Latency:** CPU execution time of `queue.pop()` (recorded per message).
   - **End-to-End (E2E) Dwell Latency:** Elapsed time from message enqueue to dequeue ($t_{\text{pop}} - t_{\text{enqueue}}$), measuring ring buffer dwell delay.
4. **Contention Counters:** Failed push attempts (`push_retries` due to full buffer) and failed pop attempts (`empty_pop_retries` due to empty buffer) are recorded without adding cache-line bouncing.
5. **NIST Type 7 Percentiles:** $P_{50}, P_{95}, P_{99}, P_{99.9}$ are computed using linear rank interpolation.
6. **Optimization Barriers:** `doNotOptimize()` memory clobbers prevent compiler dead-code elimination.

---

## 4. Correctness Verification

Before benchmarking, an automated correctness suite tests both queues at 1K and 64K capacities:
- **Boundary Verification:** Exactly fills to capacity; verifies subsequent `push()` fails; pops all messages and verifies FIFO order; verifies subsequent `pop()` fails.
- **Concurrent Integrity Test:** Streams 100,000 messages across concurrent producer and consumer threads. The consumer verifies that every received sequence number is strictly monotonic ($0, 1, \dots, N-1$), guaranteeing zero lost, duplicated, or reordered messages.

```
>>> Running Pre-Benchmark Correctness Verification Tests <<<
  [PASS] SPSCQueue (Capacity 1023): FIFO ordering, capacity bounds, and concurrent delivery verified.
  [PASS] MutexQueue (Capacity 1023): FIFO ordering, capacity bounds, and concurrent delivery verified.
  [PASS] SPSCQueue (Capacity 65535): FIFO ordering, capacity bounds, and concurrent delivery verified.
  [PASS] MutexQueue (Capacity 65535): FIFO ordering, capacity bounds, and concurrent delivery verified.
All correctness tests passed successfully.
```

---

## 5. Empirical Results (1,000,000 Messages, 5 Independent Runs)

Executed on Debian Linux (Kernel 6.12, GCC 15.2.0 `-O3 -march=native C++20`, 2 vCPUs inside VirtualBox):

### Summary Comparison Table (Averages Across 5 Repetitions)

| Queue Capacity | Queue Type | Throughput (ops/s) | Op $P_{50}$ (ns) | Op $P_{99}$ (ns) | E2E $P_{50}$ (ns) | E2E $P_{99}$ (ns) | Push Retries | Speedup |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **1K Capacity (1,024 slots)** | MutexQueue | 2,106,730 | 77 ns | 1,046 ns | 290,376 ns (290 µs) | 3,667,962 ns (3.67 ms) | 1,483,910 | baseline |
| | **SPSCQueue** | **7,298,283** | **69 ns** | **162 ns** | **94,810 ns (94.8 µs)** | **343,527 ns (343 µs)** | **822,588** | **3.46x Throughput (10.7x lower E2E $P_{99}$)** |
| **64K Capacity (65,536 slots)** | MutexQueue | 3,705,156 | 48 ns | 833 ns | 15,176,239 ns (15.2 ms) | 90,238,020 ns (90.2 ms) | 1,791,655 | baseline |
| | **SPSCQueue** | **10,143,779** | **51 ns** | **149 ns** | **5,686,718 ns (5.69 ms)** | **8,299,494 ns (8.30 ms)** | **496,593** | **2.74x Throughput (10.9x lower E2E $P_{99}$)** |

---

## 6. Deep Technical Analysis & Findings

### 1. Throughput & Contention Gap
- **1K Capacity:** The lock-free SPSC queue achieves **7.30M ops/s vs 2.11M ops/s (3.46x speedup)**. In a small 1K buffer, the queue frequently transitions between full and empty states. In `MutexQueue`, every state transition involves lock acquisitions that repeatedly serialize both threads on the same mutex cache line.
- **64K Capacity:** The lock-free SPSC queue achieves **10.14M ops/s vs 3.71M ops/s (2.74x speedup)**. The larger buffer absorbs bursts and reduces full/empty transitions, allowing `MutexQueue` throughput to increase from 2.11M to 3.71M ops/s. However, SPSC still dominates because lock acquisition overhead is completely absent.

### 2. Tail Latency Protection ($P_{99}$ and $P_{99.9}$)
- **Operation Execution Tail:** In `MutexQueue`, while median operation latency ($P_{50}$) is 48–77 ns, $P_{99}$ latency climbs to **833 – 1,046 ns** whenever a thread arrives while the peer holds the mutex. In `SPSCQueue`, $P_{99}$ remains deterministic at **149 – 162 ns** (a **5.6x to 6.5x reduction** in tail latency).
- **End-to-End Transit Delay:** In `MutexQueue`, end-to-end dwell latency reaches **3.67 ms (1K)** and **90.2 ms (64K)** at $P_{99}$, compared to **0.34 ms (1K)** and **8.30 ms (64K)** in `SPSCQueue` (a **10.7x to 10.9x reduction**).

### 3. Contention Retries
- In `MutexQueue`, spinning threads repeatedly attempt `lock_guard` acquisitions, yielding over **1.48M to 1.79M failed push retries** during the 1M message transfer.
- In `SPSCQueue`, push retries drop by more than **50% to 70%** because the producer can check `read_pos_` via atomic loads without locking.

---

## 7. Trade-offs & Limitations: Is Lock-Free Universally Better?

While the lock-free SPSC queue demonstrates decisive performance advantages in this benchmark, it is critical not to claim that lock-free is universally superior:

1. **Topology Restrictions:**
   - `SPSCQueue` is strictly single-producer single-consumer. It cannot safely handle multiple producers or multiple consumers without introducing CAS loops (MPMC queues), which suffer from their own severe cache contention under load.
   - `MutexQueue` trivially scales to any number of concurrent producers and consumers (MPMC) with identical code.
2. **CPU Spinning & Oversubscription:**
   - In our retry-based design, both queues spin using `cpuPause()` when full or empty. On systems with oversubscribed CPU cores (more active threads than physical hardware cores), continuous user-space spinning wastes CPU cycles and delays the peer thread from being scheduled by the OS kernel.
   - Under heavy oversubscription, blocking primitives (such as futex-backed condition variables or semaphores) that yield the CPU to the OS scheduler can provide better overall system throughput.

---

## 8. VirtualBox & Hypervisor Caveats

All benchmarks were conducted inside a VirtualBox virtual machine with 2 virtual CPUs. Specific virtualization dynamics observed:
- **vCPU Scheduling Jitter:** The host operating system schedules the two guest vCPUs. If the host temporarily preempts one vCPU, the running guest thread spins inside `cpuPause()`, inflating retry counts and end-to-end dwell times.
- **Start-Time Skew:** Barrier exit timestamps between the producer and consumer showed slight skew (typically $0.05\text{ ms} - 1.8\text{ ms}$), well within our 5% pipeline threshold.

---

## 9. How to Build and Run

### Build
```bash
cmake -S . -B build
cmake --build build -j2
```

### Run
Execute the concurrency benchmark suite (1,000,000 messages, 5 runs):
```bash
./build/bin/ConcurrencyBenchmark --messages 1000000 --runs 5
```

### Options
```
Options:
  --size, --messages <N>  Message count per run (default: 1000000)
  --runs <R>              Number of independent repetitions (default: 5)
  --csv <path>            CSV export file path (default: benchmark_results/concurrency_benchmark.csv)
  --help, -h              Display help message
```

Results are saved to [`benchmark_results/concurrency_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/concurrency_benchmark.csv).
