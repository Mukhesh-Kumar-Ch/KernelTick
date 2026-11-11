# KernelTick Phase 5.3: Linux Memory Locking & Page-Fault Analysis (`mlockall`)

An empirical and architectural study of virtual memory locking, demand paging, and page fault dynamics on **KernelTick**, an experimental Linux-based low-latency trading engine designed to examine operating system performance, thread scheduling, and hardware-software interaction in electronic trading systems.

---

## 1. Overview & Objective

In high-frequency trading (HFT) and ultra-low-latency financial systems, deterministic memory access is critical. Even if application algorithms execute in single-digit nanoseconds, an unexpected page fault triggered by the operating system kernel can stall thread execution for microseconds (minor page fault) or milliseconds (major disk/swap page fault), causing queue backpressure, lost orders, or stale market quotes.

In **Phase 5.1 (CPU Affinity)** and **Phase 5.2 (Scheduling Policy Optimization)**, we demonstrated how pinning threads to dedicated CPUs and granting `SCHED_FIFO` real-time scheduling eliminated CPU migration penalties and scheduler preemption jitter. However, neither CPU affinity nor real-time scheduling prevents the Linux kernel's virtual memory subsystem from paging, swapping, or delaying physical frame allocations.

The objective of **KernelTick Phase 5.3** is to evaluate the impact of Linux virtual memory locking (`mlockall`):
1. **Experiment A (Normal Mode — Unlocked):** Standard Linux demand paging without memory locking. Memory pages are mapped and faulted into physical RAM on first write, and the operating system page cache / swapper retains authority to page out anonymous memory.
2. **Experiment B (Locked Mode — `mlockall(MCL_CURRENT | MCL_FUTURE)`):** All present and future process virtual memory pages are locked into physical RAM frames. The kernel is prohibited from paging out or swapping process memory.

### Experimental Controls
To isolate memory locking as the single independent variable:
- **Scheduling Policy:** Strictly `SCHED_OTHER` (Linux Completely Fair Scheduler, priority 0) for both Experiment A and Experiment B. Real-time scheduling (`SCHED_FIFO`) was intentionally disabled to avoid confounding memory locking effects with real-time preemption suppression.
- **CPU Affinity:** Producer pinned to CPU 0, Consumer pinned to CPU 1 across all runs.
- **Queue Capacities:** Standard HFT buffer (64K slots / 65,536 elements) and backpressure-sensitive buffer (1K slots / 1,024 elements).
- **Workload Size:** 1,000,000 messages per run.
- **Repetitions:** 5 independent iterations per capacity (10 runs per experiment, 20 total runs).
- **Platform:** VirtualBox x86_64 Linux Guest (Debian/Kali, Kernel 6.19.14, 2 vCPUs, GCC 15.2.0 `-O3 -march=native C++20`).

---

## 2. Theoretical Foundation of Virtual Memory & Paging Mechanics

### 2.1 Virtual Memory and Demand Paging
Modern operating systems decouple process virtual addresses from physical RAM using page tables managed by the Memory Management Unit (MMU). When a user-space process allocates memory via `brk()` or `mmap()`, the kernel merely updates its internal virtual memory areas (VMAs). Physical RAM frames are not assigned immediately; instead, Linux employs **demand paging**:
- When the CPU attempts to read or write a virtual address that lacks a valid Translation Lookaside Buffer (TLB) entry and page table mapping, the MMU generates a hardware page fault interrupt (`#PF`, Exception 14).
- The CPU switches to kernel mode, executing the kernel page fault handler (`do_page_fault()`).

### 2.2 Minor vs. Major Page Faults

Linux classifies page faults into two fundamental categories (tracked via `struct rusage`):

| Fault Classification | `rusage` Metric | Kernel Mechanism | Latency Impact | Trading Engine Consequence |
| :--- | :--- | :--- | :--- | :--- |
| **Minor Page Fault** | `ru_minflt` | Page is already present in physical RAM. Triggered by first write to newly allocated heap memory (zero-fill on demand), Copy-On-Write (COW) resolution, or re-establishing page table entries from the kernel page cache without disk access. | ~500 ns to 5 µs per fault | Introduces micro-stalls during order book updates, bursts of message pushes, or dynamic queue resizing. |
| **Major Page Fault** | `ru_majflt` | Page is **not** present in physical RAM. The kernel must suspend thread execution, issue synchronous block I/O to disk or swap storage, wait for device interrupts, and swap the page back into RAM. | ~2 ms to 20 ms per fault | Catastrophic latency spike. A 5 ms disk swap fault will cause a lock-free queue to overflow, drop tens of thousands of market feed packets, and trigger severe execution slippage. |

### 2.3 Memory Locking via `mlockall(MCL_CURRENT | MCL_FUTURE)`
POSIX.1b defines the `mlock()` and `mlockall()` system calls to manage physical memory residency:
- **`MCL_CURRENT`:** Instructs the kernel to traverse existing VMAs of the calling process (code segment, data segment, BSS, active heap, and thread stacks) and populate page tables, locking physical RAM frames and marking them unswappable (`VM_LOCKED`).
- **`MCL_FUTURE`:** Instructs the kernel that memory mapped or allocated in the future (via `mmap`, `brk`, or thread stack expansion) must automatically have physical pages allocated, faulted, and locked into RAM at allocation time.
- **Wire-Locking Scope & Limitations:** The kernel's page reclaim mechanism (`kswapd`, page aging) strictly skips locked pages. They cannot be paged out or swapped to disk under memory pressure. However, `mlockall` operates strictly on page residency: **it prevents eligible locked pages from being swapped out, but does not eliminate hardware MMU page table walks or CPU TLB misses** when virtual address translations are evicted from CPU TLB caches.

---

## 3. Environment Inspection & OS Limits (`RLIMIT_MEMLOCK` & `CAP_IPC_LOCK`)

Before running locked benchmarks, an audit of system limits was performed:

```text
Resource Limit Audit:
  RLIMIT_MEMLOCK soft: 8,388,608 bytes (8.00 MB)
  RLIMIT_MEMLOCK hard: 8,388,608 bytes (8.00 MB)
  MemTotal:           2,058,400 kB (~2.01 GB)
  MemAvailable:         888,208 kB (~867 MB)
  SwapTotal:            999,420 kB (~976 MB)
```

### The 8 MB Limit Barrier
A full 1,000,000 message benchmark run allocates:
- 64K SPSC Queue buffer: ~2.1 MB
- `op_latencies_ns` vector (1M `int64_t`): 8.0 MB
- `e2e_latencies_ns` vector (1M `int64_t`): 8.0 MB
- Executable, shared libraries, and thread stacks: ~2.0 MB
- **Total Process Working Set:** ~20.1 MB

Because the process working set exceeds the default 8 MB `RLIMIT_MEMLOCK`, an unprivileged call to `mlockall()` fails immediately with `ENOMEM` (errno 12: *Cannot allocate memory*).

### Safe Permission Elevation via File Capabilities
Rather than running the benchmark as root (which violates security best practices and skews runtime environment variables), the benchmark was provisioned with the targeted POSIX capability:
- **Capability:** `CAP_IPC_LOCK` permits a process to lock arbitrary amounts of memory into RAM, bypassing the `RLIMIT_MEMLOCK` ceiling.
- **Application:**
  ```bash
  sudo setcap cap_ipc_lock+ep ./build/bin/QueueBenchmark
  ```
- **Verification:**
  ```bash
  getcap ./build/bin/QueueBenchmark
  # Output: ./build/bin/QueueBenchmark cap_ipc_lock=ep
  ```
- **Cleanup:** Upon benchmark completion, the capability was immediately stripped:
  ```bash
  sudo setcap -r ./build/bin/QueueBenchmark
  ```

### Kernel Verification of Memory Locking
When `--memlock` was passed, the benchmark verified that `mlockall` succeeded and queried the kernel's virtual memory status in `/proc/self/status`:
```text
[Memory Locking Configuration]
  RLIMIT_MEMLOCK: soft=8388608 bytes, hard=8388608 bytes
  System Available Memory: 867 MB
  Attempting mlockall(MCL_CURRENT | MCL_FUTURE)...
  mlockall(MCL_CURRENT | MCL_FUTURE) activated successfully.
  Kernel Verified Locked Memory (VmLck): 3948 kB
```
The kernel's `VmLck` field confirmed that process pages were actively wire-locked in RAM.

---

## 4. Measurement Methodology & Probe Integrity

To ensure empirical validity without introducing measurement distortion:
1. **Out-of-Path Page Fault Sampling:**
   Resource usage is queried via `getrusage(RUSAGE_SELF, &ru)` **strictly outside the timed pipeline**.
   - `ru_before` is recorded immediately prior to thread barrier synchronization.
   - `ru_after` is recorded immediately following `producer.join()` and `consumer.join()`.
   - Recording page faults inside the hot queue `push`/`pop` path would require issuing system calls, invalidating nanosecond-level latency measurements.
2. **Cache and Memory Warm-up:**
   Prior to each timed run, a 10,000-message `warmupQueue()` execution is performed under identical CPU pinning to prime CPU L1/L2 caches, branch predictors, and page table hierarchies.
3. **Synchronization & Skew Control:**
   C++20 `std::barrier` synchronizes worker thread start. The start skew between producer and consumer barrier exit timestamps is recorded; in all runs, start skew remained well below 5% of pipeline duration (OK status).

---

## 5. Empirical Benchmark Results (Controlled Comparison)

The benchmark was executed across 5 independent runs for each queue capacity with 1,000,000 messages per run. Results were exported to `benchmark_results/memory_locking_benchmark.csv`.

### Comprehensive Aggregate Results (5 Repetitions)

| Configuration | Queue Capacity | Memlock Status | Mean Throughput (ops/s) | Median Throughput (ops/s) | StdDev Throughput (ops/s) | Op Latency $P_{50}$ | Op Latency $P_{99}$ | E2E Dwell $P_{50}$ | Push Retries (5M msgs) | Pop Empty Retries (5M msgs) | Total $\Delta$ Minor Faults | Total $\Delta$ Major Faults |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Normal (Unlocked)** | 64K | Disabled | 7,301,222 | 7,165,565 | +/- 1,774,480 | 60 ns | 155 ns | 6.61 ms | 4,297,649 | 419,072 | 216 | 0 |
| **Locked (`mlockall`)** | 64K | **Enabled** | **7,828,590** | **8,120,419** | +/- 1,996,839 | 76 ns | **135 ns** | 7.25 ms | **3,552,949** | **139,718** | **185** | 0 |
| *Delta (%)* | | | **+7.2%** | **+13.3%** | | *+26.7%* | **-12.9%** | | **-17.3%** | **-66.7%** | **-14.4%** | |
| **Normal (Unlocked)** | 1K | Disabled | 8,721,639 | 8,960,065 | +/- 1,134,925 | 91 ns | 157 ns | 103.6 µs | 3,704,931 | 651,032 | 0 | 0 |
| **Locked (`mlockall`)** | 1K | **Enabled** | 8,431,807 | 8,115,337 | +/- 2,148,189 | **70 ns** | 170 ns | **95.4 µs** | **3,623,212** | 1,093,538 | 0 | 0 |
| *Delta (%)* | | | *-3.3%* | *-9.4%* | | **-23.1%** | *+8.3%* | **-7.9%** | **-2.2%** | *+68.0%* | | |

---

### Individual Run Detail (1,000,000 Messages per Run)

#### Capacity 64K: Normal vs. Locked

| Run Index | Normal Throughput | Normal $\Delta$ MinFlt | Normal Retries (Push/Pop) | Locked Throughput | Locked $\Delta$ MinFlt | Locked Retries (Push/Pop) | Throughput Delta |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Run #1** | 4,508,684 ops/s | 216 | 606,222 / 133,176 | 4,583,503 ops/s | 185 | 1,108,496 / 105,391 | +1.7% |
| **Run #2** | 9,007,519 ops/s | 0 | 585,387 / 64,447 | 8,120,419 ops/s | 0 | 570,866 / 0 | -9.8% |
| **Run #3** | 7,165,565 ops/s | 0 | 1,287,027 / 0 | 7,567,795 ops/s | 0 | 663,400 / 12,280 | +5.6% |
| **Run #4** | 7,163,679 ops/s | 0 | 1,308,867 / 336 | 9,211,302 ops/s | 0 | 578,389 / 644 | **+28.6%** |
| **Run #5** | 8,660,662 ops/s | 0 | 510,146 / 221,113 | 9,659,933 ops/s | 0 | 631,798 / 21,403 | **+11.5%** |

#### Capacity 1K: Normal vs. Locked

| Run Index | Normal Throughput | Normal $\Delta$ MinFlt | Normal Retries (Push/Pop) | Locked Throughput | Locked $\Delta$ MinFlt | Locked Retries (Push/Pop) | Throughput Delta |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Run #1** | 6,818,115 ops/s | 0 | 769,760 / 387,751 | 6,068,001 ops/s | 0 | 853,410 / 414,123 | -11.0% |
| **Run #2** | 8,960,065 ops/s | 0 | 747,612 / 45,608 | 8,115,337 ops/s | 0 | 817,446 / 171,409 | -9.4% |
| **Run #3** | 9,124,651 ops/s | 0 | 726,834 / 36,299 | 9,857,433 ops/s | 0 | 528,650 / 171,051 | +8.0% |
| **Run #4** | 8,846,161 ops/s | 0 | 759,230 / 126,095 | **11,289,787 ops/s** | 0 | 474,401 / 52,238 | **+27.6%** |
| **Run #5** | 9,859,203 ops/s | 0 | 701,495 / 55,279 | 6,828,475 ops/s | 0 | 949,305 / 284,717 | -30.7% |

---

## 6. Empirical Findings & Analysis

### 6.1 Page-Fault Dynamics & Qualification
1. **Initial Minor Fault Observation & Methodological Qualification:**
   - In the first run of the 64K benchmark (the very first execution in the process lifetime), memory locking recorded 185 minor page faults compared to 216 in the normal configuration.
   - **Important Qualification:** This observed difference (-31 faults) does **not** independently establish that `mlockall` caused systematic pre-faulting of application data buffers. Minor page faults represent page-table mapping allocations and zero-fill operations that can vary due to memory allocator internals (glibc `malloc`/`mmap` heuristics), dynamic linker resolutions, thread initialization, and heap state prior to timed execution.
2. **Zero In-Pipeline Page Faults Across Subsequent Runs:**
   - In Runs 2 through 5 of both 64K and 1K tests, **$\Delta$ minor page faults was exactly 0** in both normal and locked configurations.
   - Because the benchmark reuses heap buffers across runs, pages touched in Run 1 remained resident in the process page table. In the absence of system-wide memory exhaustion, the Linux kernel did not reclaim or unmap these pages.
3. **Zero Major Faults Throughout:**
   - Major page faults (`ru_majflt`) were 0 across all 20 runs. The host VM possessed ~867 MB of available RAM, comfortably exceeding the benchmark's ~20 MB working set. No disk swapping occurred in either configuration.

### 6.2 Workload-Dependent Performance & Empirical Trade-Offs

The empirical data demonstrates that memory locking is **not universally beneficial**. Its effects are workload-dependent, exhibiting distinct trade-offs across queue capacities and latency metrics:

1. **64K Capacity Workload (Large Buffer / Standard HFT):**
   - **Throughput Elevation:** Mean throughput improved by **+7.2%** (from 7,301,222 to 7,828,590 ops/s), and median throughput increased by **+13.3%** (from 7,165,565 to 8,120,419 ops/s).
   - **P50 Latency Increase:** Despite throughput gains, median ($P_{50}$) operation latency **increased** from 60 ns to 76 ns (+26.7%).
   - **P99 Tail Latency Compression:** Operation latency at the 99th percentile ($P_{99}$) decreased from 155 ns to 135 ns (-12.9%).
   - **Contention Metrics:** Push retries dropped from 4.30M to 3.55M (-17.3%), and empty pop retries fell from 419K to 140K (-66.7%).
   - **Mechanism:** Memory locking guarantees page residency, preventing kernel paging mechanisms from swapping memory frames to disk. However, it does not eliminate MMU page table walks or CPU TLB misses, and the shift in thread scheduling dynamics under CFS yielded higher median per-operation latency alongside higher overall pipeline throughput.

2. **1K Capacity Workload (Small Buffer / Backpressure-Sensitive):**
   - **Throughput Degradation:** Both mean and median throughput **decreased** under locked mode: mean throughput dropped by **-3.3%** (from 8,721,639 to 8,431,807 ops/s) and median throughput dropped by **-9.4%** (from 8,960,065 to 8,115,337 ops/s).
   - **Divergent Latency Trade-Off ($P_{50}$ vs. $P_{99}$):** Median operation latency ($P_{50}$) **improved** from 91 ns to 70 ns (-23.1%), but tail latency at the 99th percentile ($P_{99}$) **worsened** from 157 ns to 170 ns (+8.3%).
   - **Starvation & Backpressure:** Empty pop retries increased from 651,032 to 1,093,538 (+68.0%), reflecting intermittent consumer starvation when producer-consumer synchronization interacted with Linux CFS scheduling on a constrained buffer.
   - **Peak Throughput Anomaly:** Although average throughput fell, Run #4 achieved the highest single-run throughput of the entire study (11,289,787 ops/s), illustrating substantial run-to-run variance (std dev +/- 2.15M ops/s) under `SCHED_OTHER`.

3. **Absence of Universal Benefit:**
   - Memory locking cannot be characterized as an unconditional performance optimization. While it successfully prevents eligible locked pages from being swapped out, its impact on throughput and latency distributions varies significantly depending on queue sizing, memory access patterns, and operating system scheduling interactions.

---

## 7. Virtualization Limitations vs. Bare-Metal Production Deployments

A critical requirement of this study is accurately characterizing the boundary between the guest operating system and the hypervisor in a **VirtualBox virtualized environment**:

```
+-------------------------------------------------------------+
| Bare Metal Production System                                |
|  User App (mlockall) <==> Linux Kernel <==> Physical RAM    |
|  [Guaranteed Physical Frame Pinning - No Secondary Paging]  |
+-------------------------------------------------------------+

+-------------------------------------------------------------+
| VirtualBox Virtual Machine                                  |
|  User App (mlockall) <==> Guest Kernel <==> Guest Phys RAM  |
|                                                    |        |
|                                       Hypervisor (Host OS)  |
|                                                    |        |
|                                            Host Physical RAM|
|  [Guest mlockall DOES NOT Guarantee Host Physical Residency]|
+-------------------------------------------------------------+
```

### VirtualBox Architectural Limitations
1. **No Guarantee of Host Physical Memory Residency:**
   Guest-level memory locking via `mlockall` operates exclusively within the guest Linux kernel. It locks Guest Virtual Addresses (GVA) to Guest Physical Addresses (GPA) within the guest page tables. However, **guest-level memory locking does not guarantee host physical memory residency or eliminate hypervisor-related memory-management effects**. The host hypervisor translates GPAs to Host Physical Addresses (HPA) via Extended Page Tables (EPT) or nested paging. The guest kernel has no architectural mechanism to compel the host operating system to wire-lock the host physical RAM frames backing the virtual machine.
2. **Persistence of Hypervisor-Related Memory Effects:**
   Guest-level memory locking does not eliminate hypervisor-level memory management effects, such as host page swapping, host memory ballooning, kernel samepage merging (KSM), or host CPU thread preemption and steal time. If the host operating system experiences memory pressure or CPU scheduling delays, the virtual machine will experience latency spikes regardless of guest `mlockall` status.
3. **Absence of System Memory Pressure:**
   Because the virtual machine had ample unallocated memory (~867 MB available vs. ~20 MB working set), the Linux kernel was under no memory pressure to reclaim anonymous pages even in normal mode. On a system under active memory pressure, `mlockall` prevents catastrophic swapping to disk, but within an idle VM with sufficient RAM, major page faults remained at zero for both configurations.

### Production Bare-Metal Recommendations for Ultra-Low-Latency Trading
In an enterprise low-latency electronic trading plant, memory locking is never deployed in isolation. It forms one layer of a comprehensive bare-metal memory architecture:

1. **`mlockall(MCL_CURRENT | MCL_FUTURE)`:**
   Mandatory at process initialization to lock code, heap, and thread stacks, preventing any paging during trading hours.
2. **Linux HugePages (2 MB or 1 GB Pages):**
   Standard 4 KB pages impose severe TLB overhead. Configuring 2 MB HugePages (`hugetlbfs`) reduces TLB entries by a factor of 512, directly mitigating TLB misses and MMU page table walks during rapid order processing.
3. **NUMA Memory Binding (`numactl --membind` / `mbind`):**
   On multi-socket servers, matching engine memory must be allocated strictly on the NUMA node local to the CPU socket executing the matching thread, eliminating UPI/QPI cross-socket interconnect latency.
4. **Complete Swap Elimination:**
   Disable swap entirely on trading nodes (`swapoff -a`), ensuring the kernel never attempts page-out operations under any circumstances.
5. **Combined with OS Isolation (`isolcpus`, `nohz_full`, `SCHED_FIFO`):**
   Combine `mlockall` with dedicated isolated cores (`isolcpus`), tickless kernels (`nohz_full`), and real-time scheduling (`SCHED_FIFO`) to ensure the matching engine thread experiences zero kernel interference.

---

## 8. Summary & Phase 5 Progression

| Milestone | Optimization Layer | Primary Mechanism | Key Empirical Finding |
| :--- | :--- | :--- | :--- |
| **Phase 5.1** | CPU Pinning | `pthread_setaffinity_np` | Eliminated core migrations; stabilized queue throughput. |
| **Phase 5.2** | Scheduling Policy | `SCHED_FIFO` (POSIX RT) | Eliminated CFS preemption; +53.8% throughput; -52.5% variance. |
| **Phase 5.3** | Virtual Memory | `mlockall(MCL_CURRENT\|FUTURE)` | **Complete with documented limitations:** Prevents swap-out of locked pages; workload-dependent throughput & latency trade-offs (64K: throughput up, P50 up; 1K: throughput down, P50 down, P99 up); no elimination of TLB misses or page walks; no host residency guarantees under VirtualBox. |

Phase 5.3 is complete, with all empirical results, methodology constraints, and virtualization limitations rigorously documented.
