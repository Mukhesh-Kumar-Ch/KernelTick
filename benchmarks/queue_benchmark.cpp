#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <cassert>
#include <iomanip>
#include <barrier>
#include <cstring>
#include <cerrno>
#include <sched.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include "../include/utils/SPSCQueue.hpp"
#include "benchmark_utils.hpp"

using namespace kerneltick::benchmark;

// Message structure for queue benchmarking
struct alignas(16) BenchmarkMessage {
    uint64_t sequence;
    int64_t enqueue_timestamp_ns;
};
static_assert(std::is_trivially_copyable_v<BenchmarkMessage>);

// Helper to query locked memory in KB from /proc/self/status
inline uint64_t getVmLckKb() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmLck:", 0) == 0) {
            std::istringstream iss(line.substr(6));
            uint64_t kb = 0;
            iss >> kb;
            return kb;
        }
    }
    return 0;
}

// Helper to query available system memory in KB from /proc/meminfo
inline uint64_t getMemAvailableKb() {
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.rfind("MemAvailable:", 0) == 0) {
            std::istringstream iss(line.substr(13));
            uint64_t kb = 0;
            iss >> kb;
            return kb;
        }
    }
    return 0;
}

// Helper to activate memory locking safely
inline bool activateMemoryLocking() {
    struct rlimit rl{};
    if (getrlimit(RLIMIT_MEMLOCK, &rl) != 0) {
        std::cerr << "Warning: getrlimit(RLIMIT_MEMLOCK) failed: " << std::strerror(errno) << "\n";
    } else {
        std::cout << "[Memory Locking Configuration]\n"
                  << "  RLIMIT_MEMLOCK: soft=" << rl.rlim_cur << " bytes, hard=" << rl.rlim_max << " bytes\n";
    }

    uint64_t mem_avail_kb = getMemAvailableKb();
    std::cout << "  System Available Memory: " << (mem_avail_kb / 1024) << " MB\n";
    if (mem_avail_kb < 50 * 1024) { // Less than 50 MB
        std::cerr << "Error: Available system memory is too low (" << (mem_avail_kb / 1024)
                  << " MB) to safely lock memory without risking OOM.\n";
        return false;
    }

    std::cout << "  Attempting mlockall(MCL_CURRENT | MCL_FUTURE)...\n";
    int rc = mlockall(MCL_CURRENT | MCL_FUTURE);
    if (rc != 0) {
        int err = errno;
        std::cerr << "Error: mlockall(MCL_CURRENT | MCL_FUTURE) failed: "
                  << std::strerror(err) << " (errno " << err << ")\n";
        if (err == ENOMEM) {
            std::cerr << "  Diagnosis: Memory to be locked exceeds RLIMIT_MEMLOCK or available RAM.\n"
                      << "  Consider granting CAP_IPC_LOCK or raising memlock limit in /etc/security/limits.conf.\n";
        } else if (err == EPERM) {
            std::cerr << "  Diagnosis: Insufficient privilege (CAP_IPC_LOCK capability required).\n";
        }
        return false;
    }

    uint64_t vmlck_kb = getVmLckKb();
    std::cout << "  mlockall(MCL_CURRENT | MCL_FUTURE) activated successfully.\n"
                  << "  Kernel Verified Locked Memory (VmLck): " << vmlck_kb << " kB\n\n";
    return true;
}

// Helper to query available CPUs for current process using sched_getaffinity
inline std::vector<int> getAvailableCpus() {
    std::vector<int> cpus;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    if (sched_getaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        int err = errno;
        std::cerr << "Error: sched_getaffinity failed: " << std::strerror(err)
                  << " (errno " << err << ")\n";
        return cpus;
    }
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &cpuset)) {
            cpus.push_back(cpu);
        }
    }
    return cpus;
}

// Warm-up helper using synchronized barrier to prime threads, memory pages, and caches
template<size_t QueueCapacity>
void warmupQueue(
    SPSCQueue<BenchmarkMessage, QueueCapacity>& queue,
    size_t warmup_count,
    bool use_affinity,
    int producer_cpu,
    int consumer_cpu,
    bool use_realtime,
    int realtime_priority)
{
    std::barrier sync_barrier(2);

    std::thread consumer([&]() {
        if (use_affinity) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(consumer_cpu, &cpuset);
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        }
        if (use_realtime) {
            sched_param param{};
            param.sched_priority = realtime_priority;
            pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
        }
        sync_barrier.arrive_and_wait();

        BenchmarkMessage msg{};
        size_t count = 0;
        while (count < warmup_count) {
            if (queue.pop(msg)) {
                doNotOptimize(msg);
                count++;
            } else {
                cpuPause();
            }
        }
    });

    std::thread producer([&]() {
        if (use_affinity) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(producer_cpu, &cpuset);
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        }
        if (use_realtime) {
            sched_param param{};
            param.sched_priority = realtime_priority;
            pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
        }
        sync_barrier.arrive_and_wait();

        for (size_t i = 0; i < warmup_count; ++i) {
            BenchmarkMessage msg{i, getSteadyClockNs()};
            while (!queue.push(msg)) {
                cpuPause();
            }
        }
    });

    producer.join();
    consumer.join();
    assert(queue.empty());
}

// SPSC Queue Benchmark Runner with barrier synchronization, retry counting, separated metrics, and CPU affinity
template<size_t QueueCapacity>
BenchmarkRunResult runSPSCQueueBenchmark(
    const std::string& bench_name,
    size_t N,
    size_t run_idx,
    bool use_affinity,
    int producer_cpu,
    int consumer_cpu,
    bool use_realtime,
    int realtime_priority,
    bool use_memlock)
{
    SPSCQueue<BenchmarkMessage, QueueCapacity> queue;

    // Dedicated warm-up before measured run to prime page tables and CPU caches
    warmupQueue<QueueCapacity>(queue, std::min(N, static_cast<size_t>(10000)),
                               use_affinity, producer_cpu, consumer_cpu,
                               use_realtime, realtime_priority);

    std::vector<int64_t> op_latencies_ns(N, 0);
    std::vector<int64_t> e2e_latencies_ns(N, 0);

    // Capture initial process resource usage (page faults) outside timed section
    struct rusage ru_before{};
    getrusage(RUSAGE_SELF, &ru_before);

    // C++20 synchronization barrier: Producer and Consumer synchronize arrival
    std::barrier sync_barrier(2);

    int64_t prod_t0 = 0;
    int64_t prod_t1 = 0;
    int64_t cons_t0 = 0;
    int64_t cons_t1 = 0;

    uint64_t recorded_push_retries = 0;
    uint64_t recorded_empty_pops = 0;
    std::atomic<size_t> consumed_count{0};

    std::atomic<bool> affinity_error{false};
    std::atomic<int> affinity_errno{0};
    std::atomic<bool> sched_error{false};
    std::atomic<int> sched_errno{0};
    std::atomic<int> consumer_policy{SCHED_OTHER};
    std::atomic<int> consumer_prio{0};
    std::atomic<int> producer_policy{SCHED_OTHER};
    std::atomic<int> producer_prio{0};

    // Consumer thread: pops messages, records operation latency, dwell latency, and empty pop counts
    std::thread consumer([&]() {
        if (use_affinity) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(consumer_cpu, &cpuset);
            int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
            if (rc != 0) {
                affinity_errno.store(rc);
                affinity_error.store(true);
            }
        }

        if (use_realtime) {
            sched_param param{};
            param.sched_priority = realtime_priority;
            int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
            if (rc != 0) {
                sched_errno.store(rc);
                sched_error.store(true);
            }
        }

        int pol = 0;
        sched_param cur_param{};
        if (pthread_getschedparam(pthread_self(), &pol, &cur_param) == 0) {
            consumer_policy.store(pol);
            consumer_prio.store(cur_param.sched_priority);
        }

        sync_barrier.arrive_and_wait();
        cons_t0 = getSteadyClockNs(); // Record consumer start timestamp immediately upon barrier release

        size_t count = 0;
        uint64_t empty_pops = 0;
        BenchmarkMessage msg{};

        while (count < N) {
            int64_t pop_t0 = getSteadyClockNs();
            if (queue.pop(msg)) {
                int64_t pop_t1 = getSteadyClockNs();

                doNotOptimize(msg);

                // Operation latency: CPU execution time of pop()
                op_latencies_ns[count] = pop_t1 - pop_t0;

                // End-to-end latency: Queue dwell / transit time
                int64_t e2e = pop_t1 - msg.enqueue_timestamp_ns;
                e2e_latencies_ns[count] = (e2e > 0) ? e2e : 0;

                count++;
            } else {
                empty_pops++; // Polling attempt found queue empty
                cpuPause();
            }
        }

        cons_t1 = getSteadyClockNs(); // Record consumer completion timestamp
        recorded_empty_pops = empty_pops;
        consumed_count.store(count, std::memory_order_release);
    });

    // Producer thread: pushes N messages, records duration and push backpressure retries
    std::thread producer([&]() {
        if (use_affinity) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(producer_cpu, &cpuset);
            int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
            if (rc != 0) {
                affinity_errno.store(rc);
                affinity_error.store(true);
            }
        }

        if (use_realtime) {
            sched_param param{};
            param.sched_priority = realtime_priority;
            int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
            if (rc != 0) {
                sched_errno.store(rc);
                sched_error.store(true);
            }
        }

        int pol = 0;
        sched_param cur_param{};
        if (pthread_getschedparam(pthread_self(), &pol, &cur_param) == 0) {
            producer_policy.store(pol);
            producer_prio.store(cur_param.sched_priority);
        }

        sync_barrier.arrive_and_wait();
        prod_t0 = getSteadyClockNs(); // Record producer start timestamp immediately upon barrier release

        uint64_t push_retries = 0;
        for (size_t i = 0; i < N; ++i) {
            BenchmarkMessage msg;
            msg.sequence = i;
            msg.enqueue_timestamp_ns = getSteadyClockNs();

            while (!queue.push(msg)) {
                push_retries++; // Polling attempt found queue full
                cpuPause();     // Backpressure spin-wait with CPU pause
            }
        }

        prod_t1 = getSteadyClockNs(); // Record producer completion timestamp
        recorded_push_retries = push_retries;
    });

    producer.join();
    consumer.join();

    // Capture post-execution process resource usage (page faults) outside timed section
    struct rusage ru_after{};
    getrusage(RUSAGE_SELF, &ru_after);

    if (affinity_error.load()) {
        int err = affinity_errno.load();
        std::cerr << "Fatal Error: pthread_setaffinity_np failed: " << std::strerror(err)
                  << " (code " << err << ")\n";
        std::exit(1);
    }

    bool both_fifo = (consumer_policy.load() == SCHED_FIFO && producer_policy.load() == SCHED_FIFO);
    if (use_realtime && !both_fifo) {
        int err = sched_errno.load();
        std::cerr << "\nFatal Error: Requested SCHED_FIFO (priority " << realtime_priority
                  << ") failed on worker thread: " << std::strerror(err)
                  << " (errno " << err << ").\n"
                  << "Aborting FIFO experiment: Both workers must genuinely execute under SCHED_FIFO.\n";
        std::exit(1);
    }

    // Measurement 1: Start-time difference between producer and consumer (skew in barrier exit timestamps)
    int64_t start_skew_ns = prod_t0 - cons_t0;

    // Measurement 2: Workload duration strictly from earliest thread start to latest thread finish (excluding thread creation/teardown)
    int64_t earliest_start = std::min(prod_t0, cons_t0);
    int64_t latest_finish = std::max(prod_t1, cons_t1);
    int64_t total_pipeline_elapsed_ns = latest_finish - earliest_start;

    // Measurement 3: Individual execution durations
    int64_t producer_duration_ns = prod_t1 - prod_t0;
    int64_t consumer_duration_ns = cons_t1 - cons_t0;

    // Correctness checks
    assert(consumed_count.load() == N);
    assert(queue.empty());

    BenchmarkRunResult res;
    res.benchmark_name = bench_name;
    res.queue_capacity = QueueCapacity;
    res.producer_cpu = use_affinity ? producer_cpu : -1;
    res.consumer_cpu = use_affinity ? consumer_cpu : -1;
    res.memlock_enabled = use_memlock;
    res.minor_faults_before = ru_before.ru_minflt;
    res.minor_faults_after = ru_after.ru_minflt;
    res.delta_minor_faults = ru_after.ru_minflt - ru_before.ru_minflt;
    res.major_faults_before = ru_before.ru_majflt;
    res.major_faults_after = ru_after.ru_majflt;
    res.delta_major_faults = ru_after.ru_majflt - ru_before.ru_majflt;

    if (use_memlock) {
        res.sched_policy = "SCHED_OTHER";
        res.sched_priority = 0;
        res.execution_mode = use_affinity ? "affinity_locked" : "locked";
    } else if (both_fifo) {
        res.sched_policy = "SCHED_FIFO";
        res.sched_priority = consumer_prio.load();
        res.execution_mode = use_affinity ? "affinity_fifo" : "fifo";
    } else {
        res.sched_policy = "SCHED_OTHER";
        res.sched_priority = 0;
        if (use_realtime) {
            res.execution_mode = use_affinity ? "affinity_other_eperm" : "default";
        } else if (use_affinity) {
            res.execution_mode = (bench_name.find("normal") != std::string::npos) ? "affinity_normal" : "affinity";
        } else {
            res.execution_mode = (bench_name.find("normal") != std::string::npos) ? "normal" : "default";
        }
    }
    res.workload_size = N;
    res.run_index = run_idx;
    res.operations = N;

    // Total pipeline elapsed time and consistent throughput (total_messages / total_pipeline_elapsed_seconds)
    res.duration_sec = static_cast<double>(total_pipeline_elapsed_ns) / 1e9;
    res.duration_ms = static_cast<double>(total_pipeline_elapsed_ns) / 1e6;
    res.throughput_ops_sec = (res.duration_sec > 0.0) ? (static_cast<double>(N) / res.duration_sec) : 0.0;

    // Individual worker execution durations and rates
    res.producer_duration_ms = static_cast<double>(producer_duration_ns) / 1e6;
    res.consumer_duration_ms = static_cast<double>(consumer_duration_ns) / 1e6;
    double prod_sec = static_cast<double>(producer_duration_ns) / 1e9;
    double cons_sec = static_cast<double>(consumer_duration_ns) / 1e9;
    res.producer_throughput_ops_sec = (prod_sec > 0.0) ? (static_cast<double>(N) / prod_sec) : 0.0;
    res.consumer_throughput_ops_sec = (cons_sec > 0.0) ? (static_cast<double>(N) / cons_sec) : 0.0;

    // Start-time skew and warning check (> 5% of total pipeline elapsed time)
    res.start_skew_ns = start_skew_ns;
    if (total_pipeline_elapsed_ns > 0) {
        res.skew_percent_of_duration = (static_cast<double>(std::abs(start_skew_ns)) / static_cast<double>(total_pipeline_elapsed_ns)) * 100.0;
        res.has_skew_warning = (res.skew_percent_of_duration > 5.0);
    }

    // Retries due to backpressure or starvation
    res.push_retries = recorded_push_retries;
    res.empty_pop_retries = recorded_empty_pops;

    // Latency distributions
    res.op_latency = calculateLatencyStats(op_latencies_ns);
    res.e2e_latency = calculateLatencyStats(e2e_latencies_ns);
    res.has_e2e_latency = true;

    res.compiler_config = getCompilerConfig();

    std::cout << "    [Run #" << run_idx << "] Pipeline Time: " << std::fixed << std::setprecision(2) << res.duration_ms << " ms"
              << " | Overall Throughput: " << std::fixed << std::setprecision(0) << res.throughput_ops_sec << " ops/s\n";
    if (use_affinity) {
        std::cout << "             Affinity: Producer on CPU " << producer_cpu
                  << ", Consumer on CPU " << consumer_cpu << "\n";
    } else {
        std::cout << "             Affinity: Default Linux scheduling (unpinned)\n";
    }
    std::cout << "             Scheduling: " << res.sched_policy
              << " (priority " << res.sched_priority << ")";
    if (use_realtime && !both_fifo) {
        std::cout << " [Requested SCHED_FIFO prio " << realtime_priority << " denied: EPERM]";
    }
    std::cout << "\n";
    std::cout << "             Memory: " << (use_memlock ? "LOCKED (mlockall MCL_CURRENT|MCL_FUTURE)" : "NORMAL (unlocked)")
              << " | Page Faults: Minor=" << res.delta_minor_faults << " (delta), Major=" << res.delta_major_faults << " (delta)\n";
    std::cout << "             Producer: " << res.producer_duration_ms << " ms (" << res.producer_throughput_ops_sec << " ops/s)"
              << " | Consumer: " << res.consumer_duration_ms << " ms (" << res.consumer_throughput_ops_sec << " ops/s)\n"
              << "             Start Skew: " << formatSkewDisplay(res.start_skew_ns)
              << " (" << std::fixed << std::setprecision(1) << res.skew_percent_of_duration << "% of pipeline)";
    if (res.has_skew_warning) {
        std::cout << " [WARNING: Start skew > 5% of pipeline elapsed time]";
    }
    std::cout << "\n"
              << "             Retries: PushFull=" << res.push_retries << ", PopEmpty=" << res.empty_pop_retries << "\n"
              << "             Pop Op Latency: P50=" << std::fixed << std::setprecision(0) << res.op_latency.p50_ns << " ns, P99=" 
              << res.op_latency.p99_ns << " ns (Avg=" << std::setprecision(1) << res.op_latency.avg_ns << " ns)\n"
              << "             E2E Dwell Time: P50=" << std::fixed << std::setprecision(0) << res.e2e_latency.p50_ns << " ns, P99=" 
              << res.e2e_latency.p99_ns << " ns (Avg=" << std::setprecision(1) << res.e2e_latency.avg_ns << " ns)\n";

    return res;
}

int main(int argc, char* argv[]) {
    size_t workload_size = 1000000; // Standard 1M messages for full pipeline testing
    size_t repetitions = 5;         // Default: 5 independent repetitions
    std::string csv_path = "";
    bool run_all_sizes = false;
    bool use_affinity = false;
    bool use_realtime = false;
    bool use_memlock = false;
    int realtime_priority = 10;
    bool overwrite_csv = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--size" && i + 1 < argc) {
            workload_size = std::stoull(argv[++i]);
        } else if (arg == "--runs" && i + 1 < argc) {
            repetitions = std::stoull(argv[++i]);
        } else if (arg == "--csv" && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (arg == "--affinity") {
            use_affinity = true;
        } else if (arg == "--realtime") {
            use_realtime = true;
        } else if (arg == "--memlock") {
            use_memlock = true;
        } else if (arg == "--priority" && i + 1 < argc) {
            realtime_priority = std::stoi(argv[++i]);
        } else if (arg == "--overwrite") {
            overwrite_csv = true;
        } else if (arg == "--all") {
            run_all_sizes = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "KernelTick Queue Benchmark (Phase 5.3 Memory Locking & Scheduling)\n"
                      << "Usage: " << argv[0] << " [options]\n\n"
                      << "Options:\n"
                      << "  --size <N>       Set queue message count (default: 1000000)\n"
                      << "  --runs <R>       Number of independent repetitions (default: 5)\n"
                      << "  --affinity       Pin producer and consumer to separate available CPUs\n"
                      << "  --realtime       Request SCHED_FIFO real-time scheduling on worker threads\n"
                      << "  --priority <P>   Real-time priority for SCHED_FIFO (default: 10, max: 98)\n"
                      << "  --memlock        Lock memory into RAM via mlockall(MCL_CURRENT | MCL_FUTURE)\n"
                      << "  --csv <path>     Custom CSV output destination\n"
                      << "  --overwrite      Overwrite CSV output destination instead of appending\n"
                      << "  --all            Run 10K, 100K, and 1M benchmarks sequentially\n"
                      << "  --help, -h       Show help\n";
            return 0;
        }
    }

    if (csv_path.empty()) {
        if (use_memlock) {
            csv_path = "benchmark_results/memory_locking_benchmark.csv";
        } else if (use_realtime) {
            csv_path = "benchmark_results/scheduling_benchmark.csv";
        } else if (use_affinity) {
            csv_path = "benchmark_results/cpu_affinity_benchmark.csv";
        } else {
            csv_path = "benchmark_results/cpu_affinity_benchmark.csv";
        }
    }

    if (overwrite_csv) {
        std::error_code ec;
        std::filesystem::remove(csv_path, ec);
    }

    printVirtualizationNotice();

    if (use_memlock) {
        if (!activateMemoryLocking()) {
            std::cerr << "Stopping: Memory locking activation failed. Halting benchmark run.\n";
            return 1;
        }
    }

    int min_rt_prio = sched_get_priority_min(SCHED_FIFO);
    int max_rt_prio = sched_get_priority_max(SCHED_FIFO);
    if (use_realtime) {
        if (realtime_priority >= 99) {
            std::cerr << "Error: Real-time priority " << realtime_priority
                      << " is prohibited by KernelTick benchmark safety guidelines to prevent kernel thread lockup.\n"
                      << "Permitted priority range is [" << min_rt_prio << ".." << std::min(max_rt_prio, 98) << "].\n";
            return 1;
        }
        if (realtime_priority < min_rt_prio || realtime_priority > max_rt_prio) {
            std::cerr << "Error: Priority " << realtime_priority << " is outside permitted SCHED_FIFO range ["
                      << min_rt_prio << ".." << max_rt_prio << "].\n";
            return 1;
        }
    }

    int producer_cpu = -1;
    int consumer_cpu = -1;

    if (use_affinity) {
        std::vector<int> available_cpus = getAvailableCpus();
        if (available_cpus.size() < 2) {
            std::cerr << "Error: CPU affinity benchmark requires at least 2 available CPUs for separate worker placement.\n"
                      << "Only " << available_cpus.size() << " available CPU(s) detected via sched_getaffinity.\n"
                      << "Cannot run affinity experiment.\n";
            return 1;
        }
        producer_cpu = available_cpus[0];
        consumer_cpu = available_cpus[1];

        std::cout << "[Affinity Configuration]\n"
                  << "  Available CPUs detected: ";
        for (size_t i = 0; i < available_cpus.size(); ++i) {
            std::cout << available_cpus[i] << (i + 1 < available_cpus.size() ? ", " : "\n");
        }
        std::cout << "  Producer thread pinned to CPU: " << producer_cpu << "\n"
                  << "  Consumer thread pinned to CPU: " << consumer_cpu << "\n"
                  << "  Coordinator thread: unpinned (default Linux scheduling)\n\n";
    } else {
        std::cout << "[Affinity Configuration]\n"
                  << "  Default Linux scheduling (no CPU affinity applied)\n\n";
    }

    bool realtime_supported = false;
    int probe_errno = 0;
    if (use_realtime) {
        std::thread probe([&]() {
            sched_param param{};
            param.sched_priority = realtime_priority;
            int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
            if (rc == 0) {
                realtime_supported = true;
                param.sched_priority = 0;
                pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
            } else {
                probe_errno = rc;
            }
        });
        probe.join();

        std::cout << "[Real-Time Scheduling Configuration]\n"
                  << "  Target Policy: SCHED_FIFO\n"
                  << "  Requested Priority: " << realtime_priority << " (valid range: [" << min_rt_prio << ".." << max_rt_prio << "])\n";
        if (realtime_supported) {
            std::cout << "  Permission Check: SUCCESS (CAP_SYS_NICE or RLIMIT_RTPRIO available)\n"
                      << "  Workers will genuinely execute under SCHED_FIFO (priority " << realtime_priority << ")\n\n";
        } else {
            std::cerr << "  Permission Check: DENIED (" << std::strerror(probe_errno) << ", errno " << probe_errno << " / EPERM)\n"
                      << "  Diagnosis: Current process lacks CAP_SYS_NICE capability and RLIMIT_RTPRIO is 0.\n"
                      << "  Stopping: Genuine SCHED_FIFO execution is required. Halting benchmark run.\n\n";
            return 1;
        }
    } else {
        std::cout << "[Scheduling Policy Configuration]\n"
                  << "  Policy: SCHED_OTHER (Linux Completely Fair Scheduler, priority 0)\n\n";
    }

    std::vector<size_t> sizes;
    if (run_all_sizes) {
        sizes = {10000, 100000, 1000000};
    } else {
        sizes = {workload_size};
    }

    std::vector<BenchmarkRunResult> all_runs;
    std::vector<AggregateRunStats> all_aggregates;

    for (size_t sz : sizes) {
        std::string mode_desc = use_affinity ? "AFFINITY" : "DEFAULT";
        if (use_memlock) {
            mode_desc += " + MEMLOCK (mlockall)";
        } else if (use_realtime) {
            mode_desc += realtime_supported ? " + SCHED_FIFO" : " + SCHED_FIFO (EPERM FALLBACK)";
        } else {
            mode_desc += " + SCHED_OTHER";
        }

        std::cout << ">>> Running SPSC Queue Benchmarks: Size = " << sz 
                  << " messages, Repetitions = " << repetitions 
                  << " [" << mode_desc << "] <<<\n";

        std::vector<BenchmarkRunResult> runs_64k;
        std::vector<BenchmarkRunResult> runs_1k;

        std::string name_64k;
        std::string name_1k;
        if (csv_path.find("memory_locking") != std::string::npos) {
            name_64k = use_memlock ? "spsc_64k_locked" : "spsc_64k_normal";
            name_1k  = use_memlock ? "spsc_1k_locked"  : "spsc_1k_normal";
        } else if (use_realtime) {
            name_64k = realtime_supported ? "spsc_64k_sched_fifo" : "spsc_64k_sched_fifo_eperm";
            name_1k  = realtime_supported ? "spsc_1k_sched_fifo"  : "spsc_1k_sched_fifo_eperm";
        } else if (use_affinity) {
            if (csv_path.find("scheduling") != std::string::npos) {
                name_64k = "spsc_64k_sched_other";
                name_1k  = "spsc_1k_sched_other";
            } else {
                name_64k = "spsc_64k_affinity";
                name_1k  = "spsc_1k_affinity";
            }
        } else {
            name_64k = "spsc_64k_default";
            name_1k  = "spsc_1k_default";
        }

        std::cout << "\n  --- Capacity 64K (65,536 buffer slots, standard HFT buffer) ---\n";
        for (size_t r = 1; r <= repetitions; ++r) {
            BenchmarkRunResult res = runSPSCQueueBenchmark<65536>(
                name_64k, sz, r, use_affinity, producer_cpu, consumer_cpu,
                use_realtime, realtime_priority, use_memlock);
            runs_64k.push_back(res);
            all_runs.push_back(res);
        }

        std::cout << "\n  --- Capacity 1K (1,024 buffer slots, backpressure sensitive) ---\n";
        for (size_t r = 1; r <= repetitions; ++r) {
            BenchmarkRunResult res = runSPSCQueueBenchmark<1024>(
                name_1k, sz, r, use_affinity, producer_cpu, consumer_cpu,
                use_realtime, realtime_priority, use_memlock);
            runs_1k.push_back(res);
            all_runs.push_back(res);
        }

        all_aggregates.push_back(computeAggregateStats(runs_64k));
        all_aggregates.push_back(computeAggregateStats(runs_1k));
    }

    printResultTable(all_runs, all_aggregates);

    if (exportToCSV(csv_path, all_runs, all_aggregates)) {
        std::cout << "Results exported successfully to: " << csv_path << "\n";
    }

    if (use_memlock) {
        munlockall();
    }

    return 0;
}
