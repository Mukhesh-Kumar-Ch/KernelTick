#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cassert>
#include <cstring>
#include <iomanip>
#include <algorithm>
#include "../include/order_matching/Order.hpp"
#include "../include/utils/MemoryPool.hpp"
#include "benchmark_utils.hpp"

using namespace kerneltick::benchmark;

// Default pool size for memory benchmarks (1M orders capacity)
static constexpr size_t POOL_CAPACITY = 1024 * 1024;

// Warm-up helper to prime CPU caches, branch predictors, and heap metadata
static void warmupMemorySubsystem() {
    constexpr size_t WARMUP_COUNT = 2000;
    std::vector<Order*> warmup_ptrs(WARMUP_COUNT, nullptr);

    // Warm-up dynamic heap
    for (size_t i = 0; i < WARMUP_COUNT; ++i) {
        warmup_ptrs[i] = new Order(static_cast<OrderId>(i + 1), true, 100, 1000000, 0);
        doNotOptimize(warmup_ptrs[i]);
    }
    for (size_t i = 0; i < WARMUP_COUNT; ++i) {
        delete warmup_ptrs[i];
        warmup_ptrs[i] = nullptr;
    }

    // Warm-up preallocated memory pool
    OrderMemoryPool<WARMUP_COUNT * 2> warmup_pool;
    for (size_t i = 0; i < WARMUP_COUNT; ++i) {
        warmup_ptrs[i] = warmup_pool.allocate(static_cast<OrderId>(i + 1), true, 100, 1000000, 0);
        doNotOptimize(warmup_ptrs[i]);
    }
    for (size_t i = 0; i < WARMUP_COUNT; ++i) {
        warmup_pool.deallocate(warmup_ptrs[i]);
        warmup_ptrs[i] = nullptr;
    }
}

// -----------------------------------------------------------------------------
// Experiment 1: Pure Allocation Only (N allocations without deallocation in timed block)
// -----------------------------------------------------------------------------

BenchmarkRunResult runExp1DynamicAllocOnly(size_t N, size_t run_idx) {
    std::vector<Order*> ptrs(N, nullptr);
    std::vector<int64_t> latencies_ns(N, 0);

    const int64_t t_start = getSteadyClockNs();
    for (size_t i = 0; i < N; ++i) {
        const int64_t op_t0 = getSteadyClockNs();
        Order* ord = new Order();
        const int64_t op_t1 = getSteadyClockNs();
        doNotOptimize(ord);
        ptrs[i] = ord;
        latencies_ns[i] = op_t1 - op_t0;
    }
    const int64_t t_end = getSteadyClockNs();

    // Clean up heap memory outside timed section
    for (size_t i = 0; i < N; ++i) {
        delete ptrs[i];
    }

    const double duration_ms = static_cast<double>(t_end - t_start) / 1e6;
    const double duration_sec = static_cast<double>(t_end - t_start) / 1e9;

    BenchmarkRunResult res;
    res.benchmark_name = "exp1_alloc_dynamic";
    res.workload_size = N;
    res.run_index = run_idx;
    res.operations = N;
    res.duration_ms = duration_ms;
    res.duration_sec = duration_sec;
    res.throughput_ops_sec = (duration_sec > 0.0) ? (static_cast<double>(N) / duration_sec) : 0.0;
    res.op_latency = calculateLatencyStats(latencies_ns);
    res.compiler_config = getCompilerConfig();
    return res;
}

BenchmarkRunResult runExp1PoolAllocOnly(size_t N, size_t run_idx, OrderMemoryPool<POOL_CAPACITY>& pool) {
    pool.reset();
    std::vector<Order*> ptrs(N, nullptr);
    std::vector<int64_t> latencies_ns(N, 0);

    const int64_t t_start = getSteadyClockNs();
    for (size_t i = 0; i < N; ++i) {
        const int64_t op_t0 = getSteadyClockNs();
        Order* ord = pool.allocate();
        const int64_t op_t1 = getSteadyClockNs();
        doNotOptimize(ord);
        ptrs[i] = ord;
        latencies_ns[i] = op_t1 - op_t0;
    }
    const int64_t t_end = getSteadyClockNs();

    assert(pool.getFallbackCount() == 0);
    assert(pool.allocatedCount() == N);

    // Clean up pool orders outside timed section
    for (size_t i = 0; i < N; ++i) {
        pool.deallocate(ptrs[i]);
    }
    assert(pool.allocatedCount() == 0);

    const double duration_ms = static_cast<double>(t_end - t_start) / 1e6;
    const double duration_sec = static_cast<double>(t_end - t_start) / 1e9;

    BenchmarkRunResult res;
    res.benchmark_name = "exp1_alloc_pool";
    res.workload_size = N;
    res.run_index = run_idx;
    res.operations = N;
    res.duration_ms = duration_ms;
    res.duration_sec = duration_sec;
    res.throughput_ops_sec = (duration_sec > 0.0) ? (static_cast<double>(N) / duration_sec) : 0.0;
    res.op_latency = calculateLatencyStats(latencies_ns);
    res.compiler_config = getCompilerConfig();
    return res;
}

// -----------------------------------------------------------------------------
// Experiment 2: Allocation Followed by Batch Deallocation (Separate Latencies)
// -----------------------------------------------------------------------------

void runExp2DynamicAllocDealloc(
    size_t N, size_t run_idx,
    BenchmarkRunResult& out_alloc,
    BenchmarkRunResult& out_dealloc)
{
    std::vector<Order*> ptrs(N, nullptr);
    std::vector<int64_t> alloc_latencies(N, 0);
    std::vector<int64_t> dealloc_latencies(N, 0);

    // Phase A: Timed Allocation
    const int64_t alloc_start = getSteadyClockNs();
    for (size_t i = 0; i < N; ++i) {
        const int64_t op_t0 = getSteadyClockNs();
        Order* ord = new Order();
        const int64_t op_t1 = getSteadyClockNs();
        doNotOptimize(ord);
        ptrs[i] = ord;
        alloc_latencies[i] = op_t1 - op_t0;
    }
    const int64_t alloc_end = getSteadyClockNs();

    // Phase B: Timed Deallocation
    const int64_t dealloc_start = getSteadyClockNs();
    for (size_t i = 0; i < N; ++i) {
        const int64_t op_t0 = getSteadyClockNs();
        delete ptrs[i];
        const int64_t op_t1 = getSteadyClockNs();
        ptrs[i] = nullptr;
        dealloc_latencies[i] = op_t1 - op_t0;
    }
    const int64_t dealloc_end = getSteadyClockNs();

    const double alloc_ms = static_cast<double>(alloc_end - alloc_start) / 1e6;
    const double alloc_sec = static_cast<double>(alloc_end - alloc_start) / 1e9;
    const double dealloc_ms = static_cast<double>(dealloc_end - dealloc_start) / 1e6;
    const double dealloc_sec = static_cast<double>(dealloc_end - dealloc_start) / 1e9;

    out_alloc.benchmark_name = "exp2_alloc_dynamic";
    out_alloc.workload_size = N;
    out_alloc.run_index = run_idx;
    out_alloc.operations = N;
    out_alloc.duration_ms = alloc_ms;
    out_alloc.duration_sec = alloc_sec;
    out_alloc.throughput_ops_sec = (alloc_sec > 0.0) ? (static_cast<double>(N) / alloc_sec) : 0.0;
    out_alloc.op_latency = calculateLatencyStats(alloc_latencies);
    out_alloc.compiler_config = getCompilerConfig();

    out_dealloc.benchmark_name = "exp2_free_dynamic";
    out_dealloc.workload_size = N;
    out_dealloc.run_index = run_idx;
    out_dealloc.operations = N;
    out_dealloc.duration_ms = dealloc_ms;
    out_dealloc.duration_sec = dealloc_sec;
    out_dealloc.throughput_ops_sec = (dealloc_sec > 0.0) ? (static_cast<double>(N) / dealloc_sec) : 0.0;
    out_dealloc.op_latency = calculateLatencyStats(dealloc_latencies);
    out_dealloc.compiler_config = getCompilerConfig();
}

void runExp2PoolAllocDealloc(
    size_t N, size_t run_idx, OrderMemoryPool<POOL_CAPACITY>& pool,
    BenchmarkRunResult& out_alloc,
    BenchmarkRunResult& out_dealloc)
{
    pool.reset();
    std::vector<Order*> ptrs(N, nullptr);
    std::vector<int64_t> alloc_latencies(N, 0);
    std::vector<int64_t> dealloc_latencies(N, 0);

    // Phase A: Timed Allocation
    const int64_t alloc_start = getSteadyClockNs();
    for (size_t i = 0; i < N; ++i) {
        const int64_t op_t0 = getSteadyClockNs();
        Order* ord = pool.allocate();
        const int64_t op_t1 = getSteadyClockNs();
        doNotOptimize(ord);
        ptrs[i] = ord;
        alloc_latencies[i] = op_t1 - op_t0;
    }
    const int64_t alloc_end = getSteadyClockNs();

    assert(pool.getFallbackCount() == 0);
    assert(pool.allocatedCount() == N);

    // Phase B: Timed Deallocation
    const int64_t dealloc_start = getSteadyClockNs();
    for (size_t i = 0; i < N; ++i) {
        const int64_t op_t0 = getSteadyClockNs();
        pool.deallocate(ptrs[i]);
        const int64_t op_t1 = getSteadyClockNs();
        ptrs[i] = nullptr;
        dealloc_latencies[i] = op_t1 - op_t0;
    }
    const int64_t dealloc_end = getSteadyClockNs();

    assert(pool.allocatedCount() == 0);
    assert(pool.getFallbackCount() == 0);

    const double alloc_ms = static_cast<double>(alloc_end - alloc_start) / 1e6;
    const double alloc_sec = static_cast<double>(alloc_end - alloc_start) / 1e9;
    const double dealloc_ms = static_cast<double>(dealloc_end - dealloc_start) / 1e6;
    const double dealloc_sec = static_cast<double>(dealloc_end - dealloc_start) / 1e9;

    out_alloc.benchmark_name = "exp2_alloc_pool";
    out_alloc.workload_size = N;
    out_alloc.run_index = run_idx;
    out_alloc.operations = N;
    out_alloc.duration_ms = alloc_ms;
    out_alloc.duration_sec = alloc_sec;
    out_alloc.throughput_ops_sec = (alloc_sec > 0.0) ? (static_cast<double>(N) / alloc_sec) : 0.0;
    out_alloc.op_latency = calculateLatencyStats(alloc_latencies);
    out_alloc.compiler_config = getCompilerConfig();

    out_dealloc.benchmark_name = "exp2_free_pool";
    out_dealloc.workload_size = N;
    out_dealloc.run_index = run_idx;
    out_dealloc.operations = N;
    out_dealloc.duration_ms = dealloc_ms;
    out_dealloc.duration_sec = dealloc_sec;
    out_dealloc.throughput_ops_sec = (dealloc_sec > 0.0) ? (static_cast<double>(N) / dealloc_sec) : 0.0;
    out_dealloc.op_latency = calculateLatencyStats(dealloc_latencies);
    out_dealloc.compiler_config = getCompilerConfig();
}

// -----------------------------------------------------------------------------
// Experiment 3: Repeated Allocation and Reuse across Cycles
// -----------------------------------------------------------------------------

BenchmarkRunResult runExp3DynamicRepeatedReuse(size_t N, size_t run_idx) {
    constexpr size_t CYCLES = 10;
    const size_t batch_size = N / CYCLES;
    std::vector<Order*> batch(batch_size, nullptr);
    std::vector<int64_t> latencies_ns(N, 0);

    size_t sample_idx = 0;
    const int64_t t_start = getSteadyClockNs();

    for (size_t c = 0; c < CYCLES; ++c) {
        // Allocation phase of cycle
        for (size_t i = 0; i < batch_size; ++i) {
            const int64_t op_t0 = getSteadyClockNs();
            Order* ord = new Order();
            const int64_t op_t1 = getSteadyClockNs();
            ord->id = static_cast<OrderId>(0xAA000000 | (c * batch_size + i));
            ord->quantity = 999;
            doNotOptimize(ord);
            batch[i] = ord;
            latencies_ns[sample_idx++] = op_t1 - op_t0;
        }

        // Deallocation phase of cycle
        for (size_t i = 0; i < batch_size; ++i) {
            delete batch[i];
            batch[i] = nullptr;
        }
    }

    const int64_t t_end = getSteadyClockNs();
    const double duration_ms = static_cast<double>(t_end - t_start) / 1e6;
    const double duration_sec = static_cast<double>(t_end - t_start) / 1e9;

    BenchmarkRunResult res;
    res.benchmark_name = "exp3_reuse_dynamic";
    res.workload_size = N;
    res.run_index = run_idx;
    res.operations = N;
    res.duration_ms = duration_ms;
    res.duration_sec = duration_sec;
    res.throughput_ops_sec = (duration_sec > 0.0) ? (static_cast<double>(N) / duration_sec) : 0.0;
    res.op_latency = calculateLatencyStats(latencies_ns);
    res.compiler_config = getCompilerConfig();
    return res;
}

BenchmarkRunResult runExp3PoolRepeatedReuse(size_t N, size_t run_idx, OrderMemoryPool<POOL_CAPACITY>& pool) {
    pool.reset();
    constexpr size_t CYCLES = 10;
    const size_t batch_size = N / CYCLES;
    std::vector<Order*> batch(batch_size, nullptr);
    std::vector<int64_t> latencies_ns(N, 0);

    size_t sample_idx = 0;
    const int64_t t_start = getSteadyClockNs();

    for (size_t c = 0; c < CYCLES; ++c) {
        // Allocation phase of cycle
        for (size_t i = 0; i < batch_size; ++i) {
            const int64_t op_t0 = getSteadyClockNs();
            Order* ord = pool.allocate();
            const int64_t op_t1 = getSteadyClockNs();

            // Correctness verification: recycled order must be cleanly initialized (no stale data)
            assert(ord->id == 0);
            assert(ord->quantity == 0);

            // Write test pattern into object
            ord->id = static_cast<OrderId>(0xAA000000 | (c * batch_size + i));
            ord->quantity = 999;
            doNotOptimize(ord);
            batch[i] = ord;
            latencies_ns[sample_idx++] = op_t1 - op_t0;
        }

        assert(pool.allocatedCount() == batch_size);
        assert(pool.getFallbackCount() == 0);

        // Deallocation phase of cycle
        for (size_t i = 0; i < batch_size; ++i) {
            pool.deallocate(batch[i]);
            batch[i] = nullptr;
        }

        assert(pool.allocatedCount() == 0);
    }

    const int64_t t_end = getSteadyClockNs();
    assert(pool.getFallbackCount() == 0);

    const double duration_ms = static_cast<double>(t_end - t_start) / 1e6;
    const double duration_sec = static_cast<double>(t_end - t_start) / 1e9;

    BenchmarkRunResult res;
    res.benchmark_name = "exp3_reuse_pool";
    res.workload_size = N;
    res.run_index = run_idx;
    res.operations = N;
    res.duration_ms = duration_ms;
    res.duration_sec = duration_sec;
    res.throughput_ops_sec = (duration_sec > 0.0) ? (static_cast<double>(N) / duration_sec) : 0.0;
    res.op_latency = calculateLatencyStats(latencies_ns);
    res.compiler_config = getCompilerConfig();
    return res;
}

// -----------------------------------------------------------------------------
// Experiment 4: Sustained Workload (Sliding Window FIFO Pipeline Churn)
// -----------------------------------------------------------------------------

BenchmarkRunResult runExp4DynamicSustainedChurn(size_t N, size_t run_idx) {
    constexpr size_t WINDOW_SIZE = 1000;
    std::vector<Order*> window(WINDOW_SIZE, nullptr);
    std::vector<int64_t> latencies_ns(N, 0);

    const int64_t t_start = getSteadyClockNs();
    for (size_t i = 0; i < N; ++i) {
        const int64_t op_t0 = getSteadyClockNs();
        Order* ord = new Order(static_cast<OrderId>(i + 1), (i % 2 == 0), 100, 1000000, op_t0);
        const size_t slot = i % WINDOW_SIZE;
        if (i >= WINDOW_SIZE) {
            Order* old_ord = window[slot];
            delete old_ord;
        }
        window[slot] = ord;
        const int64_t op_t1 = getSteadyClockNs();
        doNotOptimize(ord);
        latencies_ns[i] = op_t1 - op_t0;
    }
    const int64_t t_end = getSteadyClockNs();

    // Drain remaining orders in window outside timed section
    const size_t active_orders = std::min(N, WINDOW_SIZE);
    for (size_t i = 0; i < active_orders; ++i) {
        delete window[i];
        window[i] = nullptr;
    }

    const double duration_ms = static_cast<double>(t_end - t_start) / 1e6;
    const double duration_sec = static_cast<double>(t_end - t_start) / 1e9;

    BenchmarkRunResult res;
    res.benchmark_name = "exp4_churn_dynamic";
    res.workload_size = N;
    res.run_index = run_idx;
    res.operations = N;
    res.duration_ms = duration_ms;
    res.duration_sec = duration_sec;
    res.throughput_ops_sec = (duration_sec > 0.0) ? (static_cast<double>(N) / duration_sec) : 0.0;
    res.op_latency = calculateLatencyStats(latencies_ns);
    res.compiler_config = getCompilerConfig();
    return res;
}

BenchmarkRunResult runExp4PoolSustainedChurn(size_t N, size_t run_idx, OrderMemoryPool<POOL_CAPACITY>& pool) {
    pool.reset();
    constexpr size_t WINDOW_SIZE = 1000;
    std::vector<Order*> window(WINDOW_SIZE, nullptr);
    std::vector<int64_t> latencies_ns(N, 0);

    const int64_t t_start = getSteadyClockNs();
    for (size_t i = 0; i < N; ++i) {
        const int64_t op_t0 = getSteadyClockNs();
        Order* ord = pool.allocate(static_cast<OrderId>(i + 1), (i % 2 == 0), 100, 1000000, op_t0);
        const size_t slot = i % WINDOW_SIZE;
        if (i >= WINDOW_SIZE) {
            Order* old_ord = window[slot];
            pool.deallocate(old_ord);
        }
        window[slot] = ord;
        const int64_t op_t1 = getSteadyClockNs();
        doNotOptimize(ord);
        latencies_ns[i] = op_t1 - op_t0;
    }
    const int64_t t_end = getSteadyClockNs();

    assert(pool.getFallbackCount() == 0);

    // Drain remaining orders in window outside timed section
    const size_t active_orders = std::min(N, WINDOW_SIZE);
    for (size_t i = 0; i < active_orders; ++i) {
        pool.deallocate(window[i]);
        window[i] = nullptr;
    }

    assert(pool.allocatedCount() == 0);
    assert(pool.getFallbackCount() == 0);

    const double duration_ms = static_cast<double>(t_end - t_start) / 1e6;
    const double duration_sec = static_cast<double>(t_end - t_start) / 1e9;

    BenchmarkRunResult res;
    res.benchmark_name = "exp4_churn_pool";
    res.workload_size = N;
    res.run_index = run_idx;
    res.operations = N;
    res.duration_ms = duration_ms;
    res.duration_sec = duration_sec;
    res.throughput_ops_sec = (duration_sec > 0.0) ? (static_cast<double>(N) / duration_sec) : 0.0;
    res.op_latency = calculateLatencyStats(latencies_ns);
    res.compiler_config = getCompilerConfig();
    return res;
}

// -----------------------------------------------------------------------------
// Side-by-Side Comparison Reporter
// -----------------------------------------------------------------------------

struct StrategyComparison {
    std::string experiment_title;
    AggregateRunStats dynamic_stats;
    AggregateRunStats pool_stats;
};

static void printComparisonTable(const std::vector<StrategyComparison>& comparisons) {
    std::cout << "\n" << std::string(132, '=') << "\n";
    std::cout << "                 KernelTick Phase 3: Memory Allocation Strategy Comparison (Dynamic vs MemoryPool)\n";
    std::cout << std::string(132, '=') << "\n";
    std::cout << std::left
              << std::setw(30) << "Experiment"
              << std::setw(10) << "Strategy"
              << std::setw(18) << "Throughput (ops/s)"
              << std::setw(14) << "Mean (ns)"
              << std::setw(12) << "p50 (ns)"
              << std::setw(12) << "p95 (ns)"
              << std::setw(12) << "p99 (ns)"
              << std::setw(14) << "p99.9 (ns)"
              << std::setw(10) << "Speedup"
              << "\n";
    std::cout << std::string(132, '-') << "\n";

    for (const auto& cmp : comparisons) {
        const double dyn_thru = cmp.dynamic_stats.mean_throughput_ops_sec;
        const double pool_thru = cmp.pool_stats.mean_throughput_ops_sec;
        const double speedup = (dyn_thru > 0.0) ? (pool_thru / dyn_thru) : 1.0;

        std::ostringstream sp_ss;
        sp_ss << std::fixed << std::setprecision(2) << speedup << "x";

        // Row 1: Dynamic
        std::cout << std::left
                  << std::setw(30) << cmp.experiment_title
                  << std::setw(10) << "Dynamic"
                  << std::setw(18) << std::fixed << std::setprecision(0) << dyn_thru
                  << std::setw(14) << std::fixed << std::setprecision(1) << cmp.dynamic_stats.mean_op_avg_ns
                  << std::setw(12) << std::fixed << std::setprecision(0) << cmp.dynamic_stats.mean_op_p50_ns
                  << std::setw(12) << std::fixed << std::setprecision(0) << cmp.dynamic_stats.mean_op_p95_ns
                  << std::setw(12) << std::fixed << std::setprecision(0) << cmp.dynamic_stats.mean_op_p99_ns
                  << std::setw(14) << std::fixed << std::setprecision(0) << cmp.dynamic_stats.mean_op_p99_9_ns
                  << std::setw(10) << "baseline"
                  << "\n";

        // Row 2: Pool
        std::cout << std::left
                  << std::setw(30) << ""
                  << std::setw(10) << "Pool"
                  << std::setw(18) << std::fixed << std::setprecision(0) << pool_thru
                  << std::setw(14) << std::fixed << std::setprecision(1) << cmp.pool_stats.mean_op_avg_ns
                  << std::setw(12) << std::fixed << std::setprecision(0) << cmp.pool_stats.mean_op_p50_ns
                  << std::setw(12) << std::fixed << std::setprecision(0) << cmp.pool_stats.mean_op_p95_ns
                  << std::setw(12) << std::fixed << std::setprecision(0) << cmp.pool_stats.mean_op_p99_ns
                  << std::setw(14) << std::fixed << std::setprecision(0) << cmp.pool_stats.mean_op_p99_9_ns
                  << std::setw(10) << sp_ss.str()
                  << "\n";

        std::cout << std::string(132, '-') << "\n";
    }
    std::cout << std::string(132, '=') << "\n\n";
}

// -----------------------------------------------------------------------------
// Main CLI Driver
// -----------------------------------------------------------------------------

int main(int argc, char** argv) {
    size_t workload_size = 100000;
    size_t repetitions = 5;
    std::string csv_path = "benchmark_results/memory_benchmark.csv";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--size" && i + 1 < argc) {
            workload_size = static_cast<size_t>(std::stoull(argv[++i]));
        } else if (arg == "--runs" && i + 1 < argc) {
            repetitions = static_cast<size_t>(std::stoull(argv[++i]));
        } else if (arg == "--csv" && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "KernelTick Phase 3: Memory Optimization Benchmark\n"
                      << "Usage: " << argv[0] << " [options]\n\n"
                      << "Options:\n"
                      << "  --size <N>       Workload operation count per run (default: 100000)\n"
                      << "  --runs <R>       Number of independent repetitions (default: 5)\n"
                      << "  --csv <path>     CSV export file path (default: benchmark_results/memory_benchmark.csv)\n"
                      << "  --help, -h       Display this help message\n";
            return 0;
        }
    }

    if (workload_size > POOL_CAPACITY) {
        std::cerr << "Error: Workload size (" << workload_size 
                  << ") exceeds pool capacity (" << POOL_CAPACITY << ").\n";
        return 1;
    }

    printVirtualizationNotice();
    std::cout << ">>> Running KernelTick Memory Benchmarks: Workload = " << workload_size
              << " ops, Repetitions = " << repetitions << " <<<\n\n";

    warmupMemorySubsystem();

    OrderMemoryPool<POOL_CAPACITY> shared_pool;

    std::vector<BenchmarkRunResult> all_runs;
    std::vector<AggregateRunStats> all_aggregates;

    // Vectors to hold runs for aggregation
    std::vector<BenchmarkRunResult> runs_e1_dyn;
    std::vector<BenchmarkRunResult> runs_e1_pool;
    std::vector<BenchmarkRunResult> runs_e2_alloc_dyn;
    std::vector<BenchmarkRunResult> runs_e2_alloc_pool;
    std::vector<BenchmarkRunResult> runs_e2_free_dyn;
    std::vector<BenchmarkRunResult> runs_e2_free_pool;
    std::vector<BenchmarkRunResult> runs_e3_dyn;
    std::vector<BenchmarkRunResult> runs_e3_pool;
    std::vector<BenchmarkRunResult> runs_e4_dyn;
    std::vector<BenchmarkRunResult> runs_e4_pool;

    for (size_t r = 1; r <= repetitions; ++r) {
        std::cout << "  [Run #" << r << "/" << repetitions << "] Executing memory experiments...\n";

        // Exp 1: Pure Alloc Only
        auto e1_dyn = runExp1DynamicAllocOnly(workload_size, r);
        auto e1_pool = runExp1PoolAllocOnly(workload_size, r, shared_pool);

        // Exp 2: Batch Alloc & Batch Free
        BenchmarkRunResult e2_alloc_dyn, e2_free_dyn;
        BenchmarkRunResult e2_alloc_pool, e2_free_pool;
        runExp2DynamicAllocDealloc(workload_size, r, e2_alloc_dyn, e2_free_dyn);
        runExp2PoolAllocDealloc(workload_size, r, shared_pool, e2_alloc_pool, e2_free_pool);

        // Exp 3: Repeated Reuse
        auto e3_dyn = runExp3DynamicRepeatedReuse(workload_size, r);
        auto e3_pool = runExp3PoolRepeatedReuse(workload_size, r, shared_pool);

        // Exp 4: Sustained Window Churn
        auto e4_dyn = runExp4DynamicSustainedChurn(workload_size, r);
        auto e4_pool = runExp4PoolSustainedChurn(workload_size, r, shared_pool);

        runs_e1_dyn.push_back(e1_dyn);
        runs_e1_pool.push_back(e1_pool);
        runs_e2_alloc_dyn.push_back(e2_alloc_dyn);
        runs_e2_alloc_pool.push_back(e2_alloc_pool);
        runs_e2_free_dyn.push_back(e2_free_dyn);
        runs_e2_free_pool.push_back(e2_free_pool);
        runs_e3_dyn.push_back(e3_dyn);
        runs_e3_pool.push_back(e3_pool);
        runs_e4_dyn.push_back(e4_dyn);
        runs_e4_pool.push_back(e4_pool);

        all_runs.push_back(e1_dyn);
        all_runs.push_back(e1_pool);
        all_runs.push_back(e2_alloc_dyn);
        all_runs.push_back(e2_alloc_pool);
        all_runs.push_back(e2_free_dyn);
        all_runs.push_back(e2_free_pool);
        all_runs.push_back(e3_dyn);
        all_runs.push_back(e3_pool);
        all_runs.push_back(e4_dyn);
        all_runs.push_back(e4_pool);
    }

    // Compute aggregates
    auto agg_e1_dyn = computeAggregateStats(runs_e1_dyn);
    auto agg_e1_pool = computeAggregateStats(runs_e1_pool);
    auto agg_e2_alloc_dyn = computeAggregateStats(runs_e2_alloc_dyn);
    auto agg_e2_alloc_pool = computeAggregateStats(runs_e2_alloc_pool);
    auto agg_e2_free_dyn = computeAggregateStats(runs_e2_free_dyn);
    auto agg_e2_free_pool = computeAggregateStats(runs_e2_free_pool);
    auto agg_e3_dyn = computeAggregateStats(runs_e3_dyn);
    auto agg_e3_pool = computeAggregateStats(runs_e3_pool);
    auto agg_e4_dyn = computeAggregateStats(runs_e4_dyn);
    auto agg_e4_pool = computeAggregateStats(runs_e4_pool);

    all_aggregates.push_back(agg_e1_dyn);
    all_aggregates.push_back(agg_e1_pool);
    all_aggregates.push_back(agg_e2_alloc_dyn);
    all_aggregates.push_back(agg_e2_alloc_pool);
    all_aggregates.push_back(agg_e2_free_dyn);
    all_aggregates.push_back(agg_e2_free_pool);
    all_aggregates.push_back(agg_e3_dyn);
    all_aggregates.push_back(agg_e3_pool);
    all_aggregates.push_back(agg_e4_dyn);
    all_aggregates.push_back(agg_e4_pool);

    // Prepare side-by-side comparison
    std::vector<StrategyComparison> comparisons = {
        {"1. Pure Allocation Only", agg_e1_dyn, agg_e1_pool},
        {"2A. Batch Alloc Phase", agg_e2_alloc_dyn, agg_e2_alloc_pool},
        {"2B. Batch Dealloc Phase", agg_e2_free_dyn, agg_e2_free_pool},
        {"3. Repeated Alloc/Free Reuse", agg_e3_dyn, agg_e3_pool},
        {"4. Sustained Window Churn", agg_e4_dyn, agg_e4_pool}
    };

    printComparisonTable(comparisons);
    printResultTable(all_runs, all_aggregates);

    if (exportToCSV(csv_path, all_runs, all_aggregates)) {
        std::cout << "Benchmark results successfully exported to: " << csv_path << "\n";
    }

    return 0;
}
