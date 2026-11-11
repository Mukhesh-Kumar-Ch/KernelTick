# KernelTick Phase 3: Memory Allocation Strategy Benchmarking

A comprehensive empirical evaluation of memory allocation strategies for **KernelTick**, an experimental Linux-based low-latency trading engine designed to study operating system latency, throughput, and performance consistency.

---

## 1. Overview & Objective

In high-frequency trading (HFT) and ultra-low-latency financial systems, the order lifecycle requires sub-microsecond determinism. Every incoming order must be instantiated, registered into an order book price level, matched or cancelled, and subsequently destroyed.

The objective of **KernelTick Phase 3** is to evaluate two fundamental memory management strategies:
1. **Strategy A (Dynamic Allocation):** Traditional runtime heap management via standard C++ `new` and `delete` (glibc `ptmalloc3`).
2. **Strategy B (Preallocated Memory Pool):** A custom, lock-free, cache-aligned object pool (`OrderMemoryPool`) that pre-allocates memory upfront and recycles slots without involving the operating system or runtime heap manager in the hot path.

This document details the architectural design, correctness fixes applied to `include/utils/MemoryPool.hpp`, the four dedicated benchmark experiments, and empirical performance findings obtained on Linux in a 2 vCPU environment.

---

## 2. Allocation Strategy Architecture

### Strategy A: Dynamic Allocation (`new` and `delete`)
- **Mechanism:** Leverages standard C++ operators `new Order()` and `delete order`, backed by glibc's memory allocator (`ptmalloc`).
- **Glibc Internals:**
  - **Thread Caching (`tcache`):** Glibc maintains a per-thread singly-linked list of recently freed chunks (up to 64 bins for small chunks $\le 1032$ bytes). Allocations matching cached sizes are serviced with minimal overhead without acquiring arena locks.
  - **Arena Locks & Syscalls:** When tcache bins are exhausted, requests fall back to heap arenas, which require mutex locking in multi-threaded environments, heap bin searching, coalesce operations, and occasional `brk()` / `mmap()` system calls.
  - **Latency Tail Risk:** Heap fragmentation, non-contiguous address allocations causing Translation Lookaside Buffer (TLB) misses, and allocator metadata updates induce significant latency spikes ($P_{99}$ and $P_{99.9}$ tails).

### Strategy B: Preallocated Memory Pool (`OrderMemoryPool`)
- **Mechanism:** A contiguous buffer of $N$ orders is pre-allocated on the heap with 64-byte cache alignment at initialization time.
- **Lock-Free Recycling Stack:** Utilizes an atomic Treiber stack with an index-based free list (`next_index_`) and a 64-bit atomic head:
  - Upper 32 bits: ABA generation counter (increments on every push and pop to prevent ABA hazards).
  - Lower 32 bits: Index of the current top free slot in the pool (or sentinel `0xFFFFFFFFU` if exhausted).
- **C++ Object Lifetime Semantics:**
  - Allocation: Slot index is popped lock-free via CAS; placement new `new (order) Order(...)` is invoked to construct the object in-place.
  - Deallocation: If the pointer belongs to the pool buffer (`isFromPool()`), the destructor `order->~Order()` is called and the slot index is pushed back to the free list via CAS. If the pointer was a fallback heap allocation, standard `delete` is invoked.
- **Zero Runtime Allocator Bookkeeping:** No search bins, no heap arena locks, and no runtime page allocation calls during active trading.

---

## 3. Correctness Defects Discovered & Resolved in `MemoryPool.hpp`

During the architectural inspection of the original `include/utils/MemoryPool.hpp`, several critical defects were identified and resolved:

| Defect | Original Behavior | Architectural Fix |
| :--- | :--- | :--- |
| **Stack Overflow Risk** | Pool storage was declared as an inline member `alignas(64) std::array<Order, PoolSize> pool_`. At the default 1M orders, `sizeof(OrderMemoryPool)` was $\approx 32\text{ MB}$. Instantiating a pool on the stack caused an instant segmentation fault (Linux default stack is 8 MB). | Buffer storage (`pool_`) and index table (`next_index_`) are now dynamically allocated on the heap upon construction using `posix_memalign` (64-byte alignment). |
| **No Slot Recycling (Deallocation No-Op)** | In `deallocate()`, the code only cleared bytes via `memset`, but never recycled the index back to any free list or updated `next_free_`. Once 1M orders were allocated, the pool was permanently exhausted and silently routed all subsequent orders to `new Order()`. | Implemented a lock-free Treiber stack of slot indices with 32-bit ABA generation counter. `deallocate()` pushes the slot index back onto the free stack, enabling infinite reuse. |
| **Undefined Object Lifetime** | The implementation called `std::memset(order, 0, sizeof(Order))` on non-trivial C++ objects, triggering compiler warnings (`-Wclass-memaccess`) and violating C++ object lifetime rules. | Replaced `memset` with placement new `new (order) Order(...)` on allocation and explicit destructor invocation `order->~Order()` on deallocation. |
| **Silent Fallback Leaks** | The pool had no counter or tracking for heap fallback allocations when capacity was exceeded. | Added `fallback_allocations_`, `allocated_count_`, and pointer bounds check `isFromPool(const Order*)` to guarantee that fallback orders are cleanly deleted and verified. |

---

## 4. Benchmark Methodology & Experiments

The memory benchmarking executable (`MemoryBenchmark`) implements four focused experiments:

### Experiment 1: Pure Allocation Only
- **Objective:** Measure pure allocation execution speed without deallocation overhead.
- **Methodology:** Pre-allocates pointer vector `std::vector<Order*> ptrs(N)`. Timed loop allocates $N$ orders (`new Order()` vs `pool.allocate()`), recording each individual allocation's nanosecond latency. Memory cleanup occurs strictly **after** the timed loop.

### Experiment 2: Allocation Followed by Batch Deallocation
- **Objective:** Isolate and measure allocation latency and deallocation latency independently.
- **Methodology:**
  - **Phase A (Timed Allocation):** Allocates $N$ orders, capturing individual nanosecond allocation latencies.
  - **Phase B (Timed Deallocation):** Deallocates all $N$ orders (`delete ptrs[i]` vs `pool.deallocate(ptrs[i])`), capturing individual deallocation latencies.
  - Verifies that `pool.allocatedCount() == 0` upon completion.

### Experiment 3: Repeated Allocation and Reuse Across Cycles
- **Objective:** Measure memory recycling behavior, heap fragmentation overhead, and cache warmth across multiple cycles.
- **Methodology:** Executes 10 successive cycles of $N / 10$ orders. In each cycle, $B$ orders are allocated, stamped with unique markers, and then deallocated.
- **Recycling Correctness Check:** For the memory pool, verifies that freshly allocated recycled orders are cleanly default-initialized (id=0, quantity=0), proving that no stale data leaks across cycles.

### Experiment 4: Sustained Workload (FIFO Sliding Window Churn)
- **Objective:** Simulate steady-state continuous order flow and realistic order book churn.
- **Methodology:** Maintains a fixed active window of $W = 1,000$ active orders. For $N$ operations:
  - In each step $i$: allocates a new order and pushes it into the sliding window.
  - If $i \ge W$: pops the oldest active order from the window and deallocates it.
  - Measures the combined operational latency per churn step.
  - Drains and cleans up remaining window orders outside the timed section.

---

## 5. Empirical Results & Performance Analysis

The following empirical results were obtained by running `./build/bin/MemoryBenchmark --size 100000 --runs 5` on Linux (Kernel 6.12, GCC 15.2.0 `-O3 -march=native C++20`, 2 vCPUs):

### Aggregate Comparison Table (Across 5 Independent Repetitions)

| Experiment | Strategy | Mean Throughput (ops/s) | Mean Latency (ns) | $P_{50}$ (ns) | $P_{95}$ (ns) | $P_{99}$ (ns) | $P_{99.9}$ (ns) | Speedup / Reduction |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **1. Pure Allocation Only** | Dynamic | 3,877,793 | 253.2 ns | 113 ns | 164 ns | 3,944 ns | 9,200 ns | baseline |
| | **MemoryPool** | **6,873,708** | **120.2 ns** | **76 ns** | **87 ns** | **93 ns** | **1,074 ns** | **1.77x throughput (53% lower mean latency)** |
| **2A. Batch Allocation** | Dynamic | 3,377,098 | 258.8 ns | 122 ns | 168 ns | 4,197 ns | 9,905 ns | baseline |
| | **MemoryPool** | **6,774,571** | **93.7 ns** | **84 ns** | **93 ns** | **100 ns** | **249 ns** | **2.01x throughput (64% lower mean latency)** |
| **2B. Batch Deallocation** | Dynamic | 5,994,013 | 119.0 ns | 83 ns | 88 ns | 91 ns | 156 ns | baseline |
| | **MemoryPool** | **6,939,411** | **92.9 ns** | **87 ns** | **95 ns** | **100 ns** | **200 ns** | **1.16x throughput (22% lower mean latency)** |
| **3. Repeated Reuse (10 Cycles)** | Dynamic | 3,996,015 | 154.8 ns | 135 ns | 180 ns | 198 ns | 3,694 ns | baseline |
| | **MemoryPool** | **6,108,196** | **85.8 ns** | **80 ns** | **87 ns** | **93 ns** | **136 ns** | **1.53x throughput (27x lower $P_{99.9}$ tail)** |
| **4. Sustained Window Churn** | Dynamic | 7,652,087 | 82.3 ns | 74 ns | 83 ns | 160 ns | 196 ns | baseline |
| | **MemoryPool** | 6,877,876 | 111.1 ns | 99 ns | 115 ns | 121 ns | 132 ns | 0.90x throughput (1.5x lower $P_{99.9}$ tail) |

---

## 6. Key Scientific Findings

### 1. Significant Throughput & Latency Superiority in Bulk and Burst Workloads
In pure allocation (Exp 1) and batch allocation (Exp 2A), the preallocated memory pool delivers **1.77x to 2.01x higher throughput** and reduces mean allocation latency from $\approx 255\text{ ns}$ down to $\approx 94 - 120\text{ ns}$.

### 2. Elimination of Extreme Tail Latency ($P_{99}$ and $P_{99.9}$)
The most critical advantage of the memory pool for high-frequency trading is consistency:
- In Dynamic Allocation, $P_{99}$ latency climbs to **3,944 – 4,197 ns** and $P_{99.9}$ reaches **9,200 – 9,905 ns** due to glibc arena lock contention, page faults, and heap metadata maintenance.
- In `OrderMemoryPool`, $P_{99}$ latency remains deterministic at **93 – 100 ns**, and $P_{99.9}$ remains below **250 ns** in batch allocations. This represents an order-of-magnitude reduction in latency jitter.

### 3. Recycling Stability & Cache Warmth (Exp 3)
In repeated allocation/deallocation cycles, the memory pool achieves **1.53x higher throughput** and reduces $P_{99.9}$ tail latency from **3,694 ns down to 136 ns (27x reduction)**. Reusing contiguous slots maintains warm CPU L1/L2 caches and avoids address space fragmentation.

### 4. Nuance in Small Sliding Window Churn (Exp 4)
In Experiment 4 (a tight 1,000-order sliding window on a single thread), Dynamic Allocation slightly outperforms the Memory Pool in throughput (7.65M ops/s vs 6.88M ops/s, 0.90x speedup):
- **Why?** Glibc's per-thread `tcache` optimizes immediate alloc/free cycles of identical size by popping and pushing from an unsynchronized singly-linked list with zero atomic operations.
- In contrast, `OrderMemoryPool` provides thread-safe concurrent allocation via atomic CAS operations (`compare_exchange_weak` with memory fences). Atomic instructions introduce small hardware pipeline serialization penalties relative to unsynchronized pointer chasing.
- **However:** Notice that even in Experiment 4, the Memory Pool's $P_{99.9}$ tail latency (132 ns) is substantially tighter than Dynamic Allocation's (196 ns).

---

## 7. OS Virtualization & Hypervisor Effects

The benchmarks were conducted inside a VirtualBox virtual machine running Debian Linux on 2 vCPUs. Key virtualization dynamics include:
1. **Virtual CPU Preemption:** Host OS scheduling interrupts can cause brief vCPU preemptions, appearing as microsecond-level outliers in individual runs.
2. **Two-Dimensional Page Walks:** Hardware-assisted paging (EPT/NPT) increases the cost of TLB misses when dynamic allocation traverses disparate heap pages, giving contiguous preallocated pools an even greater physical advantage.
3. **Monotonic Clocks:** All timings utilize `std::chrono::steady_clock` backed by the Linux `clock_gettime(CLOCK_MONOTONIC)` or calibrated TSC syscalls to ensure nanosecond precision without time drift.

---

## 8. How to Build and Run

### Build
From the repository root:
```bash
cmake -S . -B build
cmake --build build -j2
```

### Run Benchmarks
Execute the memory benchmark suite with default parameters (100,000 operations, 5 runs):
```bash
./build/bin/MemoryBenchmark --size 100000 --runs 5
```

### CLI Options
```
Options:
  --size <N>       Workload operation count per run (default: 100000, max: 1048576)
  --runs <R>       Number of independent repetitions (default: 5)
  --csv <path>     CSV export file path (default: benchmark_results/memory_benchmark.csv)
  --help, -h       Display help message
```

### Verify Output CSV
Results are saved to `benchmark_results/memory_benchmark.csv` containing both individual run records (`RUN`) and summary statistics (`AGGREGATE`) compatible with standard data analysis workflows.
