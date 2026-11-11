#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <cassert>
#include <iomanip>
#include <barrier>
#include "../include/utils/SPSCQueue.hpp"
#include "../include/utils/MutexQueue.hpp"
#include "benchmark_utils.hpp"

using namespace kerneltick::benchmark;

// Standard benchmark message
struct alignas(16) BenchmarkMessage {
    uint64_t sequence;
    int64_t enqueue_timestamp_ns;
};
static_assert(std::is_trivially_copyable_v<BenchmarkMessage>);

// -----------------------------------------------------------------------------
// Step 4: Correctness Verification Suite
// -----------------------------------------------------------------------------

template <typename QueueType>
bool verifyQueueCorrectness(const std::string& queue_name, size_t capacity) {
    QueueType queue;

    // Test 1: Capacity boundaries and FIFO ordering in single-threaded mode
    for (size_t i = 0; i < capacity; ++i) {
        BenchmarkMessage msg{i, 1000 + static_cast<int64_t>(i)};
        if (!queue.push(msg)) {
            std::cerr << "[FAIL] " << queue_name << ": push failed before reaching capacity at item " << i << "\n";
            return false;
        }
    }

    // Next push MUST fail (queue full)
    BenchmarkMessage overflow_msg{999999, 0};
    if (queue.push(overflow_msg)) {
        std::cerr << "[FAIL] " << queue_name << ": push succeeded when queue was full!\n";
        return false;
    }

    // Pop all items and verify FIFO order
    for (size_t i = 0; i < capacity; ++i) {
        BenchmarkMessage popped{};
        if (!queue.pop(popped)) {
            std::cerr << "[FAIL] " << queue_name << ": pop failed at item " << i << "\n";
            return false;
        }
        if (popped.sequence != i) {
            std::cerr << "[FAIL] " << queue_name << ": FIFO sequence mismatch. Expected " << i
                      << ", got " << popped.sequence << "\n";
            return false;
        }
    }

    // Next pop MUST fail (queue empty)
    BenchmarkMessage empty_check{};
    if (queue.pop(empty_check)) {
        std::cerr << "[FAIL] " << queue_name << ": pop succeeded on empty queue!\n";
        return false;
    }

    // Test 2: Concurrent multi-threaded delivery, ordering, and termination
    constexpr size_t CONCURRENT_TEST_COUNT = 100000;
    std::atomic<bool> producer_done{false};
    std::atomic<size_t> consumed_count{0};
    std::atomic<bool> sequence_error{false};

    std::thread consumer([&]() {
        size_t expected_seq = 0;
        BenchmarkMessage msg{};
        while (expected_seq < CONCURRENT_TEST_COUNT) {
            if (queue.pop(msg)) {
                if (msg.sequence != expected_seq) {
                    sequence_error.store(true);
                }
                expected_seq++;
            } else {
                cpuPause();
            }
        }
        consumed_count.store(expected_seq);
    });

    std::thread producer([&]() {
        for (size_t i = 0; i < CONCURRENT_TEST_COUNT; ++i) {
            BenchmarkMessage msg{i, getSteadyClockNs()};
            while (!queue.push(msg)) {
                cpuPause();
            }
        }
        producer_done.store(true);
    });

    producer.join();
    consumer.join();

    if (sequence_error.load()) {
        std::cerr << "[FAIL] " << queue_name << ": Concurrent sequence mismatch / out-of-order delivery!\n";
        return false;
    }

    if (consumed_count.load() != CONCURRENT_TEST_COUNT) {
        std::cerr << "[FAIL] " << queue_name << ": Count mismatch! Produced " << CONCURRENT_TEST_COUNT
                  << ", Consumed " << consumed_count.load() << "\n";
        return false;
    }

    if (!queue.empty()) {
        std::cerr << "[FAIL] " << queue_name << ": Queue not empty after concurrent test completion!\n";
        return false;
    }

    std::cout << "  [PASS] " << queue_name << " (Capacity " << capacity
              << "): FIFO ordering, capacity bounds, and concurrent delivery verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Warm-Up Helper
// -----------------------------------------------------------------------------

template <typename QueueType>
void warmupQueue(QueueType& queue, size_t warmup_count = 10000) {
    std::barrier sync_barrier(2);

    std::thread consumer([&]() {
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

// -----------------------------------------------------------------------------
// Core Concurrent Queue Benchmark Runner
// -----------------------------------------------------------------------------

template <typename QueueType>
BenchmarkRunResult runQueueBenchmark(const std::string& bench_name, size_t N, size_t run_idx) {
    QueueType queue;

    warmupQueue(queue, std::min(N, static_cast<size_t>(10000)));

    std::vector<int64_t> op_latencies_ns(N, 0);
    std::vector<int64_t> e2e_latencies_ns(N, 0);

    std::barrier sync_barrier(2);

    int64_t prod_t0 = 0;
    int64_t prod_t1 = 0;
    int64_t cons_t0 = 0;
    int64_t cons_t1 = 0;

    uint64_t recorded_push_retries = 0;
    uint64_t recorded_empty_pops = 0;
    std::atomic<size_t> consumed_count{0};

    // Consumer thread: pops messages, measures operation latency and end-to-end dwell latency
    std::thread consumer([&]() {
        sync_barrier.arrive_and_wait();
        cons_t0 = getSteadyClockNs();

        size_t count = 0;
        uint64_t empty_pops = 0;
        BenchmarkMessage msg{};

        while (count < N) {
            const int64_t pop_t0 = getSteadyClockNs();
            if (queue.pop(msg)) {
                const int64_t pop_t1 = getSteadyClockNs();
                doNotOptimize(msg);

                op_latencies_ns[count] = pop_t1 - pop_t0;

                const int64_t e2e = pop_t1 - msg.enqueue_timestamp_ns;
                e2e_latencies_ns[count] = (e2e > 0) ? e2e : 0;
                count++;
            } else {
                empty_pops++;
                cpuPause();
            }
        }

        cons_t1 = getSteadyClockNs();
        recorded_empty_pops = empty_pops;
        consumed_count.store(count, std::memory_order_release);
    });

    // Producer thread: pushes N messages, measures push duration and records retry backpressure
    std::thread producer([&]() {
        sync_barrier.arrive_and_wait();
        prod_t0 = getSteadyClockNs();

        uint64_t push_retries = 0;
        for (size_t i = 0; i < N; ++i) {
            BenchmarkMessage msg;
            msg.sequence = i;
            msg.enqueue_timestamp_ns = getSteadyClockNs();

            while (!queue.push(msg)) {
                push_retries++;
                cpuPause();
            }
        }

        prod_t1 = getSteadyClockNs();
        recorded_push_retries = push_retries;
    });

    producer.join();
    consumer.join();

    assert(consumed_count.load() == N);
    assert(queue.empty());

    const int64_t start_skew_ns = prod_t0 - cons_t0;
    const int64_t earliest_start = std::min(prod_t0, cons_t0);
    const int64_t latest_finish = std::max(prod_t1, cons_t1);
    const int64_t total_pipeline_elapsed_ns = latest_finish - earliest_start;

    const int64_t producer_duration_ns = prod_t1 - prod_t0;
    const int64_t consumer_duration_ns = cons_t1 - cons_t0;

    BenchmarkRunResult res;
    res.benchmark_name = bench_name;
    res.workload_size = N;
    res.run_index = run_idx;
    res.operations = N;

    res.duration_sec = static_cast<double>(total_pipeline_elapsed_ns) / 1e9;
    res.duration_ms = static_cast<double>(total_pipeline_elapsed_ns) / 1e6;
    res.throughput_ops_sec = (res.duration_sec > 0.0) ? (static_cast<double>(N) / res.duration_sec) : 0.0;

    res.producer_duration_ms = static_cast<double>(producer_duration_ns) / 1e6;
    res.consumer_duration_ms = static_cast<double>(consumer_duration_ns) / 1e6;
    const double prod_sec = static_cast<double>(producer_duration_ns) / 1e9;
    const double cons_sec = static_cast<double>(consumer_duration_ns) / 1e9;
    res.producer_throughput_ops_sec = (prod_sec > 0.0) ? (static_cast<double>(N) / prod_sec) : 0.0;
    res.consumer_throughput_ops_sec = (cons_sec > 0.0) ? (static_cast<double>(N) / cons_sec) : 0.0;

    res.start_skew_ns = start_skew_ns;
    if (total_pipeline_elapsed_ns > 0) {
        res.skew_percent_of_duration = (static_cast<double>(std::abs(start_skew_ns)) /
                                        static_cast<double>(total_pipeline_elapsed_ns)) * 100.0;
        res.has_skew_warning = (res.skew_percent_of_duration > 5.0);
    }

    res.push_retries = recorded_push_retries;
    res.empty_pop_retries = recorded_empty_pops;

    res.op_latency = calculateLatencyStats(op_latencies_ns);
    res.e2e_latency = calculateLatencyStats(e2e_latencies_ns);
    res.has_e2e_latency = true;

    res.compiler_config = getCompilerConfig();
    return res;
}

// -----------------------------------------------------------------------------
// Side-by-Side Comparison Reporter
// -----------------------------------------------------------------------------

struct QueueComparison {
    std::string capacity_label;
    AggregateRunStats mutex_stats;
    AggregateRunStats spsc_stats;
};

static void printComparisonTable(const std::vector<QueueComparison>& comparisons) {
    std::cout << "\n" << std::string(140, '=') << "\n";
    std::cout << "        KernelTick Phase 4: Concurrency Synchronization Comparison (MutexQueue vs Lock-Free SPSCQueue)\n";
    std::cout << std::string(140, '=') << "\n";
    std::cout << std::left
              << std::setw(26) << "Queue Capacity"
              << std::setw(14) << "Queue Type"
              << std::setw(18) << "Throughput (ops/s)"
              << std::setw(14) << "Op P50 (ns)"
              << std::setw(14) << "Op P99 (ns)"
              << std::setw(14) << "E2E P50 (ns)"
              << std::setw(14) << "E2E P99 (ns)"
              << std::setw(14) << "Push Retries"
              << std::setw(12) << "Speedup"
              << "\n";
    std::cout << std::string(140, '-') << "\n";

    for (const auto& cmp : comparisons) {
        const double mut_thru = cmp.mutex_stats.mean_throughput_ops_sec;
        const double spsc_thru = cmp.spsc_stats.mean_throughput_ops_sec;
        const double speedup = (mut_thru > 0.0) ? (spsc_thru / mut_thru) : 1.0;

        std::ostringstream sp_ss;
        sp_ss << std::fixed << std::setprecision(2) << speedup << "x";

        // Row 1: MutexQueue
        std::cout << std::left
                  << std::setw(26) << cmp.capacity_label
                  << std::setw(14) << "MutexQueue"
                  << std::setw(18) << std::fixed << std::setprecision(0) << mut_thru
                  << std::setw(14) << std::fixed << std::setprecision(0) << cmp.mutex_stats.mean_op_p50_ns
                  << std::setw(14) << std::fixed << std::setprecision(0) << cmp.mutex_stats.mean_op_p99_ns
                  << std::setw(14) << std::fixed << std::setprecision(0) << cmp.mutex_stats.mean_e2e_p50_ns
                  << std::setw(14) << std::fixed << std::setprecision(0) << cmp.mutex_stats.mean_e2e_p99_ns
                  << std::setw(14) << static_cast<uint64_t>(cmp.mutex_stats.mean_push_retries)
                  << std::setw(12) << "baseline"
                  << "\n";

        // Row 2: SPSCQueue
        std::cout << std::left
                  << std::setw(26) << ""
                  << std::setw(14) << "SPSCQueue"
                  << std::setw(18) << std::fixed << std::setprecision(0) << spsc_thru
                  << std::setw(14) << std::fixed << std::setprecision(0) << cmp.spsc_stats.mean_op_p50_ns
                  << std::setw(14) << std::fixed << std::setprecision(0) << cmp.spsc_stats.mean_op_p99_ns
                  << std::setw(14) << std::fixed << std::setprecision(0) << cmp.spsc_stats.mean_e2e_p50_ns
                  << std::setw(14) << std::fixed << std::setprecision(0) << cmp.spsc_stats.mean_e2e_p99_ns
                  << std::setw(14) << static_cast<uint64_t>(cmp.spsc_stats.mean_push_retries)
                  << std::setw(12) << sp_ss.str()
                  << "\n";

        std::cout << std::string(140, '-') << "\n";
    }
    std::cout << std::string(140, '=') << "\n\n";
}

// -----------------------------------------------------------------------------
// Main CLI Driver
// -----------------------------------------------------------------------------

int main(int argc, char** argv) {
    size_t workload_size = 1000000; // 1M messages
    size_t repetitions = 5;
    std::string csv_path = "benchmark_results/concurrency_benchmark.csv";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--size" || arg == "--messages") && i + 1 < argc) {
            workload_size = static_cast<size_t>(std::stoull(argv[++i]));
        } else if (arg == "--runs" && i + 1 < argc) {
            repetitions = static_cast<size_t>(std::stoull(argv[++i]));
        } else if (arg == "--csv" && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "KernelTick Phase 4: Concurrency Synchronization Benchmark\n"
                      << "Usage: " << argv[0] << " [options]\n\n"
                      << "Options:\n"
                      << "  --size, --messages <N>  Message count per run (default: 1000000)\n"
                      << "  --runs <R>              Number of independent repetitions (default: 5)\n"
                      << "  --csv <path>            CSV export file path (default: benchmark_results/concurrency_benchmark.csv)\n"
                      << "  --help, -h              Display this help message\n";
            return 0;
        }
    }

    printVirtualizationNotice();

    // Step 4: Correctness Verification before running benchmarks
    std::cout << ">>> Running Pre-Benchmark Correctness Verification Tests <<<\n";
    bool ok_spsc_1k = verifyQueueCorrectness<SPSCQueue<BenchmarkMessage, 1024>>("SPSCQueue", 1023);
    bool ok_mutex_1k = verifyQueueCorrectness<MutexQueue<BenchmarkMessage, 1024>>("MutexQueue", 1023);
    bool ok_spsc_64k = verifyQueueCorrectness<SPSCQueue<BenchmarkMessage, 65536>>("SPSCQueue", 65535);
    bool ok_mutex_64k = verifyQueueCorrectness<MutexQueue<BenchmarkMessage, 65536>>("MutexQueue", 65535);

    if (!ok_spsc_1k || !ok_mutex_1k || !ok_spsc_64k || !ok_mutex_64k) {
        std::cerr << "Fatal: Queue correctness verification failed! Halting benchmark.\n";
        return 1;
    }
    std::cout << "All correctness tests passed successfully.\n\n";

    std::cout << ">>> Running Concurrency Benchmarks: Workload = " << workload_size
              << " messages, Repetitions = " << repetitions << " <<<\n\n";

    std::vector<BenchmarkRunResult> all_runs;
    std::vector<AggregateRunStats> all_aggregates;

    std::vector<BenchmarkRunResult> runs_mutex_1k;
    std::vector<BenchmarkRunResult> runs_spsc_1k;
    std::vector<BenchmarkRunResult> runs_mutex_64k;
    std::vector<BenchmarkRunResult> runs_spsc_64k;

    for (size_t r = 1; r <= repetitions; ++r) {
        std::cout << "  [Run #" << r << "/" << repetitions << "] Executing concurrency suites...\n";

        // 1K Capacity runs
        auto res_mutex_1k = runQueueBenchmark<MutexQueue<BenchmarkMessage, 1024>>("mutex_queue_1k", workload_size, r);
        auto res_spsc_1k = runQueueBenchmark<SPSCQueue<BenchmarkMessage, 1024>>("spsc_queue_1k", workload_size, r);

        // 64K Capacity runs
        auto res_mutex_64k = runQueueBenchmark<MutexQueue<BenchmarkMessage, 65536>>("mutex_queue_64k", workload_size, r);
        auto res_spsc_64k = runQueueBenchmark<SPSCQueue<BenchmarkMessage, 65536>>("spsc_queue_64k", workload_size, r);

        runs_mutex_1k.push_back(res_mutex_1k);
        runs_spsc_1k.push_back(res_spsc_1k);
        runs_mutex_64k.push_back(res_mutex_64k);
        runs_spsc_64k.push_back(res_spsc_64k);

        all_runs.push_back(res_mutex_1k);
        all_runs.push_back(res_spsc_1k);
        all_runs.push_back(res_mutex_64k);
        all_runs.push_back(res_spsc_64k);
    }

    auto agg_mutex_1k = computeAggregateStats(runs_mutex_1k);
    auto agg_spsc_1k = computeAggregateStats(runs_spsc_1k);
    auto agg_mutex_64k = computeAggregateStats(runs_mutex_64k);
    auto agg_spsc_64k = computeAggregateStats(runs_spsc_64k);

    all_aggregates.push_back(agg_mutex_1k);
    all_aggregates.push_back(agg_spsc_1k);
    all_aggregates.push_back(agg_mutex_64k);
    all_aggregates.push_back(agg_spsc_64k);

    std::vector<QueueComparison> comparisons = {
        {"1K Capacity (1,024 slots)", agg_mutex_1k, agg_spsc_1k},
        {"64K Capacity (65,536 slots)", agg_mutex_64k, agg_spsc_64k}
    };

    printComparisonTable(comparisons);
    printResultTable(all_runs, all_aggregates);

    if (exportToCSV(csv_path, all_runs, all_aggregates)) {
        std::cout << "Concurrency benchmark results successfully exported to: " << csv_path << "\n";
    }

    return 0;
}
