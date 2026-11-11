#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <chrono>

namespace kerneltick::benchmark {

// High-resolution monotonic clock helper for nanosecond precision
[[nodiscard]] inline int64_t getSteadyClockNs() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Memory and compiler optimization barrier to prevent dead-code elimination
template <typename T>
inline void doNotOptimize(T& value) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : "+m,r"(value) : : "memory");
#else
    volatile char* p = reinterpret_cast<volatile char*>(&value);
    (void)p;
#endif
}

// CPU pause instruction for low-power backpressure spin loops without pipeline thrashing
inline void cpuPause() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    asm volatile("pause" ::: "memory");
#endif
}

// Format duration/skew into clean human-readable unit string with fixed bounds
inline std::string formatSkewDisplay(int64_t ns) {
    int64_t abs_ns = std::abs(ns);
    std::string sign = (ns < 0) ? "-" : "+";
    std::ostringstream ss;
    if (abs_ns >= 1'000'000'000) {
        ss << sign << std::fixed << std::setprecision(2) << (static_cast<double>(abs_ns) / 1e9) << "s";
    } else if (abs_ns >= 1'000'000) {
        ss << sign << std::fixed << std::setprecision(2) << (static_cast<double>(abs_ns) / 1e6) << "ms";
    } else if (abs_ns >= 1'000) {
        ss << sign << std::fixed << std::setprecision(1) << (static_cast<double>(abs_ns) / 1e3) << "us";
    } else {
        ss << sign << abs_ns << "ns";
    }
    return ss.str();
}

// Statistical distribution of latency samples within a single run
struct LatencyStats {
    size_t count = 0;
    double avg_ns = 0.0;
    int64_t min_ns = 0;
    int64_t max_ns = 0;
    double p50_ns = 0.0;
    double p95_ns = 0.0;
    double p99_ns = 0.0;
    double p99_9_ns = 0.0;
    double stddev_ns = 0.0;
};

// Calculate latency distribution using NIST Type 7 linear interpolation percentiles
inline LatencyStats calculateLatencyStats(std::vector<int64_t>& latencies_ns) {
    LatencyStats stats;
    stats.count = latencies_ns.size();
    if (latencies_ns.empty()) {
        return stats;
    }

    // Sort samples for order statistics and percentiles
    std::sort(latencies_ns.begin(), latencies_ns.end());

    stats.min_ns = latencies_ns.front();
    stats.max_ns = latencies_ns.back();

    // Arithmetic mean
    double sum = 0.0;
    for (int64_t lat : latencies_ns) {
        sum += static_cast<double>(lat);
    }
    stats.avg_ns = sum / static_cast<double>(latencies_ns.size());

    // Sample standard deviation (Bessel's correction for sample variance)
    double sum_sq_diff = 0.0;
    for (int64_t lat : latencies_ns) {
        double diff = static_cast<double>(lat) - stats.avg_ns;
        sum_sq_diff += diff * diff;
    }
    double divisor = (latencies_ns.size() > 1) ? static_cast<double>(latencies_ns.size() - 1) : 1.0;
    stats.stddev_ns = std::sqrt(sum_sq_diff / divisor);

    // NIST / Hyndman & Fan Type 7 Linear Rank Interpolation for percentiles:
    // rank = p * (N - 1), interpolated between lower and upper indices
    auto calc_percentile = [&](double p) -> double {
        if (latencies_ns.empty()) return 0.0;
        if (latencies_ns.size() == 1) return static_cast<double>(latencies_ns[0]);

        double rank = p * static_cast<double>(latencies_ns.size() - 1);
        size_t lower = static_cast<size_t>(rank);
        size_t upper = lower + 1;
        double weight = rank - static_cast<double>(lower);

        if (upper >= latencies_ns.size()) {
            return static_cast<double>(latencies_ns.back());
        }
        return static_cast<double>(latencies_ns[lower]) * (1.0 - weight) +
               static_cast<double>(latencies_ns[upper]) * weight;
    };

    stats.p50_ns = calc_percentile(0.50);
    stats.p95_ns = calc_percentile(0.95);
    stats.p99_ns = calc_percentile(0.99);
    stats.p99_9_ns = calc_percentile(0.999);

    return stats;
}

// Single run benchmark record
struct BenchmarkRunResult {
    std::string benchmark_name;
    std::string execution_mode = "default"; // "default" or "affinity"
    int producer_cpu = -1;                  // -1 if unpinned, >= 0 if pinned
    int consumer_cpu = -1;                  // -1 if unpinned, >= 0 if pinned
    std::string sched_policy = "SCHED_OTHER";
    int sched_priority = 0;
    bool memlock_enabled = false;
    int64_t minor_faults_before = 0;
    int64_t minor_faults_after = 0;
    int64_t delta_minor_faults = 0;
    int64_t major_faults_before = 0;
    int64_t major_faults_after = 0;
    int64_t delta_major_faults = 0;
    size_t queue_capacity = 0;
    size_t workload_size = 0;
    size_t run_index = 1;
    size_t operations = 0;

    // Measurement 1: Total pipeline elapsed time (earliest worker start to latest worker completion)
    double duration_ms = 0.0;
    double duration_sec = 0.0;
    double throughput_ops_sec = 0.0;

    // Measurement 2: Producer and consumer individual execution durations and rates
    double producer_duration_ms = 0.0;
    double consumer_duration_ms = 0.0;
    double producer_throughput_ops_sec = 0.0;
    double consumer_throughput_ops_sec = 0.0;

    // Measurement 3: Producer-consumer start-time skew
    int64_t start_skew_ns = 0;
    double skew_percent_of_duration = 0.0;
    bool has_skew_warning = false; // True if absolute start skew > 5% of pipeline elapsed time

    // Retries due to backpressure or starvation (polling loop iterations)
    uint64_t push_retries = 0;       // Failed push attempts due to full queue
    uint64_t empty_pop_retries = 0;  // Failed pop attempts due to empty queue

    // Distinct measurement: CPU Operation Execution Latency (ns)
    LatencyStats op_latency;

    // Distinct measurement: End-to-End Queue Transit/Dwell Latency (ns)
    LatencyStats e2e_latency;
    bool has_e2e_latency = false;

    std::string compiler_config;
};

// Summary aggregate statistics across multiple independent repetitions
struct AggregateRunStats {
    std::string benchmark_name;
    std::string execution_mode = "default";
    int producer_cpu = -1;
    int consumer_cpu = -1;
    std::string sched_policy = "SCHED_OTHER";
    int sched_priority = 0;
    bool memlock_enabled = false;
    int64_t total_delta_minor_faults = 0;
    int64_t total_delta_major_faults = 0;
    double mean_delta_minor_faults = 0.0;
    double mean_delta_major_faults = 0.0;
    size_t queue_capacity = 0;
    size_t workload_size = 0;
    size_t total_runs = 0;

    // Pipeline duration across runs
    double mean_duration_ms = 0.0;

    // Throughput across runs
    double median_throughput_ops_sec = 0.0;
    double mean_throughput_ops_sec = 0.0;
    double min_throughput_ops_sec = 0.0;
    double max_throughput_ops_sec = 0.0;
    double stddev_throughput_ops_sec = 0.0;

    // Pipeline execution times and throughputs across runs
    double mean_producer_duration_ms = 0.0;
    double mean_consumer_duration_ms = 0.0;
    double mean_producer_throughput_ops_sec = 0.0;
    double mean_consumer_throughput_ops_sec = 0.0;

    // Start skew across runs
    double mean_abs_start_skew_ns = 0.0;
    int64_t max_abs_start_skew_ns = 0;
    size_t runs_with_skew_warning = 0;

    // Retries across runs
    uint64_t total_push_retries = 0;
    uint64_t total_empty_pop_retries = 0;
    double mean_push_retries = 0.0;
    double mean_empty_pop_retries = 0.0;

    // Operation Latency across runs
    double mean_op_avg_ns = 0.0;
    double mean_op_p50_ns = 0.0;
    double mean_op_p95_ns = 0.0;
    double mean_op_p99_ns = 0.0;
    double mean_op_p99_9_ns = 0.0;
    int64_t overall_op_min_ns = 0;
    int64_t overall_op_max_ns = 0;
    double mean_op_stddev_ns = 0.0;

    // End-to-End Latency across runs (if applicable)
    bool has_e2e_latency = false;
    double mean_e2e_avg_ns = 0.0;
    double mean_e2e_p50_ns = 0.0;
    double mean_e2e_p95_ns = 0.0;
    double mean_e2e_p99_ns = 0.0;
    double mean_e2e_p99_9_ns = 0.0;
    int64_t overall_e2e_min_ns = 0;
    int64_t overall_e2e_max_ns = 0;
    double mean_e2e_stddev_ns = 0.0;

    std::string compiler_config;
};

// Helper to determine compiler information
inline std::string getCompilerConfig() {
#if defined(__GNUC__)
    return "GCC " + std::to_string(__GNUC__) + "." +
           std::to_string(__GNUC_MINOR__) + "." +
           std::to_string(__GNUC_PATCHLEVEL__) + " -O3 -march=native C++20";
#elif defined(__clang__)
    return "Clang " + std::string(__clang_version__) + " -O3 -march=native C++20";
#else
    return "Standard C++20 Release";
#endif
}

// Compute aggregate statistics across R repetitions of a benchmark
inline AggregateRunStats computeAggregateStats(const std::vector<BenchmarkRunResult>& runs) {
    AggregateRunStats agg;
    if (runs.empty()) return agg;

    agg.benchmark_name = runs[0].benchmark_name;
    agg.execution_mode = runs[0].execution_mode;
    agg.producer_cpu = runs[0].producer_cpu;
    agg.consumer_cpu = runs[0].consumer_cpu;
    agg.sched_policy = runs[0].sched_policy;
    agg.sched_priority = runs[0].sched_priority;
    agg.memlock_enabled = runs[0].memlock_enabled;
    int64_t total_min = 0;
    int64_t total_maj = 0;
    for (const auto& r : runs) {
        total_min += r.delta_minor_faults;
        total_maj += r.delta_major_faults;
    }
    agg.total_delta_minor_faults = total_min;
    agg.total_delta_major_faults = total_maj;
    agg.mean_delta_minor_faults = runs.empty() ? 0.0 : static_cast<double>(total_min) / static_cast<double>(runs.size());
    agg.mean_delta_major_faults = runs.empty() ? 0.0 : static_cast<double>(total_maj) / static_cast<double>(runs.size());
    agg.queue_capacity = runs[0].queue_capacity;
    agg.workload_size = runs[0].workload_size;
    agg.total_runs = runs.size();
    agg.has_e2e_latency = runs[0].has_e2e_latency;
    agg.compiler_config = runs[0].compiler_config;

    agg.min_throughput_ops_sec = runs[0].throughput_ops_sec;
    agg.max_throughput_ops_sec = runs[0].throughput_ops_sec;
    agg.overall_op_min_ns = runs[0].op_latency.min_ns;
    agg.overall_op_max_ns = runs[0].op_latency.max_ns;

    if (agg.has_e2e_latency) {
        agg.overall_e2e_min_ns = runs[0].e2e_latency.min_ns;
        agg.overall_e2e_max_ns = runs[0].e2e_latency.max_ns;
    }

    std::vector<double> thru_list;
    thru_list.reserve(runs.size());

    double sum_thru = 0.0;
    double sum_prod_dur = 0.0;
    double sum_cons_dur = 0.0;
    double sum_prod_thru = 0.0;
    double sum_cons_thru = 0.0;
    double sum_dur = 0.0;
    double sum_op_avg = 0.0;
    double sum_op_p50 = 0.0;
    double sum_op_p95 = 0.0;
    double sum_op_p99 = 0.0;
    double sum_op_p99_9 = 0.0;
    double sum_op_stddev = 0.0;

    double sum_e2e_avg = 0.0;
    double sum_e2e_p50 = 0.0;
    double sum_e2e_p95 = 0.0;
    double sum_e2e_p99 = 0.0;
    double sum_e2e_p99_9 = 0.0;
    double sum_e2e_stddev = 0.0;

    double sum_skew = 0.0;
    int64_t max_skew = 0;
    size_t warn_count = 0;

    uint64_t sum_push_retries = 0;
    uint64_t sum_pop_retries = 0;

    for (const auto& r : runs) {
        thru_list.push_back(r.throughput_ops_sec);
        sum_thru += r.throughput_ops_sec;
        sum_prod_dur += r.producer_duration_ms;
        sum_cons_dur += r.consumer_duration_ms;
        sum_prod_thru += r.producer_throughput_ops_sec;
        sum_cons_thru += r.consumer_throughput_ops_sec;
        sum_push_retries += r.push_retries;
        sum_pop_retries += r.empty_pop_retries;

        int64_t abs_skew = std::abs(r.start_skew_ns);
        sum_skew += static_cast<double>(abs_skew);
        max_skew = std::max(max_skew, abs_skew);
        if (r.has_skew_warning) {
            warn_count++;
        }

        agg.min_throughput_ops_sec = std::min(agg.min_throughput_ops_sec, r.throughput_ops_sec);
        agg.max_throughput_ops_sec = std::max(agg.max_throughput_ops_sec, r.throughput_ops_sec);
        sum_dur += r.duration_ms;

        sum_op_avg += r.op_latency.avg_ns;
        sum_op_p50 += r.op_latency.p50_ns;
        sum_op_p95 += r.op_latency.p95_ns;
        sum_op_p99 += r.op_latency.p99_ns;
        sum_op_p99_9 += r.op_latency.p99_9_ns;
        sum_op_stddev += r.op_latency.stddev_ns;
        agg.overall_op_min_ns = std::min(agg.overall_op_min_ns, r.op_latency.min_ns);
        agg.overall_op_max_ns = std::max(agg.overall_op_max_ns, r.op_latency.max_ns);

        if (agg.has_e2e_latency) {
            sum_e2e_avg += r.e2e_latency.avg_ns;
            sum_e2e_p50 += r.e2e_latency.p50_ns;
            sum_e2e_p95 += r.e2e_latency.p95_ns;
            sum_e2e_p99 += r.e2e_latency.p99_ns;
            sum_e2e_p99_9 += r.e2e_latency.p99_9_ns;
            sum_e2e_stddev += r.e2e_latency.stddev_ns;
            agg.overall_e2e_min_ns = std::min(agg.overall_e2e_min_ns, r.e2e_latency.min_ns);
            agg.overall_e2e_max_ns = std::max(agg.overall_e2e_max_ns, r.e2e_latency.max_ns);
        }
    }

    double n = static_cast<double>(runs.size());
    agg.mean_throughput_ops_sec = sum_thru / n;
    agg.mean_producer_duration_ms = sum_prod_dur / n;
    agg.mean_consumer_duration_ms = sum_cons_dur / n;
    agg.mean_producer_throughput_ops_sec = sum_prod_thru / n;
    agg.mean_consumer_throughput_ops_sec = sum_cons_thru / n;
    agg.total_push_retries = sum_push_retries;
    agg.total_empty_pop_retries = sum_pop_retries;
    agg.mean_push_retries = static_cast<double>(sum_push_retries) / n;
    agg.mean_empty_pop_retries = static_cast<double>(sum_pop_retries) / n;
    agg.mean_abs_start_skew_ns = sum_skew / n;
    agg.max_abs_start_skew_ns = max_skew;
    agg.runs_with_skew_warning = warn_count;

    agg.mean_duration_ms = sum_dur / n;

    agg.mean_op_avg_ns = sum_op_avg / n;
    agg.mean_op_p50_ns = sum_op_p50 / n;
    agg.mean_op_p95_ns = sum_op_p95 / n;
    agg.mean_op_p99_ns = sum_op_p99 / n;
    agg.mean_op_p99_9_ns = sum_op_p99_9 / n;
    agg.mean_op_stddev_ns = sum_op_stddev / n;

    if (agg.has_e2e_latency) {
        agg.mean_e2e_avg_ns = sum_e2e_avg / n;
        agg.mean_e2e_p50_ns = sum_e2e_p50 / n;
        agg.mean_e2e_p95_ns = sum_e2e_p95 / n;
        agg.mean_e2e_p99_ns = sum_e2e_p99 / n;
        agg.mean_e2e_p99_9_ns = sum_e2e_p99_9 / n;
        agg.mean_e2e_stddev_ns = sum_e2e_stddev / n;
    }

    // Median throughput across runs
    std::sort(thru_list.begin(), thru_list.end());
    size_t mid = thru_list.size() / 2;
    agg.median_throughput_ops_sec = (thru_list.size() % 2 == 0)
        ? (thru_list[mid - 1] + thru_list[mid]) / 2.0
        : thru_list[mid];

    // Unbiased sample standard deviation across runs (N - 1 divisor)
    double sum_sq_thru = 0.0;
    for (double t : thru_list) {
        double d = t - agg.mean_throughput_ops_sec;
        sum_sq_thru += d * d;
    }
    double dev_div = (runs.size() > 1) ? static_cast<double>(runs.size() - 1) : 1.0;
    agg.stddev_throughput_ops_sec = std::sqrt(sum_sq_thru / dev_div);

    return agg;
}

// Print formatted summary table of runs and aggregates to console
inline void printResultTable(
    const std::vector<BenchmarkRunResult>& individual_runs,
    const std::vector<AggregateRunStats>& aggregates) 
{
    std::cout << "\n" << std::string(142, '=') << "\n";
    std::cout << "INDIVIDUAL BENCHMARK RUNS (Operations, Pipeline Duration & Operation Latency)\n";
    std::cout << std::string(142, '-') << "\n";
    std::cout << std::left
              << std::setw(26) << "Benchmark"
              << std::setw(8)  << "Size"
              << std::setw(6)  << "Run"
              << std::setw(12) << "Throughput"
              << std::setw(13) << "Pipeline(ms)"
              << std::setw(12) << "StartSkew"
              << std::setw(10) << "OpAvg(ns)"
              << std::setw(9)  << "OpMin"
              << std::setw(9)  << "OpP50"
              << std::setw(9)  << "OpP95"
              << std::setw(9)  << "OpP99"
              << std::setw(16) << "Retries(Full/Empty)"
              << "\n";
    std::cout << std::string(142, '-') << "\n";

    for (const auto& r : individual_runs) {
        std::string retry_str = (r.push_retries > 0 || r.empty_pop_retries > 0)
            ? (std::to_string(r.push_retries) + "/" + std::to_string(r.empty_pop_retries))
            : "-";

        std::string skew_str = (r.has_e2e_latency) ? formatSkewDisplay(r.start_skew_ns) : "-";
        std::string dur_str = std::to_string(static_cast<int64_t>(r.duration_ms)) + " ms";
        if (r.duration_ms < 10.0) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2) << r.duration_ms << " ms";
            dur_str = ss.str();
        }

        std::cout << std::left
                  << std::setw(26) << r.benchmark_name
                  << std::setw(8)  << r.workload_size
                  << "#" << std::setw(5) << r.run_index
                  << std::setw(12) << std::fixed << std::setprecision(0) << r.throughput_ops_sec
                  << std::setw(13) << dur_str
                  << std::setw(12) << skew_str
                  << std::setw(10) << std::fixed << std::setprecision(1) << r.op_latency.avg_ns
                  << std::setw(9)  << r.op_latency.min_ns
                  << std::setw(9)  << std::fixed << std::setprecision(0) << r.op_latency.p50_ns
                  << std::setw(9)  << std::fixed << std::setprecision(0) << r.op_latency.p95_ns
                  << std::setw(9)  << std::fixed << std::setprecision(0) << r.op_latency.p99_ns
                  << std::setw(16) << retry_str
                  << "\n";
    }

    // Check if any benchmark reports End-to-End latency (Queue benchmarks)
    bool has_any_e2e = false;
    for (const auto& r : individual_runs) {
        if (r.has_e2e_latency) {
            has_any_e2e = true;
            break;
        }
    }

    if (has_any_e2e) {
        std::cout << "\n" << std::string(142, '-') << "\n";
        std::cout << "CONCURRENT QUEUE PIPELINE EXECUTION (Individual Thread Durations, Rates & Start Skew)\n";
        std::cout << std::string(142, '-') << "\n";
        std::cout << std::left
                  << std::setw(24) << "Benchmark"
                  << std::setw(5)  << "Run"
                  << std::setw(12) << "Prod(ms)"
                  << std::setw(14) << "Prod Thru"
                  << std::setw(12) << "Cons(ms)"
                  << std::setw(14) << "Cons Thru"
                  << std::setw(12) << "StartSkew"
                  << std::setw(10) << "Skew%"
                  << std::setw(13) << "PushFull"
                  << std::setw(13) << "PopEmpty"
                  << std::setw(13) << "Skew Warning"
                  << "\n";
        std::cout << std::string(142, '-') << "\n";

        for (const auto& r : individual_runs) {
            if (r.has_e2e_latency) {
                std::string warn_str = r.has_skew_warning ? "WARN (>5%)" : "OK";
                std::cout << std::left
                          << std::setw(24) << r.benchmark_name
                          << "#" << std::setw(4) << r.run_index
                          << std::setw(12) << std::fixed << std::setprecision(2) << r.producer_duration_ms
                          << std::setw(14) << std::fixed << std::setprecision(0) << r.producer_throughput_ops_sec
                          << std::setw(12) << std::fixed << std::setprecision(2) << r.consumer_duration_ms
                          << std::setw(14) << std::fixed << std::setprecision(0) << r.consumer_throughput_ops_sec
                          << std::setw(12) << formatSkewDisplay(r.start_skew_ns)
                          << std::setw(10) << std::fixed << std::setprecision(1) << r.skew_percent_of_duration
                          << std::setw(13) << r.push_retries
                          << std::setw(13) << r.empty_pop_retries
                          << std::setw(13) << warn_str
                          << "\n";
            }
        }
    }

    if (!aggregates.empty()) {
        std::cout << "\n" << std::string(142, '=') << "\n";
        std::cout << "AGGREGATE SUMMARY ACROSS RUNS (Median, Mean +/- Sample StdDev, Min, Max)\n";
        std::cout << std::string(142, '-') << "\n";
        std::cout << std::left
                  << std::setw(26) << "Benchmark"
                  << std::setw(8)  << "Size"
                  << std::setw(5)  << "Runs"
                  << std::setw(13) << "Median Thru"
                  << std::setw(22) << "Mean Thru (ops/s)"
                  << std::setw(12) << "Min Thru"
                  << std::setw(12) << "Max Thru"
                  << std::setw(12) << "Mean Skew"
                  << std::setw(11) << "Mean OpP50"
                  << std::setw(12) << "Mean E2EP50"
                  << "\n";
        std::cout << std::string(142, '-') << "\n";

        for (const auto& a : aggregates) {
            std::string mean_str = std::to_string(static_cast<int64_t>(a.mean_throughput_ops_sec)) +
                                   " (+/-" + std::to_string(static_cast<int64_t>(a.stddev_throughput_ops_sec)) + ")";
            std::string e2e_p50_str = a.has_e2e_latency
                ? (std::to_string(static_cast<int64_t>(a.mean_e2e_p50_ns)) + " ns")
                : "N/A";
            std::string skew_str = a.has_e2e_latency
                ? formatSkewDisplay(static_cast<int64_t>(a.mean_abs_start_skew_ns))
                : "-";

            std::cout << std::left
                      << std::setw(26) << a.benchmark_name
                      << std::setw(8)  << a.workload_size
                      << std::setw(5)  << a.total_runs
                      << std::setw(13) << std::fixed << std::setprecision(0) << a.median_throughput_ops_sec
                      << std::setw(22) << mean_str
                      << std::setw(12) << std::fixed << std::setprecision(0) << a.min_throughput_ops_sec
                      << std::setw(12) << std::fixed << std::setprecision(0) << a.max_throughput_ops_sec
                      << std::setw(12) << skew_str
                      << std::setw(10) << std::fixed << std::setprecision(0) << a.mean_op_p50_ns << " ns"
                      << std::setw(12) << e2e_p50_str
                      << "\n";
        }
    }

    std::cout << std::string(142, '=') << "\n\n";
}

// Export comprehensive benchmark results to CSV file
inline bool exportToCSV(
    const std::string& filepath,
    const std::vector<BenchmarkRunResult>& individual_runs,
    const std::vector<AggregateRunStats>& aggregates) 
{
    std::filesystem::path p(filepath);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) {
            std::cerr << "Warning: Could not create directories for " << filepath << ": " << ec.message() << "\n";
        }
    }

    bool file_exists = std::filesystem::exists(p) && (std::filesystem::file_size(p) > 0);
    std::ofstream csv;
    if (file_exists) {
        csv.open(filepath, std::ios::app);
    } else {
        csv.open(filepath);
        if (!csv.is_open()) {
            std::cerr << "Error: Could not open CSV file for writing: " << filepath << "\n";
            return false;
        }

        // Comprehensive CSV Header
        csv << "record_type,"
            << "benchmark_name,"
            << "execution_mode,"
            << "producer_cpu,"
            << "consumer_cpu,"
            << "scheduling_policy,"
            << "scheduling_priority,"
            << "memlock_enabled,"
            << "minor_faults_before,"
            << "minor_faults_after,"
            << "delta_minor_faults,"
            << "major_faults_before,"
            << "major_faults_after,"
            << "delta_major_faults,"
            << "queue_capacity,"
            << "workload_size,"
            << "run_index,"
            << "operations,"
            << "pipeline_duration_ms,"
            << "throughput_ops_sec,"
            << "producer_duration_ms,"
            << "producer_throughput_ops_sec,"
            << "consumer_duration_ms,"
            << "consumer_throughput_ops_sec,"
            << "start_skew_ns,"
            << "skew_percent_of_duration,"
            << "skew_warning,"
            << "push_retries,"
            << "empty_pop_retries,"
            << "op_avg_ns,"
            << "op_min_ns,"
            << "op_p50_ns,"
            << "op_p95_ns,"
            << "op_p99_ns,"
            << "op_p99_9_ns,"
            << "op_max_ns,"
            << "op_stddev_ns,"
            << "has_e2e_latency,"
            << "e2e_avg_ns,"
            << "e2e_min_ns,"
            << "e2e_p50_ns,"
            << "e2e_p95_ns,"
            << "e2e_p99_ns,"
            << "e2e_p99_9_ns,"
            << "e2e_max_ns,"
            << "e2e_stddev_ns,"
            << "compiler_config\n";
    }

    if (!csv.is_open()) {
        std::cerr << "Error: Could not open CSV file: " << filepath << "\n";
        return false;
    }

    // Individual run rows
    for (const auto& r : individual_runs) {
        csv << "RUN,"
            << r.benchmark_name << ","
            << r.execution_mode << ","
            << r.producer_cpu << ","
            << r.consumer_cpu << ","
            << r.sched_policy << ","
            << r.sched_priority << ","
            << (r.memlock_enabled ? 1 : 0) << ","
            << r.minor_faults_before << ","
            << r.minor_faults_after << ","
            << r.delta_minor_faults << ","
            << r.major_faults_before << ","
            << r.major_faults_after << ","
            << r.delta_major_faults << ","
            << r.queue_capacity << ","
            << r.workload_size << ","
            << r.run_index << ","
            << r.operations << ","
            << std::fixed << std::setprecision(3) << r.duration_ms << ","
            << std::fixed << std::setprecision(2) << r.throughput_ops_sec << ","
            << std::fixed << std::setprecision(3) << r.producer_duration_ms << ","
            << std::fixed << std::setprecision(2) << r.producer_throughput_ops_sec << ","
            << std::fixed << std::setprecision(3) << r.consumer_duration_ms << ","
            << std::fixed << std::setprecision(2) << r.consumer_throughput_ops_sec << ","
            << r.start_skew_ns << ","
            << std::fixed << std::setprecision(2) << r.skew_percent_of_duration << ","
            << (r.has_skew_warning ? 1 : 0) << ","
            << r.push_retries << ","
            << r.empty_pop_retries << ","
            << std::fixed << std::setprecision(2) << r.op_latency.avg_ns << ","
            << r.op_latency.min_ns << ","
            << std::fixed << std::setprecision(2) << r.op_latency.p50_ns << ","
            << std::fixed << std::setprecision(2) << r.op_latency.p95_ns << ","
            << std::fixed << std::setprecision(2) << r.op_latency.p99_ns << ","
            << std::fixed << std::setprecision(2) << r.op_latency.p99_9_ns << ","
            << r.op_latency.max_ns << ","
            << std::fixed << std::setprecision(2) << r.op_latency.stddev_ns << ","
            << (r.has_e2e_latency ? 1 : 0) << ","
            << (r.has_e2e_latency ? std::to_string(r.e2e_latency.avg_ns) : "0") << ","
            << (r.has_e2e_latency ? std::to_string(r.e2e_latency.min_ns) : "0") << ","
            << (r.has_e2e_latency ? std::to_string(r.e2e_latency.p50_ns) : "0") << ","
            << (r.has_e2e_latency ? std::to_string(r.e2e_latency.p95_ns) : "0") << ","
            << (r.has_e2e_latency ? std::to_string(r.e2e_latency.p99_ns) : "0") << ","
            << (r.has_e2e_latency ? std::to_string(r.e2e_latency.p99_9_ns) : "0") << ","
            << (r.has_e2e_latency ? std::to_string(r.e2e_latency.max_ns) : "0") << ","
            << (r.has_e2e_latency ? std::to_string(r.e2e_latency.stddev_ns) : "0") << ","
            << "\"" << r.compiler_config << "\"\n";
    }

    // Aggregate summary rows
    for (const auto& a : aggregates) {
        csv << "AGGREGATE,"
            << a.benchmark_name << ","
            << a.execution_mode << ","
            << a.producer_cpu << ","
            << a.consumer_cpu << ","
            << a.sched_policy << ","
            << a.sched_priority << ","
            << (a.memlock_enabled ? 1 : 0) << ","
            << "0,"
            << "0,"
            << a.total_delta_minor_faults << ","
            << "0,"
            << "0,"
            << a.total_delta_major_faults << ","
            << a.queue_capacity << ","
            << a.workload_size << ","
            << a.total_runs << ","
            << a.workload_size << ","
            << std::fixed << std::setprecision(3) << a.mean_duration_ms << ","
            << std::fixed << std::setprecision(2) << a.mean_throughput_ops_sec << ","
            << std::fixed << std::setprecision(3) << a.mean_producer_duration_ms << ","
            << std::fixed << std::setprecision(2) << a.mean_producer_throughput_ops_sec << ","
            << std::fixed << std::setprecision(3) << a.mean_consumer_duration_ms << ","
            << std::fixed << std::setprecision(2) << a.mean_consumer_throughput_ops_sec << ","
            << std::fixed << std::setprecision(1) << a.mean_abs_start_skew_ns << ","
            << "0.0,"
            << a.runs_with_skew_warning << ","
            << a.total_push_retries << ","
            << a.total_empty_pop_retries << ","
            << std::fixed << std::setprecision(2) << a.mean_op_avg_ns << ","
            << a.overall_op_min_ns << ","
            << std::fixed << std::setprecision(2) << a.mean_op_p50_ns << ","
            << std::fixed << std::setprecision(2) << a.mean_op_p95_ns << ","
            << std::fixed << std::setprecision(2) << a.mean_op_p99_ns << ","
            << std::fixed << std::setprecision(2) << a.mean_op_p99_9_ns << ","
            << a.overall_op_max_ns << ","
            << std::fixed << std::setprecision(2) << a.mean_op_stddev_ns << ","
            << (a.has_e2e_latency ? 1 : 0) << ","
            << (a.has_e2e_latency ? std::to_string(a.mean_e2e_avg_ns) : "0") << ","
            << (a.has_e2e_latency ? std::to_string(a.overall_e2e_min_ns) : "0") << ","
            << (a.has_e2e_latency ? std::to_string(a.mean_e2e_p50_ns) : "0") << ","
            << (a.has_e2e_latency ? std::to_string(a.mean_e2e_p95_ns) : "0") << ","
            << (a.has_e2e_latency ? std::to_string(a.mean_e2e_p99_ns) : "0") << ","
            << (a.has_e2e_latency ? std::to_string(a.mean_e2e_p99_9_ns) : "0") << ","
            << (a.has_e2e_latency ? std::to_string(a.overall_e2e_max_ns) : "0") << ","
            << (a.has_e2e_latency ? std::to_string(a.mean_e2e_stddev_ns) : "0") << ","
            << "\"" << a.compiler_config << "\"\n";
    }

    csv.close();
    return true;
}

// Print banner with virtualization notice
inline void printVirtualizationNotice() {
    std::cout << "[KernelTick Benchmark Methodology Notice]\n"
              << "  Environment: VirtualBox VM (Linux / x86_64, 2 vCPUs)\n"
              << "  Synchronization: C++20 std::barrier (start skew measured & reported)\n"
              << "  Backpressure Tracking: Explicit counting of failed push (full) & empty pop attempts\n"
              << "  Metrics: Separating Operation Latency (CPU execution) from E2E Latency (Queue dwell)\n"
              << "  Duration: Measured between earliest thread start and latest thread completion (excluding joins)\n"
              << "  Timing: std::chrono::steady_clock (nanosecond resolution, monotonic)\n"
              << "  Percentiles: NIST / Type-7 linear rank interpolation\n"
              << "  Warm-up: Dynamic cache & branch predictor warm-up before timed sections\n"
              << "  Compiler Barrier: asm volatile memory barriers applied to prevent dead-code elimination\n\n";
}

} // namespace kerneltick::benchmark
