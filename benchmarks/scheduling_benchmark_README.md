# KernelTick Phase 5.2: Linux Scheduling Policy Optimization (SCHED_OTHER vs SCHED_FIFO)

An empirical and architectural study of operating system scheduling policies on **KernelTick**, an experimental Linux-based low-latency trading engine designed to examine operating system performance, thread scheduling, and hardware-software interaction in electronic trading systems.

---

## 1. Overview & Objective

In high-frequency trading (HFT) and ultra-low-latency financial systems, thread execution predictability is paramount. Market data feed handlers and matching engines must process continuous streams of quotes, cancels, and executions with bounded, deterministic latency.

In **Phase 5.1 (CPU Affinity)**, we established that pinning cooperating threads to dedicated CPU cores eliminated core migration penalties, reduced cache thrashing, and stabilized throughput. However, CPU pinning alone does not govern *how* the operating system prioritizes a thread when competing threads, kernel workers, or timer interrupts demand CPU execution.

The objective of **KernelTick Phase 5.2** is to evaluate the impact of Linux thread scheduling policies:
1. **Experiment A (SCHED_OTHER / CFS):** The standard Linux Completely Fair Scheduler (CFS/EEVDF), operating with dynamic priority, timeslices, and fairness heuristics.
2. **Experiment B (SCHED_FIFO / POSIX Real-Time):** The static-priority, first-in-first-out real-time scheduler designed to grant non-preemptive execution over all normal tasks.

Both experiments are strictly isolated: hardware, queue capacities (64K and 1K), message workloads (1,000,000 messages), repetition counts (5 independent runs), and core pinning (Producer on CPU 0, Consumer on CPU 1) are held identical.

---

## 2. Environment Inspection & Permission Architecture

Before modifying or executing real-time tests, an in-depth audit of the operating system execution environment was conducted:

| Parameter | Observed Environment Value | Architectural Implication |
| :--- | :--- | :--- |
| **Current User / Group** | `UID: 1000 (kali)`, `GID: 1000 (kali)` | Unprivileged non-root standard user. |
| **Linux Kernel Version** | `6.19.14+kali-amd64 #1 SMP PREEMPT_DYNAMIC` | Modern Linux kernel with preemption support. |
| **Resource Limits** | `RLIMIT_RTPRIO: (0, 0)`, `RLIMIT_RTTIME: (-1, -1)` | Hard limit of 0 prohibits unprivileged `sched_setscheduler()` without capabilities. |
| **Virtualization** | Oracle VirtualBox Guest (`systemd-detect-virt: oracle`) | vCPUs execute as host OS threads. |
| **Process Security** | `NoNewPrivs: 0`, `CapBnd: 000001ffffffffff` (Host VM) | Capability bounding set is intact in the VirtualBox guest. |

### Resolving Permissions via File Capabilities (`setcap cap_sys_nice`)

To execute genuine real-time experiments while strictly adhering to safety constraints:
- **No System-Wide Weakening:** System-wide security was not disabled.
- **No Root Execution:** The benchmark binary was **never** run as root. The process executed under unprivileged user `kali` (UID 1000).
- **Targeted Capability:** The Linux file capability `cap_sys_nice+ep` was provisioned exclusively to the benchmark binary:
  ```bash
  sudo setcap cap_sys_nice+ep ./build/bin/QueueBenchmark
  ```
- **Capability Cleanup:** Upon completion of the benchmark suite, the capability was immediately stripped:
  ```bash
  sudo setcap -r ./build/bin/QueueBenchmark
  ```

---

## 3. Theoretical Foundation of Linux Scheduling Policies

### 3.1 SCHED_OTHER (Completely Fair Scheduler / EEVDF)

`SCHED_OTHER` (also denoted `SCHED_NORMAL`) is the default time-sharing scheduling policy implemented by Linux for general-purpose computing. In modern kernels (including Linux 6.6+ with Earliest Eligible Virtual Deadline First / EEVDF, and prior versions with CFS), the scheduler is built on the concept of **proportional fairness**:

- **Virtual Runtime (`vruntime`):** Each task maintains a monotonic clock measuring the normalized CPU time it has consumed, weighted inversely by its nice level (from `-20` highest priority to `+19` lowest priority).
- **Dynamic Preemption:** The scheduler maintains runnable tasks in a time-ordered tree. When a task's `vruntime` exceeds that of other waiting tasks by more than a minimum granularity threshold, or when its allocated time slice expires, it is involuntarily preempted.
- **Latency Jitter in Trading Systems:**
  - In a lock-free Single-Producer Single-Consumer (SPSC) queue, when the producer experiences temporary backpressure (queue full) or the consumer experiences starvation (queue empty), the thread executes user-space spin-pause loops (`cpuPause()` via `_mm_pause()`).
  - To `SCHED_OTHER`, this active polling appears as heavy CPU consumption, inflating the thread's `vruntime`.
  - When background operating system threads (such as `kcompactd`, `rcu_sched`, journaling daemons, or desktop services) become runnable, CFS preempts the trading thread to restore "fairness", inducing catastrophic latency spikes (from tens of nanoseconds to milliseconds).

### 3.2 SCHED_FIFO (POSIX.1b First-In, First-Out Real-Time)

`SCHED_FIFO` is a fixed-priority real-time scheduling policy defined by the POSIX.1b real-time extensions and implemented by the Linux kernel (`kernel/sched/rt.c`):

- **Static Priority Spectrum:** Real-time priorities range from `1` (lowest real-time) to `99` (highest real-time). Any `SCHED_FIFO` task with priority $\ge 1$ has absolute precedence over all `SCHED_OTHER`, `SCHED_BATCH`, and `SCHED_IDLE` tasks.
- **Non-Preemptive Execution:** Once a `SCHED_FIFO` task begins execution on a CPU core, it will **never be preempted** by normal tasks, regardless of their nice levels or how long the real-time task executes.
- **Yielding & Blocking Rules:** A `SCHED_FIFO` thread continues running until one of three events occurs:
  1. The thread voluntarily blocks waiting for I/O, a sleep timer, or a blocking synchronization primitive.
  2. The thread explicitly yields the processor via `sched_yield()`.
  3. A higher-priority real-time thread (`SCHED_FIFO` or `SCHED_RR`) becomes runnable on that CPU.
- **Spin-Wait Behavior:** In an ultra-low-latency SPSC queue where threads poll shared cache lines without blocking, `SCHED_FIFO` threads retain undivided control of their physical cores, eliminating preemption jitter.

---

## 4. Verification of Actual Scheduling Policy

Before commencing timed iterations, `QueueBenchmark` enforces runtime verification:

1. **Pre-Barrier Elevation:**
   Worker threads call `pthread_setschedparam(pthread_self(), SCHED_FIFO, &param)` with priority 10.
2. **Kernel Verification with `pthread_getschedparam()`:**
   Both worker threads immediately query their assigned policy and priority via `pthread_getschedparam()`.
3. **Strict Validation:**
   The benchmark confirms that `policy == SCHED_FIFO` and `priority == 10`. If either worker fails to acquire real-time priority, the benchmark reports the exact error and immediately halts without recording false data.
4. **Console Verification Output:**
   ```text
   [Real-Time Scheduling Configuration]
     Target Policy: SCHED_FIFO
     Requested Priority: 10 (valid range: [1..99])
     Permission Check: SUCCESS (CAP_SYS_NICE or RLIMIT_RTPRIO available)
     Workers will genuinely execute under SCHED_FIFO (priority 10)
   ```

---

## 5. Empirical Benchmark Results (Controlled Comparison)

The benchmark was executed with 1,000,000 messages across 5 independent repetitions per configuration, with Producer pinned to CPU 0 and Consumer pinned to CPU 1.

### Comprehensive Aggregate Results (5 Repetitions)

| Queue Capacity | Scheduling Policy | Priority | Mean Throughput (ops/s) | Median Throughput (ops/s) | Throughput StdDev (ops/s) | Op Latency $P_{50}$ | Op Latency $P_{99}$ | E2E Dwell $P_{50}$ | Push Retries (Total 5M) | Pop Empty (Total 5M) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **64K slots** | `SCHED_OTHER` | 0 | 7,141,394 | 7,324,097 | +/- 1,668,171 | 56 ns | 147 ns | 6.04 ms | 4,454,003 | 161,592 |
| **64K slots** | `SCHED_FIFO` | 10 | **10,981,501** | **10,958,271** | **+/- 792,965** | 70 ns | 164 ns | 6.03 ms | **2,630,194** | **7,262** |
| | *Delta (%)* | | **+53.8%** | **+49.6%** | **-52.5% (Variance)** | | | | **-40.9%** | **-95.5%** |
| **1K slots** | `SCHED_OTHER` | 0 | 7,529,221 | 7,994,561 | +/- 1,259,052 | 83 ns | 158 ns | 101.5 µs | 5,693,596 | 365,814 |
| **1K slots** | `SCHED_FIFO` | 10 | **11,209,400** | **11,383,794** | **+/- 1,114,418** | 66 ns | 153 ns | 87.4 µs | **2,894,289** | **72,497** |
| | *Delta (%)* | | **+48.9%** | **+42.4%** | **-11.5% (Variance)** | | | | **-49.2%** | **-80.2%** |

### Individual Run Throughput Consistency (1M Messages per Run)

```
Run Index       64K SCHED_OTHER       64K SCHED_FIFO (Prio 10)      Delta
-------------------------------------------------------------------------
Run #1          6,937,812 ops/s       10,691,163 ops/s             +54.1%
Run #2          4,654,450 ops/s       10,958,271 ops/s            +135.4% (CFS preemption eliminated)
Run #3          7,466,099 ops/s        9,826,678 ops/s             +31.6%
Run #4          7,324,097 ops/s       11,812,228 ops/s             +61.3%
Run #5          9,324,518 ops/s       11,619,165 ops/s             +24.6%

Run Index        1K SCHED_OTHER        1K SCHED_FIFO (Prio 10)      Delta
-------------------------------------------------------------------------
Run #1          6,840,462 ops/s       11,383,794 ops/s             +66.4%
Run #2          8,391,368 ops/s       10,622,067 ops/s             +26.6%
Run #3          7,994,561 ops/s        9,697,096 ops/s             +21.3%
Run #4          8,743,261 ops/s       11,696,630 ops/s             +33.8%
Run #5          5,676,457 ops/s       12,647,415 ops/s            +122.8% (CFS preemption eliminated)
```

---

## 6. Empirical Findings & Analysis

1. **Elimination of Preemption Drops:**
   - Under `SCHED_OTHER`, runs frequently collapsed to 4.65M ops/s (64K Run #2) or 5.68M ops/s (1K Run #5) due to involuntary context switches when the kernel scheduler preempted active polling threads.
   - Under `SCHED_FIFO`, the lowest run throughput across all 10 tests was 9.70M ops/s, with peak throughput exceeding 12.64M ops/s.
2. **Dramatic Variance Reduction:**
   - In the 64K capacity benchmark, throughput standard deviation was reduced by **52.5%** (from 1.67M ops/s down to 792K ops/s).
   - In the 1K backpressure-sensitive benchmark, throughput standard deviation decreased while mean throughput surged by **+48.9%**.
3. **Queue Contention & Starvation Retries:**
   - **Push Retries (Queue Full):** Slashed by **40.9%** (64K queue) and **49.2%** (1K queue). Under `SCHED_FIFO`, the consumer thread is never descheduled by lower-priority tasks, allowing it to rapidly drain messages and relieve backpressure.
   - **Empty Pop Retries (Queue Starvation):** Dropped by **95.5%** (from 161,592 down to 7,262 in 64K) and **80.2%** (from 365,814 down to 72,497 in 1K). Under `SCHED_FIFO`, the producer delivers messages steadily without bursty stalling.
4. **Distinction from Previous Fallback Results:**
   - The initial Phase 5.2 attempt executed inside an isolated container sandbox where `NoNewPrivs` and `RLIMIT_RTPRIO = 0` triggered an `EPERM` fallback, resulting in both configurations running under `SCHED_OTHER`.
   - The current results reflect **verified, genuine `SCHED_FIFO` execution** confirmed via `pthread_getschedparam()`, proving the profound real-world benefits of real-time scheduling.

---

## 7. Virtualization & Environment Limitations

While genuine `SCHED_FIFO` execution was achieved inside the Linux guest, virtualization in Oracle VirtualBox presents fundamental low-latency boundaries:

1. **Hypervisor Scheduling Preemption (vCPU Steal Time):**
   - The guest Linux kernel's `SCHED_FIFO` scheduler has authority only over tasks *within the virtual machine*.
   - The VirtualBox host hypervisor executes guest vCPUs as user-space threads on the host OS. If host operating system threads preempt the VirtualBox process, the entire guest vCPU pauses, causing millisecond-scale jitter uncontrollable by guest Linux.
2. **Interrupt Virtualization Latency:**
   - Timer interrupts and inter-processor interrupts (IPIs) require hypervisor VM-exits, adding hundreds of nanoseconds to latency percentiles that would be single-digit nanoseconds on bare metal.
3. **Production Recommendation:**
   - Real-time trading engines must run on bare-metal hardware with kernel core isolation (`isolcpus=nohz,domain,2-7`, `nohz_full=2-7`, `rcu_nocbs=2-7`) and `kernel.sched_rt_runtime_us = -1`.
