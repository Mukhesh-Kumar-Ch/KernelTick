# KernelTick: A Linux-Based Low-Latency Trading Engine for Studying Operating System Performance

**Final Technical Report — Phase 6**  
**Author / Platform:** KernelTick Research Project  
**Date:** November 4, 2026  
**Audience:** Undergraduate Computer Science & Engineering / Systems Programming  
**Authoritative Sources:** [`benchmarks/PHASE6_EXPERIMENT_INVENTORY.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/PHASE6_EXPERIMENT_INVENTORY.md) and [`benchmark_results/*.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results)

---

## 1. Project Overview

### 1.1 The Research Problem
Electronic financial markets operate at sub-microsecond timescales. In modern high-frequency trading (HFT) and automated market making, software trading algorithms process continuous streams of market quotes, submit limit orders, cancel resting depth, and match crossing trades. While modern CPU cores execute arithmetic operations and register updates in fractions of a nanosecond, operating system interactions frequently introduce non-deterministic latency spikes.

Standard general-purpose operating systems—such as Linux—are designed around fairness, throughput, and multi-user isolation rather than absolute timing determinism. Consequently, low-latency financial software regularly encounters system-level bottlenecks:
1. **Dynamic Memory Allocation Jitter:** Standard heap managers (`malloc`/`free`, `new`/`delete`) manage shared arenas, metadata bins, and thread caches (`tcache`). When allocations miss thread caches or trigger arena lock contention and kernel page allocation syscalls (`brk`/`mmap`), latency spikes by several orders of magnitude.
2. **Synchronization Contention:** Mutual exclusion locks (`std::mutex`, pthread futexes) force hardware cache-line serialization and can involuntarily suspend threads in the kernel when locks are contended.
3. **CPU Scheduling and Core Migration:** The Linux Completely Fair Scheduler (CFS/EEVDF) dynamically migrates runnable threads across CPU cores to balance system load. Cross-core migration purges L1/L2 data and instruction caches, invalidates Translation Lookaside Buffers (TLB), and introduces inter-processor interrupts (IPI).
4. **Scheduler Preemption:** Operating system timer ticks, hardware interrupts, and background kernel threads (`kworker`, `rcu_sched`, `kcompactd`) involuntarily preempt user-space polling loops, transforming tens-of-nanoseconds spinning into millisecond-scale freezes.
5. **Virtual Memory Paging:** Operating systems employ demand paging. First-time access to newly allocated virtual memory triggers minor page faults (`ru_minflt`) to allocate physical frames. Under memory pressure, pages may be evicted to swap storage, causing catastrophic major disk page faults (`ru_majflt`).

### 1.2 Why Low Latency Matters in Electronic Trading
In competitive electronic markets, execution speed is a zero-sum mechanism:
- **Price-Time Priority & Queue Position:** Financial exchanges match orders based on price first, and then arrival time (FIFO). A trading engine that processes market data 100 nanoseconds faster than a competitor establishes superior queue priority at the best bid or offer.
- **Adverse Selection:** When news or macroeconomic events move the market, slower market-making engines are "picked off" by faster participants before they can cancel resting quotes.
- **Tail Latency Dominance:** In trading architectures, average (mean) latency is rarely the metric of commercial consequence. Rather, high percentiles ($P_{99}$, $P_{99.9}$) govern operational risk. A system with a 50 ns median latency that exhibits intermittent 20 ms tail spikes will reliably suffer execution slippage, queue overflows, and financial losses during volatile trading bursts.

### 1.3 Project Objectives
The objective of the **KernelTick** project is to construct an experimental, Linux-based electronic trading engine and benchmarking harness to empirically investigate how operating system mechanisms and software engineering strategies impact throughput, latency distributions, contention retries, and run-to-run consistency.

Specifically, the project investigates:
1. **Baseline Operations:** The performance of an in-memory limit order book and lock-free Single-Producer Single-Consumer (SPSC) queue.
2. **Memory Allocation:** Dynamic runtime heap management (`new`/`delete`) versus an atomic preallocated object pool (`OrderMemoryPool`).
3. **Concurrency Synchronization:** A mutex-protected bounded queue (`MutexQueue`) versus a lock-free atomic ring buffer (`SPSCQueue`).
4. **Thread Core Affinity:** Default dynamic Linux CFS scheduling versus pinned CPU worker threads (`pthread_setaffinity_np`).
5. **Thread Scheduling Policies:** Standard Linux time-sharing (`SCHED_OTHER`) versus POSIX real-time static priority scheduling (`SCHED_FIFO`).
6. **Virtual Memory Locking:** Standard demand paging versus physical RAM wire-locking (`mlockall`).

### 1.4 Scope and Experimental Context
KernelTick is an **experimental simulated trading platform and operating system research harness**, not a commercial production trading plant:
- Workloads are generated synthetically using reproducible random or deterministic streams.
- The platform runs within an unprivileged Linux environment on a virtual machine (Oracle VirtualBox).
- Experiments are designed to study comparative systems principles—measuring the relative deltas and trade-offs of kernel mechanisms—rather than claiming absolute bare-metal commercial execution speed.

---

## 2. System Architecture

KernelTick is organized into modular components separating network I/O, inter-thread synchronization, memory management, order book state, trade matching, and benchmark measurement.

### 2.1 Component Interaction Diagram

The Mermaid diagram below reflects the actual relationships and data paths implemented in the codebase:

```mermaid
flowchart TD
    subgraph MarketDataInput["1. Market Data & Ingress"]
        direction TB
        ExtNetwork["External Network / Synthetic Generator"]
        MDHandler["MarketDataHandler\n(ITCH Parser / Message Decoder)"]
        BenchProducer["Benchmark Producer Thread\n(queue_benchmark / concurrency_benchmark)"]
        ExtNetwork -.->|Raw Packets| MDHandler
    end

    subgraph InterThreadQueue["2. Inbound Message Pipeline"]
        direction TB
        LockFreeQueue["SPSCQueue<BenchmarkMessage, Capacity>\n(Dmitry Vyukov Ring Buffer, alignas 64)"]
        MutexQ["MutexQueue<BenchmarkMessage, Capacity>\n(std::mutex & lock_guard Alternative)"]
    end

    subgraph MatchingCore["3. Execution & Order Book Core"]
        direction TB
        MatcherEngine["Matcher\n(Order Processing & Continuous Double-Auction)"]
        LimitOrderBook["OrderBook\n(Bids/Asks std::map Ladder & PriceLevel std::vector)"]
        TradeEmitter["Trade Execution\n(Atomic Two-Sided Quantity Fill)"]
        MatcherEngine <-->|Queries & Updates| LimitOrderBook
        MatcherEngine -->|Executes Fill| TradeEmitter
    end

    subgraph MemoryLayer["4. Memory Allocation Strategy"]
        direction TB
        MemPool["OrderMemoryPool\n(Contiguous Heap Buffer, Lock-Free Treiber Stack)"]
        HeapAlloc["Dynamic Allocator\n(operator new / delete via glibc ptmalloc3)"]
    end

    subgraph OutputLayer["5. Asynchronous Logging & Egress"]
        direction TB
        AsyncLog["AsyncLogger\n(Background File Logger & Ring Buffer)"]
        TradeEmitter -.->|Trade Log Records| AsyncLog
    end

    subgraph BenchmarkHarness["6. Measurement & OS Interface"]
        direction TB
        BarrierSync["std::barrier(2)\n(Rendezvous & Start Skew Timing)"]
        SteadyClock["std::chrono::steady_clock\n(Monotonic Nanosecond Clock)"]
        RUsage["getrusage(RUSAGE_SELF)\n(Page-Fault Counters)"]
        SchedAffinity["Linux OS Interface\n(pthread_setaffinity_np, SCHED_FIFO, mlockall)"]
    end

    %% Data flow connections
    MDHandler -->|Enqueues Market Orders| LockFreeQueue
    BenchProducer -->|Enqueues BenchmarkMessage| LockFreeQueue
    BenchProducer -.->|Alternative Enqueue| MutexQ

    LockFreeQueue -->|Dequeues Message| MatcherEngine
    MutexQ -.->|Alternative Dequeue| MatcherEngine

    LimitOrderBook -.->|Allocates Order POD| MemPool
    LimitOrderBook -.->|Alternative Alloc| HeapAlloc

    %% Measurement harness relationships
    BarrierSync -.->|Synchronizes Start| BenchProducer
    BarrierSync -.->|Synchronizes Start| MatcherEngine
    SteadyClock -.->|Samples Latencies| BenchProducer
    SteadyClock -.->|Samples Latencies| MatcherEngine
    RUsage -.->|Measures ru_minflt / ru_majflt| BenchProducer
    SchedAffinity -.->|Configures Cores & Policies| BenchProducer
    SchedAffinity -.->|Configures Cores & Policies| MatcherEngine
```

### 2.2 Component Responsibilities

1. **Market Data Handler ([`include/market_data/MarketDataHandler.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/market_data/MarketDataHandler.hpp)):**
   Parses inbound binary ITCH market data packets, constructs normalized order commands, and enqueues them for processing.
2. **Lock-Free SPSC Queue ([`include/utils/SPSCQueue.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/utils/SPSCQueue.hpp)):**
   Implements a Dmitry Vyukov single-producer single-consumer circular ring buffer. Head and tail indices reside on separate 64-byte hardware cache lines (`alignas(64)`) to reduce false sharing between producer and consumer. Enqueue and dequeue operations use C++20 `std::memory_order_release` and `std::memory_order_acquire`. True sharing of indices and message data still incurs standard cache-coherence protocol invalidations.
3. **Mutex Queue ([`include/utils/MutexQueue.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/utils/MutexQueue.hpp)):**
   An alternative bounded queue implementation protected by `std::mutex` and `std::lock_guard`. It provides an identical non-blocking retry-based interface (`push()` returns false when full; `pop()` returns false when empty) to isolate synchronization overhead.
4. **Order Book ([`include/order_matching/OrderBook.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/order_matching/OrderBook.hpp)):**
   Maintains the state of active orders. Price levels are stored in balanced search trees (`std::map<Price, PriceLevel>`) providing $O(\log P)$ price-level access and $O(1)$ top-of-book retrieval via cached best prices. Within each price level, orders are stored in a `std::vector<Order*>` enforcing FIFO execution. An unordered hash map (`std::unordered_map<OrderId, Order*>`) maps `order_id` to order pointers, providing $O(1)$ average-case order lookup ($O(N)$ worst-case under collisions).
5. **Trade Matcher ([`include/order_matching/Matcher.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/order_matching/Matcher.hpp)):**
   Implements continuous double-auction matching logic. Incoming aggressive orders cross resting counterparty depth. It executes partial and full fills atomically, reduces order quantities, and removes fully filled orders.
6. **Memory Pool ([`include/utils/MemoryPool.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/utils/MemoryPool.hpp)):**
   A specialized object pool for `Order` instances. It preallocates a contiguous heap block at initialization with 64-byte alignment, using an atomic Treiber stack with a 32-bit ABA generation counter to recycle slots without invoking system heap calls during runtime.
7. **Asynchronous Logger ([`include/utils/AsyncLogger.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/utils/AsyncLogger.hpp)):**
   Decouples disk I/O from the matching path. Executions and audit events are pushed into an internal ring buffer drained by a dedicated background writer thread.
8. **Benchmark Harness ([`benchmarks/benchmark_utils.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/benchmark_utils.hpp)):**
   Manages thread rendezvous via C++20 `std::barrier`, samples monotonic nanosecond timestamps (`std::chrono::steady_clock`), calculates Hyndman–Fan Type 7 quantiles, tracks process page faults (`getrusage`), applies Linux affinity and real-time scheduling APIs, and outputs structured CSV records.

---

## 3. Implementation and Correctness

### 3.1 Order Book Architecture
The order book ([`include/order_matching/OrderBook.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/order_matching/OrderBook.hpp)) is engineered around discrete price levels and index lookups:
- **Price Ladder Structure (`std::map`):** Active price levels for bids and asks are stored in `std::map<Price, PriceLevel>`, a Red-Black tree.
  - Adding or searching for a price level takes $O(\log P)$ time, where $P$ is the number of active price levels.
  - Best bid and best ask prices (`best_bid_price_`, `best_ask_price_`) are cached in member variables and updated via tree extrema (`rbegin()` / `begin()`), providing $O(1)$ top-of-book reads.
- **PriceLevel Queue (`std::vector`):** Within each `PriceLevel`, orders are stored in a `std::vector<Order*>`.
  - Appending a new resting order (`addOrder`) takes $O(1)$ amortized time via `push_back()`.
  - Retrieving the best order at a level (`getBestOrder`) takes $O(1)$ time via `front()`.
  - Removing an order from a level (`removeOrder`) performs an $O(M)$ linear scan over the vector of orders at that price level to match `order_id`, followed by vector element erasure.
- **Order Lookup Index (`std::unordered_map`):** An internal hash map (`order_map_`) maps `order_id` to `Order*`.
  - Lookups provide $O(1)$ average-case complexity, with $O(N)$ worst-case complexity under hash bucket collisions.
- **Order Cancellation Complexity:**
  - Cancelling an order by ID (`cancelOrder(id)`) executes an $O(1)$ average-case hash lookup in `order_map_` to retrieve the order pointer and its price.
  - It then executes an $O(\log P)$ tree lookup in `std::map` to locate the `PriceLevel`.
  - It performs an $O(M)$ linear scan within the `PriceLevel` vector to erase the order.
  - If the level becomes empty, removing the price level takes $O(\log P)$ time.
  - Finally, erasing the ID from `order_map_` takes $O(1)$ average-case time.

### 3.2 Matching and Partial Fill Mechanics
Continuous order matching in [`src/order_matching/Matcher.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/src/order_matching/Matcher.cpp) enforces price-time priority:
- An aggressive buy order matches resting sell orders if $\text{buy\_price} \ge \text{best\_ask}$.
- An aggressive sell order matches resting buy orders if $\text{sell\_price} \le \text{best\_bid}$.
- **Partial Fills:** If incoming order quantity is less than the resting order's quantity, the resting order's remaining volume is decremented in-place without removing it from the price level or altering its queue priority.
- **Full Fills:** When a resting order's remaining volume reaches zero, it is pruned from the linked list, unregistered from the lookup index, and cleaned up.

### 3.3 Transactional Trade Execution
In electronic exchanges, matching state transitions must be strictly atomic. The matcher implements transactional two-sided reductions in `Matcher::executeTrade(buy_order_id, sell_order_id, price, quantity)`:
- Both order IDs are validated for presence in the order book.
- Both orders are confirmed to hold available volume $\ge \text{quantity}$.
- The transaction decrements buy volume and sell volume simultaneously. If an invariant check fails prior to mutation, the entire trade is rejected with zero state mutation, preventing orphaned volume or asymmetric fills.

### 3.4 Duplicate Order ID Protection
To prevent order corruption or state overwrite:
- `OrderBook::addOrder()` queries the internal index prior to node allocation.
- If an order with an identical `order_id` already exists in the book, the insertion is immediately rejected, preserving the integrity of the original resting order.

### 3.5 Lock-Free SPSC Queue Mechanics
The `SPSCQueue` ([`include/utils/SPSCQueue.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/utils/SPSCQueue.hpp)) implements single-producer single-consumer semantics:
- Ring buffer storage is fixed at power-of-two capacity $C$.
- The producer maintains `write_pos_` and loads `read_pos_` with acquire semantics to verify capacity.
- The consumer maintains `read_pos_` and loads `write_pos_` with acquire semantics to detect available messages.
- Bitwise masking (`pos & (C - 1)`) eliminates division and modulo instructions in the critical path.
- Cache lines are separated via `alignas(64)` padding between `write_pos_`, `buffer_`, and `read_pos_`. This padding helps reduce false sharing (unrelated variables residing on the same cache line invalidating each other). However, it does not eliminate true sharing: when the producer updates `write_pos_` or message payloads and the consumer reads them, CPU cache-coherency protocols (such as MESI/MOESI) still transmit invalidation and update traffic across core interconnects.

### 3.6 Memory Pool Architecture
The `OrderMemoryPool` ([`include/utils/MemoryPool.hpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/include/utils/MemoryPool.hpp)) resolves heap fragmentation and latency spikes:
- **Lock-Free Treiber Stack:** Free slot indices are stored in a lock-free stack.
- **ABA Hazard Defense:** The atomic head pointer combines a 32-bit slot index and a 32-bit generation counter within a 64-bit word (`std::atomic<uint64_t>`). Every allocation and deallocation increments the tag counter, eliminating the ABA problem during lock-free recycling.
- **C++ Object Lifetimes:** Slot memory is allocated raw via `posix_memalign`. On allocation, placement new `new (ptr) Order(...)` initializes the object. On deallocation, `order->~Order()` explicitly destroys the object, avoiding undefined behavior from raw `memset`.

### 3.7 Correctness Test Verification
The correctness test suite in [`tests/order_book_tests.cpp`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/tests/order_book_tests.cpp) executes **144 test assertions**, all of which pass with zero failures:
```text
=== KernelTick Order Book Tests ===
[PASS] Insert buy order / sell order / best bid & ask verification
[PASS] Duplicate order ID rejection and original order preservation
[PASS] Partial fills, complete fills, and remaining quantity tracking
[PASS] Multi-order FIFO queue priority at identical price levels
[PASS] Level volume aggregation and empty level cleanup
[PASS] Best price recalculation upon complete price level exhaustion
[PASS] Transactional trade execution and rejection of invalid / excessive trades
[PASS] Self-match rejection and atomic volume reduction
Failures: 0 (All 144 assertions verified)
```

---

## 4. Experimental Environment

All empirical measurements were conducted in an audited and controlled environment. To ensure scientific accuracy, verified guest environment properties are explicitly distinguished from inferred host hardware specifications:

### 4.1 Verified Guest Environment & Toolchain
- **Guest Operating System:** Debian / Kali GNU/Linux Rolling x86_64
- **Linux Kernel Version:** `6.19.14+kali-amd64 #1 SMP PREEMPT_DYNAMIC` (verified via `uname -r`)
- **Virtualization Vendor:** Oracle VirtualBox Version 7.x (verified via `systemd-detect-virt: oracle` and CPUID hypervisor flag)
- **Guest Execution Units:** **2 vCPUs** (verified via `/proc/cpuinfo`, `processor: 0` and `processor: 1`)
- **Guest System Memory:** **2,058,400 kB (~2.01 GB) RAM**, 999,420 kB swap (verified via `/proc/meminfo`)
- **Guest Available Memory:** ~867 MB available working set during active benchmark execution
- **Compiler Toolchain:** **GCC 15.2.0** (verified via `g++ --version`), GNU ld (GNU Binutils) 2.44
- **Build System:** **CMake 4.3.4** (verified via `cmake --version`), C++ Standard 20 (`-std=c++20`)
- **Active Benchmark Compiler Flags:** `-O3 -march=native -mtune=native -flto -ffast-math -funroll-loops -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Wno-unused-parameter -std=c++20 -flto=auto -fno-fat-lto-objects` (verified via `build/CMakeFiles/QueueBenchmark.dir/flags.make`).  
  *(Note: Flags `-finline-functions` and `-fomit-frame-pointer` declared in `CMakeLists.txt` applied strictly to the `MarketDataEngine` executable target, not to benchmark executables).*
- **Monotonic Clock Source:** `CLOCK_MONOTONIC` (`std::chrono::steady_clock`)

### 4.2 Inferred Host Silicon (External Host Specifications)
- **Host Processor Model String:** `13th Gen Intel(R) Core(TM) i7-13620H` (passed through by hypervisor CPUID)
- **Inferred Physical Topology:** 10 physical cores / 16 threads (6 Performance Cores up to 4.9 GHz, 4 Efficient Cores up to 3.6 GHz, 24 MB Intel Smart Cache).
- **Virtualization Caveat:** The host operating system, power profile, CPU governor, and dynamic mapping of virtual CPUs to physical P-cores or E-cores are host-level properties that cannot be directly observed, verified, or controlled from within the virtual machine.

---

## 5. Experimental Methodology

The benchmarking framework implements statistical, timing, and synchronization protections across all test suites:

### 5.1 Barrier Synchronization and Start-Time Skew
When benchmarking concurrent producer and consumer threads, threads must not start asynchronously. Threads synchronize using a C++20 barrier (`std::barrier sync_barrier(2)`):
- Both threads arrive and wait at the barrier before executing timed loops.
- Immediately upon barrier exit, each thread records its release timestamp (`prod_t0`, `cons_t0`).
- **Start-Time Skew:**
  $$\text{Start Skew} = \text{prod\_t0} - \text{cons\_t0}$$
- If absolute start skew exceeds **5% of total pipeline elapsed time**, the run emits a warning (`[WARNING: Start skew > 5%]`). All runs, retries, and outliers are preserved in CSV outputs without filtering.

### 5.2 Accurate Workload Duration Boundaries
To eliminate thread spawning and joining overheads from throughput measurements:
- **Producer Duration:** $\Delta t_{\text{prod}} = \text{prod\_t1} - \text{prod\_t0}$
- **Consumer Duration:** $\Delta t_{\text{cons}} = \text{cons\_t1} - \text{cons\_t0}$
- **Total Pipeline Duration:**
  $$\text{Pipeline Duration} = \max(\text{prod\_t1}, \text{cons\_t1}) - \min(\text{prod\_t0}, \text{cons\_t0})$$
  This strictly measures active concurrent processing interval from the earliest thread release until the last message is consumed, completely excluding `std::thread::join()` and thread destruction syscalls.

### 5.3 Consistent Throughput Calculation
Across all queue experiments, overall throughput is calculated consistently as:
$$\text{Throughput} = \frac{N}{\text{Pipeline Duration (seconds)}}$$
Where $N$ is the total message count (1,000,000 messages).

### 5.4 Separation of Latency Metrics
1. **Operation Latency (ns):** The CPU execution time of individual function calls (`queue.pop()`, `book.addOrder()`). Measured immediately before and after the invocation.
2. **End-to-End (E2E) Message Dwell Latency (ns):** The total transit time from when a producer assigns an enqueue timestamp to when the consumer retrieves it (`dequeue_ts - enqueue_ts`). Measures buffer queueing delay.

### 5.5 Hyndman–Fan Type 7 Linear Rank Percentiles
Within each run, latency percentiles ($P_{50}, P_{95}, P_{99}, P_{99.9}$) are computed using Hyndman–Fan Type 7 linear rank interpolation ($Rank(p) = p \cdot (N - 1)$ in zero-based indexing):
$$\text{Rank}(p) = p \cdot (N - 1)$$
Given integer index $k = \lfloor \text{Rank} \rfloor$ and fractional interpolation weight $w = \text{Rank} - k$:
$$P_p = X[k] \cdot (1 - w) + X[k+1] \cdot w$$
This definition matches the standard linear interpolation convention used in Python's NumPy (`method='linear'`) and R (`type=7`). Unsupported NIST attribution has been removed, as NIST traditionally specifies the $(N+1)p$ formulation (Hyndman–Fan Type 6).

### 5.6 Dead-Code Elimination Prevention
To prevent the optimizing compiler (`-O3 -flto`) from discarding unread return values or popped structures, return values are passed through compiler memory clobbers:
```cpp
template <typename T>
inline void doNotOptimize(T& value) {
    asm volatile("" : "+m,r"(value) : : "memory");
}
```

### 5.7 Out-of-Path Page-Fault Accounting
Process resource usage is sampled via `getrusage(RUSAGE_SELF, &ru)` **strictly outside the timed pipeline**:
- `ru_before` is recorded immediately prior to barrier arrival.
- `ru_after` is recorded immediately following thread joins.
- Sampling outside the hot path guarantees zero probe-effect overhead during queue execution.

### 5.8 Virtualization Limitations of a 2-vCPU Guest
Interpreting measurements inside a 2-vCPU virtual machine requires acknowledging specific physical constraints:
1. **Hypervisor Multiplexing & Preemption:** Guest vCPUs are host OS threads. If the host kernel schedules background tasks, a vCPU thread may be paused. If an SPSC consumer is paused while a producer actively spins in user space (`cpuPause()`), false backpressure occurs.
2. **Heterogeneous Host Silicon:** The host CPU contains both high-frequency P-cores and low-frequency E-cores. VirtualBox does not guarantee symmetric core binding. If vCPU 0 is backed by a P-core and vCPU 1 is backed by an E-core, artificial producer-consumer processing imbalances arise.
3. **Absence of Hardware Cache Isolation:** The host CPU's 24 MB L3 cache is shared with the host operating system, causing occasional synthetic cache evictions.
4. **Purpose of Virtualized Benchmarking:** Measurements within this virtualized environment demonstrate **controlled comparative dynamics** (analyzing relative deltas between policies or algorithms) rather than establishing absolute bare-metal production latency guarantees.

---

## 6. Results and Comparative Analysis

The project conducted six comprehensive experimental studies. All numerical data reported below reflect the exact verified values from [`benchmarks/PHASE6_EXPERIMENT_INVENTORY.md`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmarks/PHASE6_EXPERIMENT_INVENTORY.md) and [`benchmark_results/*.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results).

---

### 6.1 Baseline Performance Benchmarking (Phase 2)

- **Objective:** Establish baseline operation latency and throughput for core limit order book actions and lock-free SPSC queueing under unpinned default Linux scheduling.
- **Workload:** 10,000 operations per workload $\times$ 3 independent runs for Order Book; 1,000,000 messages $\times$ 3 independent runs for SPSC Queue.
- **CSV Datasets:** [`benchmark_results/order_book_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/order_book_benchmark.csv) and [`benchmark_results/queue_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/queue_benchmark.csv).

#### Order Book Baseline Results (3 Repetitions)

| Workload | Operations | Mean Throughput (ops/s) | Op Latency Mean (ns) | Op Latency $P_{50}$ (ns) | Op Latency $P_{99}$ (ns) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Order Insertion (Resting)** | 10,000 | 12,546,900 | 64.8 ns | 47.3 ns | 140.3 ns |
| **Order Cancellation** | 10,000 | 11,599,608 | 72.1 ns | 62.0 ns | 112.7 ns |
| **Order Matching (Partial Fills)** | 10,000 | 546,668 | 1,819.1 ns | 1,313.2 ns | 3,584.7 ns |
| **Mixed Flow (40% B, 40% S, 10% C, 10% X)** | 10,000 | 11,162,232 | 74.2 ns | 43.3 ns | 278.0 ns |

#### Queue Baseline Results (1,000,000 Messages, 3 Repetitions)

| Capacity | Mean Throughput (ops/s) | Op Latency $P_{50}$ (ns) | Op Latency $P_{99}$ (ns) | E2E Dwell $P_{50}$ | Total Push Retries (3 Runs) | Total Pop Empty Retries (3 Runs) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **64K slots** | 6,807,353 | 62.7 ns | 189.0 ns | 7.47 ms | 1,882,305 | 113,616 |
| **1K slots** | 6,542,844 | 55.3 ns | 145.7 ns | 83.1 µs | 3,045,788 | 3,274,588 |

- **What Improved / Succeeded:** Pure insertion and cancellation demonstrated high performance exceeding 11.5M ops/s with median latencies below 65 ns.
- **What Worsened / Bottlenecks:** Crossing trade execution with partial fills dropped to 546K ops/s due to complex double-auction trade generation and order book list traversals. Baseline queue throughput exhibited noticeable variance across runs (ranging from 4.58M to 9.40M ops/s on 64K).
- **Limitations:** 10,000 operations per order book test provides insufficient sample density in upper quantiles ($P_{99.9}$ has only 10 observations). Threads ran unpinned under default CFS.

---

### 6.2 Memory Allocation Strategy Benchmarking (Phase 3)

- **Objective:** Evaluate the latency, throughput, and fragmentation trade-offs of runtime heap management (`new`/`delete` via glibc ptmalloc) versus a lock-free preallocated object pool (`OrderMemoryPool`).
- **Workload:** 100,000 operations per experiment $\times$ 5 independent runs across 4 dedicated experiments (50 measured runs).
- **CSV Dataset:** [`benchmark_results/memory_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/memory_benchmark.csv).

#### Aggregate Results (5 Repetitions per Experiment)

| Experiment | Strategy | Mean Throughput (ops/s) | Mean Latency (ns) | Op Latency $P_{50}$ (ns) | Op Latency $P_{99}$ (ns) | Op Latency $P_{99.9}$ (ns) | Speedup / Reduction |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **1. Pure Allocation** | Dynamic Heap | 3,877,793 | 253.2 ns | 112.6 ns | 3,943.8 ns | 9,200.0 ns | baseline |
| | **MemoryPool** | **6,873,708** | **120.2 ns** | **76.0 ns** | **92.8 ns** | **1,074.0 ns** | **+77% throughput (42x lower $P_{99}$)** |
| **2A. Batch Allocation** | Dynamic Heap | 3,377,098 | 258.8 ns | 122.2 ns | 4,196.8 ns | 9,905.0 ns | baseline |
| | **MemoryPool** | **6,774,571** | **93.7 ns** | **84.2 ns** | **100.2 ns** | **249.0 ns** | **+101% throughput (42x lower $P_{99}$)** |
| **2B. Batch Deallocation** | Dynamic Heap | 5,994,013 | 119.0 ns | 83.2 ns | 91.2 ns | 156.0 ns | baseline |
| | **MemoryPool** | **6,939,411** | **92.9 ns** | **87.4 ns** | **99.8 ns** | **200.0 ns** | **+16% throughput** |
| **3. Repeated Reuse (10 Cycles)** | Dynamic Heap | 3,996,015 | 154.8 ns | 134.6 ns | 197.8 ns | 3,694.0 ns | baseline |
| | **MemoryPool** | **6,108,196** | **85.8 ns** | **80.2 ns** | **92.8 ns** | **136.0 ns** | **+53% throughput (27x lower $P_{99.9}$)** |
| **4. Sustained Window Churn** | Dynamic Heap | **7,652,087** | **82.3 ns** | **74.2 ns** | 159.8 ns | 196.0 ns | baseline |
| | **MemoryPool** | 6,877,876 | 111.1 ns | 99.0 ns | **120.8 ns** | **132.0 ns** | -10% throughput (1.5x lower $P_{99.9}$) |

- **What Improved:** MemoryPool demonstrated decisive advantages in allocation-intensive paths, boosting throughput by 1.77x to 2.01x and suppressing extreme tail latency ($P_{99}$ dropped from 3,944 ns to 93 ns in Exp 1; $P_{99.9}$ dropped from 3,694 ns to 136 ns in Exp 3).
- **What Worsened / Workload Dependence:** In Exp 4 (sustained 1,000-order sliding window churn), dynamic allocation achieved higher throughput (7.65M vs 6.88M ops/s). Because the working set was small, glibc's per-thread cache (`tcache`) serviced requests entirely in L1 cache without arena locks, whereas the MemoryPool's atomic 64-bit CAS Treiber stack incurred slight atomic bus cycle overhead. However, MemoryPool retained tighter tail determinism ($P_{99.9}$ was 132 ns for pool vs. 196 ns for dynamic).
- **Limitations:** Single-threaded allocation harness. In multi-threaded production environments with cross-core order destructions, glibc heap arena contention would significantly degrade dynamic heap performance.

---

### 6.3 Concurrency Synchronization Benchmarking (Phase 4)

- **Objective:** Evaluate the throughput, latency, and contention penalty of mutual exclusion primitives (`MutexQueue`) versus lock-free ring buffers (`SPSCQueue`).
- **Workload:** 1,000,000 messages per run $\times$ 5 independent runs across 1K and 64K capacities (20 measured runs).
- **CSV Dataset:** [`benchmark_results/concurrency_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/concurrency_benchmark.csv).

#### Concurrency Comparison Results (5 Repetitions)

| Queue Capacity | Queue Type | Throughput (ops/s) | Op Latency $P_{50}$ (ns) | Op Latency $P_{99}$ (ns) | E2E Dwell $P_{50}$ | E2E Dwell $P_{99}$ | Push Retries (Total) | Pop Empty Retries (Total) |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1K slots** | MutexQueue | 2,106,730 | 77.4 ns | 1,046.4 ns | 290.4 µs | 3.67 ms | 7,419,554 | 3,449,807 |
| | **SPSCQueue** | **7,298,283** | **68.6 ns** | **162.0 ns** | **94.8 µs** | **343.5 µs** | **4,112,943** | **1,635,048** |
| | *Delta (%)* | **+246.4%** | **-11.4%** | **-84.5%** | **-67.4%** | **-90.6%** | **-44.6%** | **-52.6%** |
| **64K slots** | MutexQueue | 3,705,156 | 48.4 ns | 833.2 ns | 15.18 ms | 90.24 ms | 8,958,279 | 132,683 |
| | **SPSCQueue** | **10,143,779** | 51.0 ns | **148.8 ns** | **5.69 ms** | **8.30 ms** | **2,482,967** | 165,803 |
| | *Delta (%)* | **+173.8%** | *+5.4%* | **-82.1%** | **-62.5%** | **-90.8%** | **-72.3%** | *+25.0%* |

- **What Improved:** The lock-free SPSC queue achieved a **3.46x speedup on 1K queues** (7.30M vs 2.11M ops/s) and a **2.74x speedup on 64K queues** (10.14M vs 3.71M ops/s). End-to-end dwell latency at $P_{99}$ collapsed by over 10x in both capacities (from 3.67 ms to 344 µs in 1K; from 90.2 ms to 8.3 ms in 64K).
- **What Worsened:** In `MutexQueue`, every push and pop operation acquires `std::mutex`, forcing hardware cache-line serialization and causing extreme lock contention stalls ($P_{99}$ operation latency exceeded 1,000 ns).
- **Limitations:** MutexQueue was evaluated in a non-blocking spin-retry configuration. Blocking waits with condition variables were omitted to preserve identical polling semantics.

---

### 6.4 CPU Affinity Benchmarking (Phase 5.1)

- **Objective:** Evaluate how thread core placement (`pthread_setaffinity_np`) affects throughput, contention retries, and run-to-run consistency compared to default Linux scheduling.
- **Workload:** 1,000,000 messages per run $\times$ 5 independent runs across 1K and 64K capacities (20 measured runs).
- **CSV Dataset:** [`benchmark_results/cpu_affinity_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/cpu_affinity_benchmark.csv).

#### CPU Affinity Results (5 Repetitions)

| Capacity | Configuration | Producer / Consumer CPU | Mean Throughput (ops/s) | Median Throughput (ops/s) | StdDev Throughput (ops/s) | Op Latency $P_{50}$ (ns) | Op Latency $P_{99}$ (ns) | Push Retries (Total) | Pop Empty Retries (Total) |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1K slots** | Default Linux | Unpinned (`-1, -1`) | 3,662,916 | 3,158,133 | +/- 3,465,577 | 49.0 ns | 139.8 ns | 859,572,202 | 1,448,362,982 |
| | **CPU Affinity** | Pinned (`0, 1`) | **5,348,178** | **5,306,553** | **+/- 1,220,102** | 93.4 ns | 271.2 ns | **3,132,885** | **1,131,541** |
| | *Delta (%)* | | **+46.0%** | **+68.0%** | **-64.8%** | *+90.6%* | *+94.0%* | **-99.6%** | **-21.9%** |
| **64K slots** | **Default Linux** | Unpinned (`-1, -1`) | **10,774,319** | **11,131,676** | +/- 799,926 | 56.8 ns | 155.6 ns | **1,717,604** | 426,793 |
| | CPU Affinity | Pinned (`0, 1`) | 5,620,100 | 6,439,483 | +/- 1,637,460 | 73.6 ns | 231.4 ns | 2,953,953 | **177,969** |
| | *Delta (%)* | | **-47.8%** | **-42.2%** | *+104.7%* | *+29.6%* | *+48.7%* | *+72.0%* | **-58.3%** |

- **Workload-Dependent Divergence:**
  - **1K Capacity (Decisive Success):** In tight backpressure-sensitive queues, CPU affinity eliminated devastating co-scheduling freezes. Under default Linux scheduling, Run #1 suffered a **193.56-second stall** (throughput of only 5,166 ops/s) with **848.5 million push retries** when CFS scheduled both spinning threads onto the same vCPU. Pinned execution eliminated this condition, increasing mean throughput by **+46.0%**, median throughput by **+68.0%**, and slashing push retries by **99.6%**.
  - **64K Capacity (Performance Penalty):** In large burst-absorbing buffers, CPU affinity **reduced throughput by -47.8%** (from 10.77M down to 5.62M ops/s). In this 2-vCPU virtual machine, rigid pinning prevented the guest kernel from migrating threads when the host OS preempted one virtual CPU, whereas floating threads allowed CFS to take advantage of whichever vCPU was active.
- **Limitations:** 2 vCPUs on a hybrid host CPU (P-cores vs E-cores). Thread pinning inside a VM binds to virtual CPUs, not isolated host physical cores.

---

### 6.5 Linux Thread Scheduling Policy Optimization (Phase 5.2)

- **Objective:** Evaluate the impact of POSIX real-time static priority scheduling (`SCHED_FIFO`, priority 10) against standard Linux dynamic time-sharing (`SCHED_OTHER` / CFS) on throughput, preemption jitter, and variance.
- **Workload:** 1,000,000 messages per run $\times$ 5 independent runs across 1K and 64K capacities (20 measured runs). Both threads pinned (Producer on CPU 0, Consumer on CPU 1).
- **CSV Dataset:** [`benchmark_results/scheduling_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/scheduling_benchmark.csv).

#### Scheduling Policy Results (5 Repetitions)

| Capacity | Policy | Priority | Mean Throughput (ops/s) | Median Throughput (ops/s) | StdDev Throughput (ops/s) | Op Latency $P_{50}$ (ns) | Op Latency $P_{99}$ (ns) | E2E Dwell $P_{50}$ | Push Retries (Total) | Pop Empty Retries (Total) |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **64K slots** | `SCHED_OTHER` | 0 | 7,141,395 | 7,324,097 | +/- 1,668,171 | 55.6 ns | 146.6 ns | 6.04 ms | 4,454,003 | 161,592 |
| | **`SCHED_FIFO`** | 10 | **10,981,501** | **10,958,271** | **+/- 792,965** | 70.2 ns | 164.0 ns | 6.03 ms | **2,630,194** | **7,262** |
| | *Delta (%)* | | **+53.8%** | **+49.6%** | **-52.5%** | | | | **-40.9%** | **-95.5%** |
| **1K slots** | `SCHED_OTHER` | 0 | 7,529,222 | 7,994,561 | +/- 1,259,052 | 82.8 ns | 158.4 ns | 101.5 µs | 5,693,596 | 365,814 |
| | **`SCHED_FIFO`** | 10 | **11,209,401** | **11,383,794** | **+/- 1,114,418** | **65.8 ns** | **152.6 ns** | **87.4 µs** | **2,894,289** | **72,497** |
| | *Delta (%)* | | **+48.9%** | **+42.4%** | **-11.5%** | **-20.5%** | **-3.7%** | **-13.9%** | **-49.2%** | **-80.2%** |

- **What Improved (Measured Observations):** `SCHED_FIFO` demonstrated substantial measured performance improvements across both buffer capacities in this test environment:
  - Throughput increased by **+53.8%** on 64K buffers and **+48.9%** on 1K buffers.
  - Throughput variance on 64K buffers dropped by **52.5%** (standard deviation collapsed from 1.67M to 793K ops/s).
  - Empty pop starvation loops collapsed by **-95.5%** on 64K queues (from 161.6K down to 7.3K) and by **-80.2%** on 1K queues (from 365.8K down to 72.5K).
  - The lowest single run under `SCHED_FIFO` across all 10 tests was 9.70M ops/s (compared to 4.65M ops/s under `SCHED_OTHER`).
- **What Worsened:** None observed within the benchmarked workload. Operating under real-time priority yielded strictly superior runtime numbers in this environment.
- **Limitations & Attribution Nuance:** Requires elevated permissions (`cap_sys_nice` file capability). While assigning real-time priority grants worker threads non-preemptive execution over normal tasks in the guest scheduler, guest scheduler preemption cannot be conclusively proven to be the sole primary cause of polling stalls. In a virtual machine, host-level thread multiplexing and vCPU preemption by the host operating system remain uninstrumented from within the guest.

---

### 6.6 Linux Memory Locking & Page Fault Analysis (Phase 5.3)

- **Objective:** Evaluate the impact of Linux virtual memory locking (`mlockall`) on page fault dynamics, throughput, and latency consistency under standard `SCHED_OTHER` scheduling.
- **Workload:** 1,000,000 messages per run $\times$ 5 independent runs across 1K and 64K capacities (20 measured runs). Both threads pinned (CPU 0, CPU 1).
- **CSV Dataset:** [`benchmark_results/memory_locking_benchmark.csv`](file:///home/kali/Real-Time-Market-Data-Feed-Handler-and-Order-Matching-Engine/benchmark_results/memory_locking_benchmark.csv).

#### Memory Locking Results (5 Repetitions)

| Capacity | Configuration | Memlock | Mean Throughput (ops/s) | Median Throughput (ops/s) | StdDev Throughput (ops/s) | Op Latency $P_{50}$ (ns) | Op Latency $P_{99}$ (ns) | E2E Dwell $P_{50}$ | Push Retries (Total) | Pop Empty Retries (Total) | $\Delta \text{MinFlt}$ | $\Delta \text{MajFlt}$ |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **64K slots** | Normal | No | 7,301,222 | 7,165,565 | +/- 1,774,480 | 60.2 ns | 155.2 ns | 6.61 ms | 4,297,649 | 419,072 | 216 | 0 |
| | **Locked (`mlockall`)** | **Yes** | **7,828,590** | **8,120,419** | +/- 1,996,839 | 76.4 ns | **134.8 ns** | 7.25 ms | **3,552,949** | **139,718** | **185** | 0 |
| | *Delta (%)* | | **+7.2%** | **+13.3%** | | *+26.9%* | **-12.9%** | | **-17.3%** | **-66.7%** | **-14.4%** | |
| **1K slots** | **Normal** | No | **8,721,639** | **8,960,065** | +/- 1,134,925 | 90.8 ns | **157.2 ns** | 103.6 µs | 3,704,931 | **651,032** | 0 | 0 |
| | Locked (`mlockall`) | **Yes** | 8,431,807 | 8,115,337 | +/- 2,148,189 | **69.6 ns** | 170.0 ns | **95.4 µs** | **3,623,212** | 1,093,538 | 0 | 0 |
| | *Delta (%)* | | *-3.3%* | *-9.4%* | | **-23.3%** | *+8.1%* | **-7.9%** | **-2.2%** | *+68.0%* | | |

- **Workload-Dependent Performance (Not Universally Beneficial):**
  - **64K Capacity:** Mean throughput improved by **+7.2%** (7.30M to 7.83M ops/s), median throughput increased by **+13.3%** (7.17M to 8.12M ops/s), and tail latency ($P_{99}$) compressed by **-12.9%** (155 ns to 135 ns). Push retries dropped by -17.3% and empty pop starvation retries dropped by -66.7%. However, median ($P_{50}$) operation latency **increased** from 60.2 ns to 76.4 ns (+26.9%).
  - **1K Capacity:** Mean throughput **decreased by -3.3%** (8.72M to 8.43M ops/s) and median throughput **decreased by -9.4%** (8.96M to 8.12M ops/s). Median ($P_{50}$) operation latency improved (90.8 ns to 69.6 ns), but tail latency ($P_{99}$) **worsened** (157 ns to 170 ns), and empty pop retries rose by +68.0%.
- **Page-Fault Dynamics & Methodological Qualification:**
  - In Run #1, locked mode observed 185 minor page faults compared to 216 in normal mode. However, this difference does **not** independently establish that `mlockall` caused systematic pre-faulting; minor faults represent page table allocations that vary with allocator heuristics and dynamic linking.
  - In Runs #2 through #5, **$\Delta$ minor page faults was exactly 0** in both configurations due to buffer reuse.
  - Major page faults were 0 across all runs because system RAM (~867 MB) exceeded the ~20 MB working set.
- **Architectural Scope & Limitations:** `mlockall` guarantees physical page residency and prevents kernel swap-outs; it does **not** eliminate hardware MMU page table walks or CPU TLB misses. Inside VirtualBox, guest memory locking operates strictly in the guest address space and cannot compel the host hypervisor to wire-lock physical host RAM frames.

---

## 7. Cross-Experiment Findings

Comparing results across all experimental phases reveals core systems trade-offs:

### 7.1 Why Lock-Free Queues Outperformed Mutex Queues
In high-throughput inter-thread communication, synchronization costs are dominated by cache coherency and lock serialization. In `MutexQueue`, every push and pop acquires `std::mutex`. Even with an uncontended fast path, atomic compare-and-swap (CAS) instructions force the CPU store buffer to serialize. Under contention, both threads simultaneously access and modify the same mutex cache line, triggering frequent cache-line invalidation traffic across cores. In contrast, Dmitry Vyukov's `SPSCQueue` separates `write_pos_` and `read_pos_` with `alignas(64)` padding. This padding reduces false sharing by keeping independent variables on separate cache lines, while producer and consumer coordinate with lightweight acquire/release atomic loads. True sharing of updated indices still causes necessary cache-coherence invalidations, but eliminating false sharing and removing mutual exclusion lock serialization yielded a **2.74x to 3.46x throughput advantage** and compressed $P_{99}$ dwell latency by **over 10x**.

### 7.2 Why Memory Pools Were Not Universally Faster
While `OrderMemoryPool` delivered massive 1.77x to 2.01x speedups in pure allocation and batch recycling experiments (slashing $P_{99}$ tail latency by 42x), dynamic allocation was 10% faster in Experiment 4 (continuous 1,000-order sliding window churn). This occurred because glibc's memory allocator maintains thread caches (`tcache`). When working sets are small, allocations and deallocations recycle the same hot chunks directly out of L1 cache with minimal pointer manipulations, whereas the memory pool executes atomic 64-bit CAS loops on its Treiber stack. Memory pools provide deterministic upper-tail bounds ($P_{99.9}$ was 132 ns for pool vs 196 ns for dynamic), but for small, single-threaded working sets, modern heap allocators achieve competitive median throughput.

### 7.3 Why CPU Affinity Helped 1K Queues but Impaired 64K Queues
CPU affinity exhibited an asymmetric trade-off governed by buffer capacity and hypervisor scheduling:
- On a **1K queue**, buffer capacity is tight (1,024 slots). If both threads share a single vCPU under default Linux scheduling, the producer fills the buffer and spins in user space (`cpuPause()`). The spinning producer starves the consumer of CPU time slices, causing a catastrophic pipeline freeze (193.56 seconds and 848 million retries in Run #1). Pinning each thread to a dedicated core eliminated co-scheduling stalls, cutting retries by **99.6%** and raising throughput by **+46%**.
- On a **64K queue**, the buffer is large enough to absorb bursts without spinning. Under default scheduling, Linux CFS floats threads across vCPUs to utilize available host CPU cycles. When rigid affinity was enforced inside the VM, pinned guest threads could not migrate when the host OS temporarily descheduled a vCPU, resulting in a **47.8% throughput loss**.

### 7.4 Measured Observations from the Genuine `SCHED_FIFO` Experiment
Unlike CPU affinity, POSIX real-time static priority scheduling (`SCHED_FIFO`) consistently demonstrated improved performance across both queue sizes in this environment, increasing mean throughput by **+48.9% (1K)** and **+53.8% (64K)** while halving throughput variance. In these measurements, giving trading threads real-time priority over normal tasks strongly correlated with the elimination of large run-to-run throughput collapses and an 80% to 95% drop in empty-pop starvation retries. While these observations align with the hypothesis that real-time scheduling protects user-space polling loops from guest-level preemption, guest-level instrumentation alone cannot conclusively isolate guest scheduler effects from underlying host-level hypervisor scheduling dynamics.

### 7.5 Why Memory Locking Showed Mixed Workload Effects
Virtual memory locking (`mlockall`) prevents eligible locked pages from being swapped to disk. On large 64K queues, it provided modest throughput gains (+7.2%) and compressed $P_{99}$ tail latency (-12.9%). However, on 1K queues, mean throughput decreased (-3.3%), $P_{99}$ latency worsened (+8.1%), and median latency increased in 64K (+26.9%). Because the virtual machine had ample free memory (~867 MB) and swap was not active, major page faults were zero in both modes. Pre-locking pages altered thread memory placement and cache line alignment under CFS, yielding minor latency trade-offs without the major page-fault elimination benefits observed on memory-pressured systems.

### 7.6 Throughput vs. Latency Percentiles: The Decoupling Principle
A fundamental insight across all phases is that **throughput and latency percentiles are decoupled**:
- In Phase 5.1 (1K queue), CPU affinity boosted throughput by +46%, yet median operation latency increased from 49 ns to 93 ns.
- In Phase 5.3 (64K queue), memory locking raised throughput by +7.2%, yet median operation latency rose from 60 ns to 76 ns.
Throughput measures aggregate pipeline efficiency (messages processed per second), which improves when catastrophic pauses are eliminated. Median latency measures individual function execution time, which reflects cache warmth and instruction cycles. Systems optimizations that eliminate tail stalls often introduce slight per-operation overhead, proving that throughput and latency distributions must always be evaluated independently.

---

## 8. Limitations and Future Work

### 8.1 Methodological and Environmental Limitations
1. **VirtualBox Virtualization Layer:** All benchmarks executed inside an Oracle VirtualBox guest. Guest vCPUs are multiplexed onto host CPU threads. Host OS preemption, host background tasks, and shadow page table translations introduce latency jitter that cannot occur on bare-metal hardware.
2. **Small Number of Repetitions:** Queue and memory experiments were executed across 3 to 5 independent runs due to benchmark turnaround time constraints. While consistent trends were established, 5 runs provides limited statistical power for narrow confidence intervals.
3. **Tail Percentile Sample-Size Bounds:** In order book benchmarks ($N = 10,000$), upper quantiles ($P_{99.9}$) contain only 10 observations, which does not constitute a statistically robust sample of extreme tail behavior.
4. **Lack of Bare-Metal Validation:** Dedicated low-latency trading engines run on bare-metal servers with kernel boot parameters (`isolcpus`, `nohz_full`, `rcu_nocbs`) that completely isolate cores from the OS scheduler.
5. **Lack of MPMC Benchmark Coverage:** The queue framework evaluated single-producer single-consumer pipelines. Multi-producer multi-consumer (MPMC) order submission pipelines were not benchmarked.
6. **Synthetic vs. Real-World Market Data:** Benchmarks utilized synthetic deterministic order sequences. Real financial markets exhibit bursty clustered arrivals (Poisson or power-law distributions) and correlated cancel-replace behavior.

### 8.2 Future Bare-Metal Production Suggestions (Unvalidated in this Study)
The following architectural techniques are commonly referenced in high-frequency trading literature, but were **not experimentally tested or validated in this study** and represent unvalidated proposals for future bare-metal research:
1. **Bare-Metal Linux Core Isolation:** Booting the kernel with `isolcpus=2,3` to completely remove designated trading cores from the general Linux scheduler domain.
2. **Full Tickless Kernel Execution:** Configuring `nohz_full=2,3` and `rcu_nocbs=2,3` to eliminate timer interrupt ticks on isolated cores when running single threads.
3. **Linux HugePages (`hugetlbfs`):** Allocating memory using 2 MB or 1 GB HugePages to measure direct TLB miss suppression on large memory buffers.
4. **NUMA Memory Node Binding:** Utilizing `numactl --membind` and `libnuma` to ensure memory buffers and threads reside on the same NUMA socket, eliminating UPI/QPI cross-socket bus delays.
5. **Real-World PCAP Replay:** Benchmarking order book processing using historical NASDAQ TotalView-ITCH packet captures.

---

## 9. Conclusion

The KernelTick project demonstrates the decisive role that operating system mechanisms play in low-latency software design. The empirical findings across all five optimization phases demonstrate that:
1. **Software Architecture Dominates First:** Eliminating lock contention via cache-aligned lock-free ring buffers (`SPSCQueue`) and preallocating memory via atomic object pools (`OrderMemoryPool`) yielded the largest performance gains (2.7x to 3.5x throughput improvements and 10x to 40x tail latency compressions).
2. **OS Scheduling Governs Stability:** CPU core affinity and real-time scheduling (`SCHED_FIFO`) eliminate scheduler-induced starvation and preemption freezes, cutting push retries by up to 99.6% and slashing run-to-run variance by half.
3. **No Optimization is Universally Beneficial:** Core affinity reduced throughput in burst-absorbing queues (-48%); memory pools were slightly slower than tcache in single-threaded sliding windows (-10%); and memory locking exhibited mixed latency trade-offs in the absence of system swap pressure.
4. **Engineering for Low Latency Requires Full-Stack Co-Design:** Achieving deterministic low latency requires coordinating application data structures, cache-line layout, memory allocation lifetimes, thread affinity, and kernel scheduling policies into an integrated architecture.

---

## 10. Reproducibility Guide

All benchmarks and test suites are built using CMake and standard Linux toolchains.

### 10.1 Build Targets
From the repository root directory:
```bash
# 1. Configure the build with Release optimizations (-O3, native architecture, LTO)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 2. Compile all targets
cmake --build build -j2
```

Executable targets produced in `build/bin/`:
- `OrderBookTests`: Unit test suite (144 assertions).
- `OrderBookBenchmark`: Phase 2 order book benchmark.
- `QueueBenchmark`: Phase 2, 5.1, 5.2, and 5.3 SPSC queue benchmarking harness.
- `MemoryBenchmark`: Phase 3 memory allocation benchmark.
- `ConcurrencyBenchmark`: Phase 4 mutex vs. lock-free queue benchmark.

### 10.2 Correctness Test Execution
Run the unit test suite:
```bash
./build/bin/OrderBookTests
```
Expected output: 144 passing assertions, 0 failures.

### 10.3 Unprivileged Benchmark Execution

#### Phase 2: Order Book Baseline
```bash
./build/bin/OrderBookBenchmark --size 10000 --runs 3 --csv benchmark_results/order_book_benchmark.csv --overwrite
```

#### Phase 3: Memory Allocation Strategies
```bash
./build/bin/MemoryBenchmark --size 100000 --runs 5 --csv benchmark_results/memory_benchmark.csv --overwrite
```

#### Phase 4: Concurrency Synchronization (Mutex vs. SPSC)
```bash
./build/bin/ConcurrencyBenchmark --size 1000000 --runs 5 --csv benchmark_results/concurrency_benchmark.csv --overwrite
```

#### Phase 5.1: CPU Affinity
```bash
# Experiment A: Default Linux Scheduling (Unpinned)
./build/bin/QueueBenchmark --size 1000000 --runs 5 --csv benchmark_results/cpu_affinity_benchmark.csv --overwrite

# Experiment B: CPU Affinity (Pinned Producer CPU 0, Consumer CPU 1)
./build/bin/QueueBenchmark --size 1000000 --runs 5 --affinity --csv benchmark_results/cpu_affinity_benchmark.csv
```

### 10.4 Privileged Benchmark Execution (Explicit Capabilities)

> [!WARNING]
> **Permission Model:** Real-time scheduling (`SCHED_FIFO`) and memory locking (`mlockall`) require specific Linux capabilities (`CAP_SYS_NICE` and `CAP_IPC_LOCK`). Do **not** run benchmarks as `root`. Apply targeted file capabilities to the executable and remove them immediately post-run.

#### Phase 5.2: Scheduling Policy (`SCHED_FIFO`)
```bash
# 1. Grant CAP_SYS_NICE capability
sudo setcap cap_sys_nice+ep ./build/bin/QueueBenchmark

# 2. Run SCHED_OTHER baseline (unpinned or pinned)
./build/bin/QueueBenchmark --size 1000000 --runs 5 --affinity --csv benchmark_results/scheduling_benchmark.csv --overwrite

# 3. Run genuine SCHED_FIFO experiment (priority 10)
./build/bin/QueueBenchmark --size 1000000 --runs 5 --affinity --realtime --priority 10 --csv benchmark_results/scheduling_benchmark.csv

# 4. Clean up capability immediately
sudo setcap -r ./build/bin/QueueBenchmark
```

#### Phase 5.3: Memory Locking (`mlockall`)
```bash
# 1. Grant CAP_IPC_LOCK capability
sudo setcap cap_ipc_lock+ep ./build/bin/QueueBenchmark

# 2. Run Normal mode baseline (unlocked demand paging)
./build/bin/QueueBenchmark --size 1000000 --runs 5 --affinity --csv benchmark_results/memory_locking_benchmark.csv --overwrite

# 3. Run Locked mode experiment (mlockall MCL_CURRENT | MCL_FUTURE)
./build/bin/QueueBenchmark --size 1000000 --runs 5 --affinity --memlock --csv benchmark_results/memory_locking_benchmark.csv

# 4. Clean up capability immediately
sudo setcap -r ./build/bin/QueueBenchmark
```
