#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cassert>
#include <cstring>
#include "../include/order_matching/OrderBook.hpp"
#include "../include/order_matching/Matcher.hpp"
#include "../include/order_matching/Order.hpp"
#include "../include/utils/AsyncLogger.hpp"
#include "benchmark_utils.hpp"

using namespace kerneltick::benchmark;

// Warm-up helper to prime CPU caches, branch predictors, and memory allocators
static void warmupOrderBook() {
    OrderBook book;
    Matcher matcher;
    constexpr size_t WARMUP_COUNT = 1000;
    std::vector<Order> warmup_orders;
    warmup_orders.reserve(WARMUP_COUNT * 2);

    Price p_buy = Order::doubleToPrice(100.00);
    Price p_sell = Order::doubleToPrice(100.00);

    for (size_t i = 0; i < WARMUP_COUNT; ++i) {
        OrderId id = static_cast<OrderId>(i + 1);
        warmup_orders.emplace_back(id, false, 100, p_sell, 0);
        bool ok = book.addOrder(&warmup_orders.back());
        doNotOptimize(ok);
    }
    for (size_t i = 0; i < WARMUP_COUNT; ++i) {
        OrderId id = static_cast<OrderId>(WARMUP_COUNT + i + 1);
        warmup_orders.emplace_back(id, true, 100, p_buy, 0);
        bool ok = book.addOrder(&warmup_orders.back());
        size_t trades = matcher.matchOrders(book);
        doNotOptimize(ok);
        doNotOptimize(trades);
    }
}

// 1. Benchmark: Pure Order Insertion (Resting Buys and Sells across 40 price levels)
BenchmarkRunResult runInsertionBenchmark(size_t N, size_t run_idx) {
    warmupOrderBook();

    OrderBook book;
    std::vector<Order> orders;
    orders.reserve(N);
    std::vector<int64_t> latencies_ns(N);

    // Pre-generate non-crossing resting orders (20 bid levels, 20 ask levels)
    for (size_t i = 0; i < N; ++i) {
        OrderId id = static_cast<OrderId>(i + 1);
        bool is_buy = (i % 2 == 0);
        Price price = is_buy 
            ? Order::doubleToPrice(99.00 - static_cast<double>(i % 20) * 0.05)
            : Order::doubleToPrice(101.00 + static_cast<double>(i % 20) * 0.05);
        Quantity qty = static_cast<Quantity>(50 + (i % 10) * 10);
        orders.emplace_back(id, is_buy, qty, price, 0);
    }

    // Critical timed section: strictly measures addOrder execution latency
    int64_t t_start = getSteadyClockNs();
    for (size_t i = 0; i < N; ++i) {
        int64_t op_t0 = getSteadyClockNs();
        bool ok = book.addOrder(&orders[i]);
        int64_t op_t1 = getSteadyClockNs();
        doNotOptimize(ok);
        latencies_ns[i] = op_t1 - op_t0;
    }
    int64_t t_end = getSteadyClockNs();

    assert(book.getOrder(1) != nullptr);
    assert(book.getOrder(orders.back().id) != nullptr);
    assert(book.getTotalBidQuantity() > 0);
    assert(book.getTotalAskQuantity() > 0);

    BenchmarkRunResult res;
    res.benchmark_name = "order_insertion_resting";
    res.workload_size = N;
    res.run_index = run_idx;
    res.operations = N;
    res.duration_sec = static_cast<double>(t_end - t_start) / 1e9;
    res.duration_ms = static_cast<double>(t_end - t_start) / 1e6;
    res.throughput_ops_sec = (res.duration_sec > 0.0) ? (static_cast<double>(N) / res.duration_sec) : 0.0;
    res.op_latency = calculateLatencyStats(latencies_ns);
    res.has_e2e_latency = false;
    res.compiler_config = getCompilerConfig();

    return res;
}

// 2. Benchmark: Order Cancellation
BenchmarkRunResult runCancellationBenchmark(size_t N, size_t run_idx) {
    warmupOrderBook();

    OrderBook book;
    std::vector<Order> orders;
    orders.reserve(N);
    std::vector<int64_t> latencies_ns(N);

    // Setup: Pre-generate and populate book outside timed section
    for (size_t i = 0; i < N; ++i) {
        OrderId id = static_cast<OrderId>(i + 1);
        bool is_buy = (i % 2 == 0);
        Price price = is_buy 
            ? Order::doubleToPrice(99.00 - static_cast<double>(i % 20) * 0.05)
            : Order::doubleToPrice(101.00 + static_cast<double>(i % 20) * 0.05);
        orders.emplace_back(id, is_buy, 100, price, 0);
        bool ok = book.addOrder(&orders.back());
        doNotOptimize(ok);
    }

    assert(book.getTotalBidQuantity() > 0);
    assert(book.getTotalAskQuantity() > 0);

    // Critical timed section: strictly measures cancelOrder execution latency
    int64_t t_start = getSteadyClockNs();
    for (size_t i = 0; i < N; ++i) {
        int64_t op_t0 = getSteadyClockNs();
        bool ok = book.cancelOrder(orders[i].id);
        int64_t op_t1 = getSteadyClockNs();
        doNotOptimize(ok);
        latencies_ns[i] = op_t1 - op_t0;
    }
    int64_t t_end = getSteadyClockNs();

    assert(book.getTotalBidQuantity() == 0);
    assert(book.getTotalAskQuantity() == 0);
    assert(book.getBestBid() == nullptr);
    assert(book.getBestAsk() == nullptr);

    BenchmarkRunResult res;
    res.benchmark_name = "order_cancellation";
    res.workload_size = N;
    res.run_index = run_idx;
    res.operations = N;
    res.duration_sec = static_cast<double>(t_end - t_start) / 1e9;
    res.duration_ms = static_cast<double>(t_end - t_start) / 1e6;
    res.throughput_ops_sec = (res.duration_sec > 0.0) ? (static_cast<double>(N) / res.duration_sec) : 0.0;
    res.op_latency = calculateLatencyStats(latencies_ns);
    res.has_e2e_latency = false;
    res.compiler_config = getCompilerConfig();

    return res;
}

// 3. Benchmark: Order Matching with Partial Fills
// Setup (resting depth population) is strictly separated from the timed matching loop.
BenchmarkRunResult runMatchingBenchmark(size_t num_matches, size_t run_idx) {
    warmupOrderBook();

    OrderBook book;
    Matcher matcher;

    // To exercise price-time matching and partial fills without teardown overhead inside the timed loop:
    // We populate resting sell orders at the match price before timing.
    // Each resting sell order is 100 units.
    // Each incoming crossing buy order is 150 units.
    // To satisfy num_matches crossing buy orders of 150 units each (total buy qty = num_matches * 150):
    // We pre-populate (num_matches * 150 / 100) = num_matches * 1.5 resting sell orders.
    size_t num_resting_sells = (num_matches * 3 + 1) / 2;
    std::vector<Order> resting_sells;
    resting_sells.reserve(num_resting_sells);

    std::vector<Order> crossing_buys;
    crossing_buys.reserve(num_matches);

    Price match_price = Order::doubleToPrice(100.00);

    for (size_t i = 0; i < num_resting_sells; ++i) {
        OrderId id = static_cast<OrderId>(i + 1);
        resting_sells.emplace_back(id, false, 100, match_price, static_cast<Timestamp>(i + 1));
    }

    for (size_t i = 0; i < num_matches; ++i) {
        OrderId id = static_cast<OrderId>(num_resting_sells + i + 1);
        crossing_buys.emplace_back(id, true, 150, match_price, static_cast<Timestamp>(num_resting_sells + i + 1));
    }

    // Setup phase (OUTSIDE timing): Populate resting depth
    for (size_t i = 0; i < num_resting_sells; ++i) {
        bool ok = book.addOrder(&resting_sells[i]);
        doNotOptimize(ok);
    }

    std::vector<int64_t> latencies_ns(num_matches);
    size_t total_trades = 0;

    // Timed section: ONLY measures crossing buy insertion + matching engine execution
    int64_t t_start = getSteadyClockNs();
    for (size_t i = 0; i < num_matches; ++i) {
        int64_t op_t0 = getSteadyClockNs();
        bool ok = book.addOrder(&crossing_buys[i]);
        size_t trades = matcher.matchOrders(book);
        int64_t op_t1 = getSteadyClockNs();

        doNotOptimize(ok);
        doNotOptimize(trades);

        latencies_ns[i] = op_t1 - op_t0;
        total_trades += trades;
    }
    int64_t t_end = getSteadyClockNs();

    assert(total_trades > 0);

    BenchmarkRunResult res;
    res.benchmark_name = "order_matching_partial_fills";
    res.workload_size = num_matches;
    res.run_index = run_idx;
    res.operations = num_matches;
    res.duration_sec = static_cast<double>(t_end - t_start) / 1e9;
    res.duration_ms = static_cast<double>(t_end - t_start) / 1e6;
    res.throughput_ops_sec = (res.duration_sec > 0.0) ? (static_cast<double>(num_matches) / res.duration_sec) : 0.0;
    res.op_latency = calculateLatencyStats(latencies_ns);
    res.has_e2e_latency = false;
    res.compiler_config = getCompilerConfig();

    return res;
}

// 4. Benchmark: Mixed Trading Workload (40% Buys, 40% Sells, 10% Crosses, 10% Cancels)
BenchmarkRunResult runMixedWorkloadBenchmark(size_t N, size_t run_idx) {
    warmupOrderBook();

    OrderBook book;
    Matcher matcher;

    enum class ActionType : uint8_t { AddBuy, AddSell, AddCrossing, Cancel };
    struct Action {
        ActionType type;
        Order order;
        OrderId cancel_target_id = 0;
    };

    std::vector<Action> actions;
    actions.reserve(N);
    std::vector<int64_t> latencies_ns(N);

    std::vector<OrderId> active_order_ids;
    active_order_ids.reserve(N);

    // Deterministic pre-generation of mixed market feed
    OrderId next_id = 1;
    for (size_t i = 0; i < N; ++i) {
        int r = static_cast<int>(i % 100);
        Action act;
        if (r < 40) {
            // Passive Limit Buy
            Price p = Order::doubleToPrice(99.00 - static_cast<double>(i % 20) * 0.05);
            act.type = ActionType::AddBuy;
            act.order = Order(next_id, true, 50, p, static_cast<Timestamp>(i));
            active_order_ids.push_back(next_id);
            next_id++;
        } else if (r < 80) {
            // Passive Limit Sell
            Price p = Order::doubleToPrice(101.00 + static_cast<double>(i % 20) * 0.05);
            act.type = ActionType::AddSell;
            act.order = Order(next_id, false, 50, p, static_cast<Timestamp>(i));
            active_order_ids.push_back(next_id);
            next_id++;
        } else if (r < 90 && !active_order_ids.empty()) {
            // Cancel an earlier active order
            act.type = ActionType::Cancel;
            size_t idx = (i * 7) % active_order_ids.size();
            act.cancel_target_id = active_order_ids[idx];
            active_order_ids.erase(active_order_ids.begin() + idx);
        } else {
            // Crossing aggressive order (triggers match)
            act.type = ActionType::AddCrossing;
            Price p = (i % 2 == 0) ? Order::doubleToPrice(102.00) : Order::doubleToPrice(98.00);
            bool is_buy = (i % 2 == 0);
            act.order = Order(next_id, is_buy, 25, p, static_cast<Timestamp>(i));
            next_id++;
        }
        actions.push_back(act);
    }

    // Critical timed section: each operation measured individually with memory barrier
    int64_t t_start = getSteadyClockNs();
    for (size_t i = 0; i < N; ++i) {
        int64_t op_t0 = getSteadyClockNs();
        switch (actions[i].type) {
            case ActionType::AddBuy:
            case ActionType::AddSell: {
                bool ok = book.addOrder(&actions[i].order);
                doNotOptimize(ok);
                break;
            }
            case ActionType::AddCrossing: {
                bool ok = book.addOrder(&actions[i].order);
                size_t trades = matcher.matchOrders(book);
                doNotOptimize(ok);
                doNotOptimize(trades);
                break;
            }
            case ActionType::Cancel: {
                bool ok = book.cancelOrder(actions[i].cancel_target_id);
                doNotOptimize(ok);
                break;
            }
        }
        int64_t op_t1 = getSteadyClockNs();
        latencies_ns[i] = op_t1 - op_t0;
    }
    int64_t t_end = getSteadyClockNs();

    BenchmarkRunResult res;
    res.benchmark_name = "order_book_mixed_workload";
    res.workload_size = N;
    res.run_index = run_idx;
    res.operations = N;
    res.duration_sec = static_cast<double>(t_end - t_start) / 1e9;
    res.duration_ms = static_cast<double>(t_end - t_start) / 1e6;
    res.throughput_ops_sec = (res.duration_sec > 0.0) ? (static_cast<double>(N) / res.duration_sec) : 0.0;
    res.op_latency = calculateLatencyStats(latencies_ns);
    res.has_e2e_latency = false;
    res.compiler_config = getCompilerConfig();

    return res;
}

int main(int argc, char* argv[]) {
    // Disable noisy hot-path logging during performance benchmarking
    hft::getLogger().setLevel(hft::LogLevel::WARN);

    size_t workload_size = 10000; // Safe default for VirtualBox validation
    size_t repetitions = 3;       // Default: 3 independent repetitions for statistical stability
    std::string csv_path = "benchmark_results/order_book_benchmark.csv";
    bool run_all_sizes = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--size" && i + 1 < argc) {
            workload_size = std::stoull(argv[++i]);
        } else if (arg == "--runs" && i + 1 < argc) {
            repetitions = std::stoull(argv[++i]);
        } else if (arg == "--csv" && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (arg == "--all") {
            run_all_sizes = true;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "  --size <N>   Set workload operations count (default: 10000)\n"
                      << "  --runs <R>   Number of independent repetitions (default: 3)\n"
                      << "  --all        Run 10K, 100K, and 1M benchmarks sequentially\n"
                      << "  --csv <path> Custom CSV output destination\n"
                      << "  --help       Show help\n";
            return 0;
        }
    }

    printVirtualizationNotice();

    std::vector<size_t> sizes;
    if (run_all_sizes) {
        sizes = {10000, 100000, 1000000};
    } else {
        sizes = {workload_size};
    }

    std::vector<BenchmarkRunResult> all_runs;
    std::vector<AggregateRunStats> all_aggregates;

    for (size_t sz : sizes) {
        std::cout << ">>> Running OrderBook Benchmarks: Size = " << sz 
                  << " ops, Repetitions = " << repetitions << " <<<\n";

        // Group runs by benchmark type to compute aggregate stats
        std::vector<BenchmarkRunResult> insertion_runs;
        std::vector<BenchmarkRunResult> cancellation_runs;
        std::vector<BenchmarkRunResult> matching_runs;
        std::vector<BenchmarkRunResult> mixed_runs;

        for (size_t r = 1; r <= repetitions; ++r) {
            std::cout << "  [Run #" << r << "/" << repetitions << "] Executing test suite...\n";

            BenchmarkRunResult res_ins = runInsertionBenchmark(sz, r);
            BenchmarkRunResult res_can = runCancellationBenchmark(sz, r);
            BenchmarkRunResult res_mat = runMatchingBenchmark(sz, r);
            BenchmarkRunResult res_mix = runMixedWorkloadBenchmark(sz, r);

            insertion_runs.push_back(res_ins);
            cancellation_runs.push_back(res_can);
            matching_runs.push_back(res_mat);
            mixed_runs.push_back(res_mix);

            all_runs.push_back(res_ins);
            all_runs.push_back(res_can);
            all_runs.push_back(res_mat);
            all_runs.push_back(res_mix);
        }

        all_aggregates.push_back(computeAggregateStats(insertion_runs));
        all_aggregates.push_back(computeAggregateStats(cancellation_runs));
        all_aggregates.push_back(computeAggregateStats(matching_runs));
        all_aggregates.push_back(computeAggregateStats(mixed_runs));
    }

    printResultTable(all_runs, all_aggregates);

    if (exportToCSV(csv_path, all_runs, all_aggregates)) {
        std::cout << "Results exported successfully to: " << csv_path << "\n";
    }

    return 0;
}
