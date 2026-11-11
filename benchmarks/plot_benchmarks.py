#!/usr/bin/env python3
"""
KernelTick Benchmarks Visualization Generator
Generates academic-quality, publication-ready grayscale figures
from benchmark CSV datasets.

Figures generated:
  Figure 1: Order Book Baseline Performance (Throughput & Operation Latency)
  Figure 2: Memory Pool Optimization (Throughput & P99 Tail Latency)
  Figure 3: Queue Concurrency (MutexQueue vs. SPSCQueue Throughput, Op Latency, E2E Dwell)
  Figure 4: CPU Affinity (Throughput Asymmetry & Contention Push Retries)
  Figure 5: Scheduling Policies (SCHED_OTHER vs. SCHED_FIFO Throughput, Latency, Retries)
  Figure 6: Memory Locking & Page Fault Analysis (Throughput, Latency, Fault Counters)

Outputs:
  PNG (300 DPI, high-resolution) and SVG (vector format) for all figures.
"""

import argparse
import os
import sys
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def setup_matplotlib_style():
    """Configure matplotlib with clean grayscale academic publication styling."""
    plt.rcParams.update({
        'font.family': 'sans-serif',
        'font.sans-serif': ['DejaVu Sans', 'Arial', 'Helvetica'],
        'font.size': 10,
        'axes.titlesize': 11,
        'axes.titleweight': 'bold',
        'axes.labelsize': 10,
        'axes.labelweight': 'bold',
        'xtick.labelsize': 9,
        'ytick.labelsize': 9,
        'legend.fontsize': 9,
        'figure.titlesize': 13,
        'figure.titleweight': 'bold',
        'axes.edgecolor': '#222222',
        'axes.linewidth': 0.8,
        'axes.grid': True,
        'grid.color': '#dddddd',
        'grid.linestyle': '--',
        'grid.linewidth': 0.5,
        'grid.alpha': 0.8,
        'figure.dpi': 300,
        'savefig.dpi': 300,
        'savefig.bbox': 'tight',
    })


def save_figure(fig, output_dir, base_filename, dpi=300):
    """Save figure in both PNG and SVG formats."""
    png_path = os.path.join(output_dir, f"{base_filename}.png")
    svg_path = os.path.join(output_dir, f"{base_filename}.svg")

    fig.savefig(png_path, dpi=dpi, bbox_inches='tight')
    fig.savefig(svg_path, format='svg', bbox_inches='tight')
    plt.close(fig)
    print(f"  [OK] Saved {base_filename}.png (300 DPI) and {base_filename}.svg")
    return png_path, svg_path


# ==============================================================================
# Figure 1: Order Book Baseline Performance
# ==============================================================================
def plot_figure1_order_book(csv_path, output_dir, dpi=300):
    """
    Figure 1: Order Book Baseline Performance
    - Throughput across order book workloads.
    - Operation latency comparison (P50 and P99).
    """
    print("Generating Figure 1 — Order Book Baseline...")
    df = pd.read_csv(csv_path)

    workload_keys = [
        'order_insertion_resting',
        'order_cancellation',
        'order_matching_partial_fills',
        'order_book_mixed_workload'
    ]
    workload_labels = [
        'Resting\nInsertion',
        'Order\nCancellation',
        'Trade Matching\n(Partial Fills)',
        'Mixed Flow\n(40B/40S/10C/10X)'
    ]

    # Extract RUN records for statistics and points
    runs_df = df[df['record_type'] == 'RUN']
    agg_df = df[df['record_type'] == 'AGGREGATE']

    throughputs = []
    t_min = []
    t_max = []
    p50_latencies = []
    p99_latencies = []
    individual_runs_t = []

    for key in workload_keys:
        w_runs = runs_df[runs_df['benchmark_name'] == key]
        w_agg = agg_df[agg_df['benchmark_name'] == key]

        # Mean throughput in Million ops/sec
        mean_t = w_agg['throughput_ops_sec'].values[0] / 1e6
        throughputs.append(mean_t)

        # Run-level bounds
        t_vals = w_runs['throughput_ops_sec'].values / 1e6
        individual_runs_t.append(t_vals)
        t_min.append(mean_t - t_vals.min())
        t_max.append(t_vals.max() - mean_t)

        # Latencies in nanoseconds
        p50_latencies.append(w_agg['op_p50_ns'].values[0])
        p99_latencies.append(w_agg['op_p99_ns'].values[0])

    x = np.arange(len(workload_keys))

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5))
    fig.suptitle('Figure 1 — Limit Order Book Baseline Performance (Phase 2)', y=0.98)

    # Subplot A: Throughput
    bar_width = 0.55
    bars1 = ax1.bar(
        x, throughputs, width=bar_width,
        color='#c0c0c0', edgecolor='#111111', linewidth=0.9,
        yerr=[t_min, t_max], capsize=4, error_kw={'elinewidth': 1.0, 'ecolor': '#333333'},
        label='Mean Throughput (Error bars: Min–Max range)'
    )

    # Overlay individual run points
    for idx, r_vals in enumerate(individual_runs_t):
        ax1.scatter(
            [x[idx]] * len(r_vals), r_vals,
            color='#111111', edgecolor='#ffffff', s=35, zorder=4,
            label='Individual Run' if idx == 0 else ""
        )

    # Value annotations above bars
    for idx, (bar, val) in enumerate(zip(bars1, throughputs)):
        y_pos = val + t_max[idx] + 0.35
        ax1.text(
            bar.get_x() + bar.get_width() / 2, y_pos,
            f"{val:.2f} M", ha='center', va='bottom', fontsize=8.5, fontweight='bold'
        )

    ax1.set_title('(A) Throughput across Workloads', pad=10)
    ax1.set_ylabel('Throughput (Million ops/sec)')
    ax1.set_xticks(x)
    ax1.set_xticklabels(workload_labels)
    ax1.set_ylim(0, 16.5)
    ax1.legend(loc='upper right', framealpha=0.9)

    # Subplot B: Operation Latency (P50 vs P99)
    w = 0.32
    bars2_p50 = ax2.bar(
        x - w / 2, p50_latencies, width=w,
        color='#e8e8e8', edgecolor='#111111', linewidth=0.9, hatch='///',
        label='P50 Median Latency'
    )
    bars2_p99 = ax2.bar(
        x + w / 2, p99_latencies, width=w,
        color='#777777', edgecolor='#111111', linewidth=0.9, hatch='xxx',
        label='P99 Tail Latency'
    )

    # Annotations on bars
    for bar, val in zip(bars2_p50, p50_latencies):
        ax2.text(
            bar.get_x() + bar.get_width() / 2, val * 1.15,
            f"{val:.0f} ns", ha='center', va='bottom', fontsize=8, rotation=0
        )
    for bar, val in zip(bars2_p99, p99_latencies):
        ax2.text(
            bar.get_x() + bar.get_width() / 2, val * 1.15,
            f"{val:.0f} ns", ha='center', va='bottom', fontsize=8, rotation=0, fontweight='bold'
        )

    ax2.set_yscale('log')
    ax2.set_title('(B) Operation Latency Distribution (Log Scale)', pad=10)
    ax2.set_ylabel('Operation Latency (ns, log scale)')
    ax2.set_xticks(x)
    ax2.set_xticklabels(workload_labels)
    ax2.set_ylim(10, 10000)
    ax2.legend(loc='upper left', framealpha=0.9)

    plt.tight_layout()
    return save_figure(fig, output_dir, 'figure1_order_book_baseline', dpi)


# ==============================================================================
# Figure 2: Memory Pool Optimization
# ==============================================================================
def plot_figure2_memory_pool(csv_path, output_dir, dpi=300):
    """
    Figure 2: Memory Pool Optimization
    - Dynamic allocation vs. memory pool throughput.
    - P99 latency comparison.
    - Highlight Exp 4 (Churn) where dynamic allocation performed better in throughput.
    """
    print("Generating Figure 2 — Memory Pool Optimization...")
    df = pd.read_csv(csv_path)

    experiments = [
        ('Exp 1:\nPure Alloc', 'exp1_alloc_dynamic', 'exp1_alloc_pool'),
        ('Exp 2A:\nBatch Alloc', 'exp2_alloc_dynamic', 'exp2_alloc_pool'),
        ('Exp 2B:\nBatch Free', 'exp2_free_dynamic', 'exp2_free_pool'),
        ('Exp 3:\nReuse Cycles', 'exp3_reuse_dynamic', 'exp3_reuse_pool'),
        ('Exp 4:\nWindow Churn', 'exp4_churn_dynamic', 'exp4_churn_pool')
    ]

    runs_df = df[df['record_type'] == 'RUN']
    agg_df = df[df['record_type'] == 'AGGREGATE']

    dyn_t_means, pool_t_means = [], []
    dyn_t_stds, pool_t_stds = [], []
    dyn_p99, pool_p99 = [], []
    dyn_runs_t, pool_runs_t = [], []

    for _, dyn_key, pool_key in experiments:
        dyn_runs = runs_df[runs_df['benchmark_name'] == dyn_key]
        pool_runs = runs_df[runs_df['benchmark_name'] == pool_key]
        dyn_agg = agg_df[agg_df['benchmark_name'] == dyn_key]
        pool_agg = agg_df[agg_df['benchmark_name'] == pool_key]

        # Throughput in M ops/s
        dyn_t = dyn_agg['throughput_ops_sec'].values[0] / 1e6
        pool_t = pool_agg['throughput_ops_sec'].values[0] / 1e6
        dyn_t_means.append(dyn_t)
        pool_t_means.append(pool_t)

        # Standard deviations across the 5 runs
        dyn_t_stds.append(np.std(dyn_runs['throughput_ops_sec'].values / 1e6, ddof=1))
        pool_t_stds.append(np.std(pool_runs['throughput_ops_sec'].values / 1e6, ddof=1))

        dyn_runs_t.append(dyn_runs['throughput_ops_sec'].values / 1e6)
        pool_runs_t.append(pool_runs['throughput_ops_sec'].values / 1e6)

        # P99 latency in ns
        dyn_p99.append(dyn_agg['op_p99_ns'].values[0])
        pool_p99.append(pool_agg['op_p99_ns'].values[0])

    x = np.arange(len(experiments))
    w = 0.35

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5.5))
    fig.suptitle('Figure 2 — Memory Allocation Optimization: Dynamic Heap vs. OrderMemoryPool (Phase 3)', y=0.98)

    # Subplot A: Throughput
    bars_dyn_t = ax1.bar(
        x - w / 2, dyn_t_means, width=w,
        color='#777777', edgecolor='#111111', linewidth=0.9, hatch='///',
        yerr=dyn_t_stds, capsize=4, error_kw={'elinewidth': 1.0, 'ecolor': '#222222'},
        label='Dynamic Heap (new / delete)'
    )
    bars_pool_t = ax1.bar(
        x + w / 2, pool_t_means, width=w,
        color='#e8e8e8', edgecolor='#111111', linewidth=0.9, hatch='xxx',
        yerr=pool_t_stds, capsize=4, error_kw={'elinewidth': 1.0, 'ecolor': '#222222'},
        label='Preallocated MemoryPool'
    )

    # Overlay individual runs
    for i in range(len(experiments)):
        ax1.scatter([x[i] - w / 2] * len(dyn_runs_t[i]), dyn_runs_t[i], color='#222222', s=18, zorder=5)
        ax1.scatter([x[i] + w / 2] * len(pool_runs_t[i]), pool_runs_t[i], color='#555555', marker='^', s=18, zorder=5)

    # Annotations on Throughput
    ax1.annotate(
        '+77% Speedup',
        xy=(x[0] + w / 2, pool_t_means[0]), xytext=(x[0] - 0.2, 8.2),
        arrowprops=dict(facecolor='black', arrowstyle='->', lw=0.8),
        fontsize=8.5, fontweight='bold', ha='center'
    )
    ax1.annotate(
        'Dynamic +11%\n(tcache hypothesis)',
        xy=(x[4] - w / 2, dyn_t_means[4]), xytext=(x[4] - 0.3, 9.2),
        arrowprops=dict(facecolor='black', arrowstyle='->', lw=0.8),
        fontsize=8.5, fontweight='bold', ha='center',
        bbox=dict(boxstyle='round,pad=0.2', facecolor='#ffffff', edgecolor='#333333', lw=0.7)
    )

    ax1.set_title('(A) Throughput Comparison across Allocation Patterns', pad=10)
    ax1.set_ylabel('Mean Throughput (Million ops/sec)')
    ax1.set_xticks(x)
    ax1.set_xticklabels([e[0] for e in experiments])
    ax1.set_ylim(0, 10.5)
    ax1.legend(loc='upper left', framealpha=0.9)

    # Subplot B: P99 Tail Latency
    bars_dyn_lat = ax2.bar(
        x - w / 2, dyn_p99, width=w,
        color='#777777', edgecolor='#111111', linewidth=0.9, hatch='///',
        label='Dynamic Heap P99'
    )
    bars_pool_lat = ax2.bar(
        x + w / 2, pool_p99, width=w,
        color='#e8e8e8', edgecolor='#111111', linewidth=0.9, hatch='xxx',
        label='MemoryPool P99'
    )

    # Value annotations on latency
    for bar, val in zip(bars_dyn_lat, dyn_p99):
        ax2.text(
            bar.get_x() + bar.get_width() / 2, val * 1.15,
            f"{val:.0f}", ha='center', va='bottom', fontsize=8
        )
    for bar, val in zip(bars_pool_lat, pool_p99):
        ax2.text(
            bar.get_x() + bar.get_width() / 2, val * 1.15,
            f"{val:.0f}", ha='center', va='bottom', fontsize=8, fontweight='bold'
        )

    ax2.annotate(
        '42x Tail Latency Reduction\n(3,944 ns -> 93 ns)',
        xy=(x[0] + w / 2, pool_p99[0]), xytext=(x[0] + 0.3, 500),
        arrowprops=dict(facecolor='black', arrowstyle='->', lw=0.8),
        fontsize=8.5, fontweight='bold', ha='left'
    )
    ax2.annotate(
        'Tighter Tail in Churn\n(121 ns vs. 160 ns)',
        xy=(x[4] + w / 2, pool_p99[4]), xytext=(x[4] - 0.2, 500),
        arrowprops=dict(facecolor='black', arrowstyle='->', lw=0.8),
        fontsize=8.5, ha='center'
    )

    ax2.set_yscale('log')
    ax2.set_title('(B) P99 Tail Latency Comparison (Log Scale)', pad=10)
    ax2.set_ylabel('P99 Operation Latency (ns, log scale)')
    ax2.set_xticks(x)
    ax2.set_xticklabels([e[0] for e in experiments])
    ax2.set_ylim(10, 15000)
    ax2.legend(loc='upper right', framealpha=0.9)

    plt.tight_layout()
    return save_figure(fig, output_dir, 'figure2_memory_pool_optimization', dpi)


# ==============================================================================
# Figure 3: Queue Concurrency (MutexQueue vs. SPSCQueue)
# ==============================================================================
def plot_figure3_concurrency(csv_path, output_dir, dpi=300):
    """
    Figure 3: Queue Concurrency
    - MutexQueue vs. SPSCQueue throughput for 1K and 64K capacities.
    - Operation latency comparison.
    - End-to-end message dwell latency comparison.
    """
    print("Generating Figure 3 — Queue Concurrency...")
    df = pd.read_csv(csv_path)

    capacities = ['1K Slots', '64K Slots']
    pairs = [
        ('mutex_queue_1k', 'spsc_queue_1k'),
        ('mutex_queue_64k', 'spsc_queue_64k')
    ]

    runs_df = df[df['record_type'] == 'RUN']
    agg_df = df[df['record_type'] == 'AGGREGATE']

    mutex_t, spsc_t = [], []
    mutex_t_err, spsc_t_err = [], []
    mutex_runs_t, spsc_runs_t = [], []

    mutex_op_p50, mutex_op_p99 = [], []
    spsc_op_p50, spsc_op_p99 = [], []

    mutex_e2e_p50_us, mutex_e2e_p99_us = [], []
    spsc_e2e_p50_us, spsc_e2e_p99_us = [], []

    for m_key, s_key in pairs:
        m_runs = runs_df[runs_df['benchmark_name'] == m_key]
        s_runs = runs_df[runs_df['benchmark_name'] == s_key]
        m_agg = agg_df[agg_df['benchmark_name'] == m_key]
        s_agg = agg_df[agg_df['benchmark_name'] == s_key]

        m_t_val = m_agg['throughput_ops_sec'].values[0] / 1e6
        s_t_val = s_agg['throughput_ops_sec'].values[0] / 1e6
        mutex_t.append(m_t_val)
        spsc_t.append(s_t_val)

        m_runs_vals = m_runs['throughput_ops_sec'].values / 1e6
        s_runs_vals = s_runs['throughput_ops_sec'].values / 1e6
        mutex_runs_t.append(m_runs_vals)
        spsc_runs_t.append(s_runs_vals)

        mutex_t_err.append([m_t_val - m_runs_vals.min(), m_runs_vals.max() - m_t_val])
        spsc_t_err.append([s_t_val - s_runs_vals.min(), s_runs_vals.max() - s_t_val])

        # Operation latencies (ns)
        mutex_op_p50.append(m_agg['op_p50_ns'].values[0])
        mutex_op_p99.append(m_agg['op_p99_ns'].values[0])
        spsc_op_p50.append(s_agg['op_p50_ns'].values[0])
        spsc_op_p99.append(s_agg['op_p99_ns'].values[0])

        # End-to-end dwell latency in microseconds (ns / 1000)
        mutex_e2e_p50_us.append(m_agg['e2e_p50_ns'].values[0] / 1e3)
        mutex_e2e_p99_us.append(m_agg['e2e_p99_ns'].values[0] / 1e3)
        spsc_e2e_p50_us.append(s_agg['e2e_p50_ns'].values[0] / 1e3)
        spsc_e2e_p99_us.append(s_agg['e2e_p99_ns'].values[0] / 1e3)

    x = np.arange(len(capacities))
    w = 0.32

    fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(16, 5))
    fig.suptitle('Figure 3 — Inter-Thread Queue Concurrency: MutexQueue vs. SPSCQueue (Phase 4)', y=0.98)

    # Subplot A: Throughput
    m_err_trans = np.array(mutex_t_err).T
    s_err_trans = np.array(spsc_t_err).T

    bars1_m = ax1.bar(
        x - w / 2, mutex_t, width=w,
        color='#777777', edgecolor='#111111', linewidth=0.9, hatch='///',
        yerr=m_err_trans, capsize=4, error_kw={'elinewidth': 1.0, 'ecolor': '#222222'},
        label='MutexQueue (std::mutex)'
    )
    bars1_s = ax1.bar(
        x + w / 2, spsc_t, width=w,
        color='#e8e8e8', edgecolor='#111111', linewidth=0.9, hatch='xxx',
        yerr=s_err_trans, capsize=4, error_kw={'elinewidth': 1.0, 'ecolor': '#222222'},
        label='SPSCQueue (Lock-Free)'
    )

    # Overlay runs
    for i in range(len(capacities)):
        ax1.scatter([x[i] - w / 2] * len(mutex_runs_t[i]), mutex_runs_t[i], color='#222222', s=18, zorder=5)
        ax1.scatter([x[i] + w / 2] * len(spsc_runs_t[i]), spsc_runs_t[i], color='#555555', marker='^', s=18, zorder=5)

    # Callouts
    ax1.text(x[0] + w / 2, spsc_t[0] + 0.9, '+246.4%\n(3.46x)', ha='center', va='bottom', fontsize=8.5, fontweight='bold')
    ax1.text(x[1] + w / 2, spsc_t[1] + 0.9, '+173.8%\n(2.74x)', ha='center', va='bottom', fontsize=8.5, fontweight='bold')

    ax1.set_title('(A) Queue Throughput Comparison', pad=10)
    ax1.set_ylabel('Mean Throughput (Million ops/sec)')
    ax1.set_xticks(x)
    ax1.set_xticklabels(capacities)
    ax1.set_ylim(0, 13.5)
    ax1.legend(loc='upper left', framealpha=0.9)

    # Subplot B: Operation Latency (ns)
    w_sub = 0.20
    x_sub = np.array([0, 1])

    b2_m_p50 = ax2.bar(x_sub - 1.5 * w_sub, mutex_op_p50, width=w_sub, color='#999999', edgecolor='#111111', hatch='///', label='Mutex P50')
    b2_m_p99 = ax2.bar(x_sub - 0.5 * w_sub, mutex_op_p99, width=w_sub, color='#444444', edgecolor='#111111', hatch='///', label='Mutex P99')
    b2_s_p50 = ax2.bar(x_sub + 0.5 * w_sub, spsc_op_p50, width=w_sub, color='#f0f0f0', edgecolor='#111111', hatch='xxx', label='SPSC P50')
    b2_s_p99 = ax2.bar(x_sub + 1.5 * w_sub, spsc_op_p99, width=w_sub, color='#aaaaaa', edgecolor='#111111', hatch='xxx', label='SPSC P99')

    for bar, val in zip(b2_m_p99, mutex_op_p99):
        ax2.text(bar.get_x() + bar.get_width() / 2, val * 1.15, f"{val:.0f} ns", ha='center', va='bottom', fontsize=7.5, fontweight='bold')
    for bar, val in zip(b2_s_p99, spsc_op_p99):
        ax2.text(bar.get_x() + bar.get_width() / 2, val * 1.15, f"{val:.0f} ns", ha='center', va='bottom', fontsize=7.5, fontweight='bold')

    ax2.set_yscale('log')
    ax2.set_title('(B) Operation Latency (ns, Log Scale)', pad=10)
    ax2.set_ylabel('CPU Operation Latency (ns, log scale)')
    ax2.set_xticks(x_sub)
    ax2.set_xticklabels(capacities)
    ax2.set_ylim(10, 3000)
    ax2.legend(loc='upper right', framealpha=0.9, ncol=2, fontsize=8)

    # Subplot C: End-to-End Dwell Latency (µs)
    b3_m_p50 = ax3.bar(x_sub - 1.5 * w_sub, mutex_e2e_p50_us, width=w_sub, color='#999999', edgecolor='#111111', hatch='///', label='Mutex P50')
    b3_m_p99 = ax3.bar(x_sub - 0.5 * w_sub, mutex_e2e_p99_us, width=w_sub, color='#444444', edgecolor='#111111', hatch='///', label='Mutex P99')
    b3_s_p50 = ax3.bar(x_sub + 0.5 * w_sub, spsc_e2e_p50_us, width=w_sub, color='#f0f0f0', edgecolor='#111111', hatch='xxx', label='SPSC P50')
    b3_s_p99 = ax3.bar(x_sub + 1.5 * w_sub, spsc_e2e_p99_us, width=w_sub, color='#aaaaaa', edgecolor='#111111', hatch='xxx', label='SPSC P99')

    # Text callouts on Dwell latency
    ax3.text(x_sub[0] + w_sub, spsc_e2e_p99_us[0] * 1.3, 'P99 -90.6%\n(3.67 ms -> 344 µs)', ha='center', va='bottom', fontsize=7.5, fontweight='bold')
    ax3.text(x_sub[1] + w_sub, spsc_e2e_p99_us[1] * 1.3, 'P99 -90.8%\n(90.2 ms -> 8.3 ms)', ha='center', va='bottom', fontsize=7.5, fontweight='bold')

    ax3.set_yscale('log')
    ax3.set_title('(C) End-to-End Dwell Latency (µs, Log Scale)', pad=10)
    ax3.set_ylabel('In-Queue Dwell Latency (µs, log scale)')
    ax3.set_xticks(x_sub)
    ax3.set_xticklabels(capacities)
    ax3.set_ylim(10, 300000)
    ax3.legend(loc='upper left', framealpha=0.9, ncol=2, fontsize=8)

    plt.tight_layout()
    return save_figure(fig, output_dir, 'figure3_queue_concurrency', dpi)


# ==============================================================================
# Figure 4: CPU Affinity (Default vs. Pinned)
# ==============================================================================
def plot_figure4_cpu_affinity(csv_path, output_dir, dpi=300):
    """
    Figure 4: CPU Affinity
    - Default scheduling vs. pinned workers.
    - Separate 1K and 64K queue comparisons.
    - Clearly show that affinity helped 1K (+46%) and hurt 64K (-48%).
    """
    print("Generating Figure 4 — CPU Affinity...")
    df = pd.read_csv(csv_path)

    runs_df = df[df['record_type'] == 'RUN']
    agg_df = df[df['record_type'] == 'AGGREGATE']

    # 1K and 64K configurations
    configs = [
        ('1K Capacity', 'spsc_1k_default', 'spsc_1k_affinity'),
        ('64K Capacity', 'spsc_64k_default', 'spsc_64k_affinity')
    ]

    def_t, aff_t = [], []
    def_t_std, aff_t_std = [], []
    def_runs_t, aff_runs_t = [], []
    def_push_retries, aff_push_retries = [], []

    for _, def_key, aff_key in configs:
        d_runs = runs_df[runs_df['benchmark_name'] == def_key]
        a_runs = runs_df[runs_df['benchmark_name'] == aff_key]
        d_agg = agg_df[agg_df['benchmark_name'] == def_key]
        a_agg = agg_df[agg_df['benchmark_name'] == aff_key]

        d_t = d_agg['throughput_ops_sec'].values[0] / 1e6
        a_t = a_agg['throughput_ops_sec'].values[0] / 1e6
        def_t.append(d_t)
        aff_t.append(a_t)

        d_vals = d_runs['throughput_ops_sec'].values / 1e6
        a_vals = a_runs['throughput_ops_sec'].values / 1e6
        def_runs_t.append(d_vals)
        aff_runs_t.append(a_vals)

        def_t_std.append(np.std(d_vals, ddof=1))
        aff_t_std.append(np.std(a_vals, ddof=1))

        def_push_retries.append(d_agg['push_retries'].values[0])
        aff_push_retries.append(a_agg['push_retries'].values[0])

    x = np.arange(len(configs))
    w = 0.35

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5.5))
    fig.suptitle('Figure 4 — Linux CPU Core Affinity vs. Default CFS Scheduling (Phase 5.1)', y=0.98)

    # Subplot A: Throughput
    bars_def = ax1.bar(
        x - w / 2, def_t, width=w,
        color='#777777', edgecolor='#111111', linewidth=0.9, hatch='///',
        yerr=def_t_std, capsize=4, error_kw={'elinewidth': 1.0, 'ecolor': '#222222'},
        label='Default Linux Scheduling (Unpinned)'
    )
    bars_aff = ax1.bar(
        x + w / 2, aff_t, width=w,
        color='#e8e8e8', edgecolor='#111111', linewidth=0.9, hatch='xxx',
        yerr=aff_t_std, capsize=4, error_kw={'elinewidth': 1.0, 'ecolor': '#222222'},
        label='CPU Affinity (Pinned: Producer CPU 0, Consumer CPU 1)'
    )

    # Overlay runs
    for i in range(len(configs)):
        ax1.scatter([x[i] - w / 2] * len(def_runs_t[i]), def_runs_t[i], color='#222222', s=24, zorder=5)
        ax1.scatter([x[i] + w / 2] * len(aff_runs_t[i]), aff_runs_t[i], color='#444444', marker='^', s=24, zorder=5)

    # Annotate the dramatic workload-dependent delta
    ax1.annotate(
        '+46.0% Mean (+68% Median)\n[HELPED: Eliminates Co-Scheduling Freeze]',
        xy=(x[0] + w / 2, aff_t[0]), xytext=(x[0], 8.5),
        arrowprops=dict(facecolor='black', arrowstyle='->', lw=0.9),
        fontsize=8.5, fontweight='bold', ha='center',
        bbox=dict(boxstyle='round,pad=0.25', facecolor='#ffffff', edgecolor='#333333', lw=0.7)
    )
    ax1.annotate(
        '-47.8% Mean (-42% Median)\n[HURT: Rigid VM Pinning Penalty]',
        xy=(x[1] + w / 2, aff_t[1]), xytext=(x[1], 8.5),
        arrowprops=dict(facecolor='black', arrowstyle='->', lw=0.9),
        fontsize=8.5, fontweight='bold', ha='center',
        bbox=dict(boxstyle='round,pad=0.25', facecolor='#ffffff', edgecolor='#333333', lw=0.7)
    )

    # Annotation on the 5k outlier in 1K default
    ax1.annotate(
        'Run #1 Stall:\n5,166 ops/s\n(848M retries)',
        xy=(x[0] - w / 2, def_runs_t[0].min()), xytext=(x[0] - w / 2 - 0.28, 2.2),
        arrowprops=dict(facecolor='black', arrowstyle='->', lw=0.8),
        fontsize=7.5, ha='center'
    )

    ax1.set_title('(A) Throughput: Divergent Impact of Thread Pinning', pad=10)
    ax1.set_ylabel('Throughput (Million ops/sec)')
    ax1.set_xticks(x)
    ax1.set_xticklabels([c[0] for c in configs])
    ax1.set_ylim(0, 14.5)
    ax1.legend(loc='upper right', framealpha=0.9)

    # Subplot B: Push Contention Retries
    bars_ret_def = ax2.bar(
        x - w / 2, def_push_retries, width=w,
        color='#777777', edgecolor='#111111', linewidth=0.9, hatch='///',
        label='Default Scheduling Retries'
    )
    bars_ret_aff = ax2.bar(
        x + w / 2, aff_push_retries, width=w,
        color='#e8e8e8', edgecolor='#111111', linewidth=0.9, hatch='xxx',
        label='CPU Affinity Retries'
    )

    # Callouts on retries
    ax2.text(x[0] - w / 2, def_push_retries[0] * 1.3, '859.6 M\nRetries', ha='center', va='bottom', fontsize=8, fontweight='bold')
    ax2.text(x[0] + w / 2, aff_push_retries[0] * 1.3, '3.1 M\nRetries', ha='center', va='bottom', fontsize=8, fontweight='bold')

    ax2.annotate(
        '99.6% Reduction in Contention Retries\n(Co-scheduling spinlock collapse solved)',
        xy=(x[0] + w / 2, aff_push_retries[0]), xytext=(x[0] + 0.05, 5e4),
        arrowprops=dict(facecolor='black', arrowstyle='->', lw=0.9),
        fontsize=8.5, fontweight='bold', ha='left'
    )

    ax2.set_yscale('log')
    ax2.set_title('(B) Total Push Retries (Log Scale)', pad=10)
    ax2.set_ylabel('Total Push Retries (5 Runs, log scale)')
    ax2.set_xticks(x)
    ax2.set_xticklabels([c[0] for c in configs])
    ax2.set_ylim(1e4, 5e9)
    ax2.legend(loc='upper right', framealpha=0.9)

    plt.tight_layout()
    return save_figure(fig, output_dir, 'figure4_cpu_affinity', dpi)


# ==============================================================================
# Figure 5: Scheduling Policies (SCHED_OTHER vs. SCHED_FIFO)
# ==============================================================================
def plot_figure5_scheduling(csv_path, output_dir, dpi=300):
    """
    Figure 5: Scheduling Policies
    - SCHED_OTHER vs. genuine SCHED_FIFO priority 10.
    - Throughput and latency comparisons.
    - Throughput variability and empty pop retry reduction.
    """
    print("Generating Figure 5 — Scheduling Policies...")
    df = pd.read_csv(csv_path)

    runs_df = df[df['record_type'] == 'RUN']
    agg_df = df[df['record_type'] == 'AGGREGATE']

    configs = [
        ('64K Capacity', 'spsc_64k_sched_other', 'spsc_64k_sched_fifo'),
        ('1K Capacity', 'spsc_1k_sched_other', 'spsc_1k_sched_fifo')
    ]

    other_t, fifo_t = [], []
    other_t_std, fifo_t_std = [], []
    other_runs_t, fifo_runs_t = [], []

    other_p50, other_p99 = [], []
    fifo_p50, fifo_p99 = [], []

    other_empty_pop, fifo_empty_pop = [], []
    other_push_retries, fifo_push_retries = [], []

    for _, o_key, f_key in configs:
        o_runs = runs_df[runs_df['benchmark_name'] == o_key]
        f_runs = runs_df[runs_df['benchmark_name'] == f_key]
        o_agg = agg_df[agg_df['benchmark_name'] == o_key]
        f_agg = agg_df[agg_df['benchmark_name'] == f_key]

        o_t = o_agg['throughput_ops_sec'].values[0] / 1e6
        f_t = f_agg['throughput_ops_sec'].values[0] / 1e6
        other_t.append(o_t)
        fifo_t.append(f_t)

        o_vals = o_runs['throughput_ops_sec'].values / 1e6
        f_vals = f_runs['throughput_ops_sec'].values / 1e6
        other_runs_t.append(o_vals)
        fifo_runs_t.append(f_vals)

        other_t_std.append(np.std(o_vals, ddof=1))
        fifo_t_std.append(np.std(f_vals, ddof=1))

        other_p50.append(o_agg['op_p50_ns'].values[0])
        other_p99.append(o_agg['op_p99_ns'].values[0])
        fifo_p50.append(f_agg['op_p50_ns'].values[0])
        fifo_p99.append(f_agg['op_p99_ns'].values[0])

        other_empty_pop.append(o_agg['empty_pop_retries'].values[0])
        fifo_empty_pop.append(f_agg['empty_pop_retries'].values[0])
        other_push_retries.append(o_agg['push_retries'].values[0])
        fifo_push_retries.append(f_agg['push_retries'].values[0])

    x = np.arange(len(configs))
    w = 0.35

    fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(16, 5))
    fig.suptitle('Figure 5 — Linux Thread Scheduling Policies: SCHED_OTHER vs. SCHED_FIFO Priority 10 (Phase 5.2)', y=0.98)

    # Subplot A: Throughput & Variability
    bars_o_t = ax1.bar(
        x - w / 2, other_t, width=w,
        color='#777777', edgecolor='#111111', linewidth=0.9, hatch='///',
        yerr=other_t_std, capsize=4, error_kw={'elinewidth': 1.0, 'ecolor': '#222222'},
        label='SCHED_OTHER (prio 0)'
    )
    bars_f_t = ax1.bar(
        x + w / 2, fifo_t, width=w,
        color='#e8e8e8', edgecolor='#111111', linewidth=0.9, hatch='xxx',
        yerr=fifo_t_std, capsize=4, error_kw={'elinewidth': 1.0, 'ecolor': '#222222'},
        label='SCHED_FIFO (prio 10)'
    )

    # Overlay runs
    for i in range(len(configs)):
        ax1.scatter([x[i] - w / 2] * len(other_runs_t[i]), other_runs_t[i], color='#222222', s=22, zorder=5)
        ax1.scatter([x[i] + w / 2] * len(fifo_runs_t[i]), fifo_runs_t[i], color='#444444', marker='^', s=22, zorder=5)

    ax1.text(x[0] + w / 2, fifo_t[0] + 1.2, '+53.8% Throughput\n(StdDev halved -52.5%)', ha='center', va='bottom', fontsize=8, fontweight='bold')
    ax1.text(x[1] + w / 2, fifo_t[1] + 1.2, '+48.9% Throughput\n(StdDev -11.5%)', ha='center', va='bottom', fontsize=8, fontweight='bold')

    ax1.set_title('(A) Throughput and Variance Comparison', pad=10)
    ax1.set_ylabel('Throughput (Million ops/sec)')
    ax1.set_xticks(x)
    ax1.set_xticklabels([c[0] for c in configs])
    ax1.set_ylim(0, 15.0)
    ax1.legend(loc='upper left', framealpha=0.9)

    # Subplot B: Operation Latency (ns)
    w_sub = 0.20
    x_sub = np.array([0, 1])

    b_o_p50 = ax2.bar(x_sub - 1.5 * w_sub, other_p50, width=w_sub, color='#999999', edgecolor='#111111', hatch='///', label='OTHER P50')
    b_o_p99 = ax2.bar(x_sub - 0.5 * w_sub, other_p99, width=w_sub, color='#444444', edgecolor='#111111', hatch='///', label='OTHER P99')
    b_f_p50 = ax2.bar(x_sub + 0.5 * w_sub, fifo_p50, width=w_sub, color='#f0f0f0', edgecolor='#111111', hatch='xxx', label='FIFO P50')
    b_f_p99 = ax2.bar(x_sub + 1.5 * w_sub, fifo_p99, width=w_sub, color='#aaaaaa', edgecolor='#111111', hatch='xxx', label='FIFO P99')

    for bar, val in zip(b_o_p99, other_p99):
        ax2.text(bar.get_x() + bar.get_width() / 2, val + 3, f"{val:.0f}", ha='center', va='bottom', fontsize=7.5)
    for bar, val in zip(b_f_p99, fifo_p99):
        ax2.text(bar.get_x() + bar.get_width() / 2, val + 3, f"{val:.0f}", ha='center', va='bottom', fontsize=7.5, fontweight='bold')

    ax2.set_title('(B) Operation Latency (ns)', pad=10)
    ax2.set_ylabel('Operation Latency (ns)')
    ax2.set_xticks(x_sub)
    ax2.set_xticklabels([c[0] for c in configs])
    ax2.set_ylim(0, 210)
    ax2.legend(loc='upper right', framealpha=0.9, ncol=2, fontsize=8)

    # Subplot C: Starvation & Contention Retries (Empty Pop Retries)
    bars_o_ret = ax3.bar(
        x - w / 2, other_empty_pop, width=w,
        color='#777777', edgecolor='#111111', linewidth=0.9, hatch='///',
        label='SCHED_OTHER Starvation Retries'
    )
    bars_f_ret = ax3.bar(
        x + w / 2, fifo_empty_pop, width=w,
        color='#e8e8e8', edgecolor='#111111', linewidth=0.9, hatch='xxx',
        label='SCHED_FIFO Starvation Retries'
    )

    ax3.text(x[0] + w / 2, fifo_empty_pop[0] * 1.4, '-95.5%\n(161.6k -> 7.3k)', ha='center', va='bottom', fontsize=8, fontweight='bold')
    ax3.text(x[1] + w / 2, fifo_empty_pop[1] * 1.4, '-80.2%\n(365.8k -> 72.5k)', ha='center', va='bottom', fontsize=8, fontweight='bold')

    ax3.set_yscale('log')
    ax3.set_title('(C) Consumer Empty-Pop Starvation Retries', pad=10)
    ax3.set_ylabel('Empty Pop Retries (Total across 5 runs, log scale)')
    ax3.set_xticks(x)
    ax3.set_xticklabels([c[0] for c in configs])
    ax3.set_ylim(1e3, 1e6)
    ax3.legend(loc='upper right', framealpha=0.9)

    plt.tight_layout()
    return save_figure(fig, output_dir, 'figure5_scheduling_policies', dpi)


# ==============================================================================
# Figure 6: Memory Locking & Page Fault Analysis (mlockall)
# ==============================================================================
def plot_figure6_memory_locking(csv_path, output_dir, dpi=300):
    """
    Figure 6: Memory Locking & Page Fault Analysis
    - Normal memory vs. mlockall throughput.
    - P50 and P99 operation latency.
    - Minor and major page-fault comparison.
    """
    print("Generating Figure 6 — Memory Locking & Page Faults...")
    df = pd.read_csv(csv_path)

    runs_df = df[df['record_type'] == 'RUN']
    agg_df = df[df['record_type'] == 'AGGREGATE']

    configs = [
        ('64K Capacity', 'spsc_64k_normal', 'spsc_64k_locked'),
        ('1K Capacity', 'spsc_1k_normal', 'spsc_1k_locked')
    ]

    norm_t, lock_t = [], []
    norm_t_std, lock_t_std = [], []
    norm_runs_t, lock_runs_t = [], []

    norm_p50, norm_p99 = [], []
    lock_p50, lock_p99 = [], []

    min_flt_norm, min_flt_lock = [], []
    maj_flt_norm, maj_flt_lock = [], []

    for _, n_key, l_key in configs:
        n_runs = runs_df[runs_df['benchmark_name'] == n_key]
        l_runs = runs_df[runs_df['benchmark_name'] == l_key]
        n_agg = agg_df[agg_df['benchmark_name'] == n_key]
        l_agg = agg_df[agg_df['benchmark_name'] == l_key]

        n_t = n_agg['throughput_ops_sec'].values[0] / 1e6
        l_t = l_agg['throughput_ops_sec'].values[0] / 1e6
        norm_t.append(n_t)
        lock_t.append(l_t)

        n_vals = n_runs['throughput_ops_sec'].values / 1e6
        l_vals = l_runs['throughput_ops_sec'].values / 1e6
        norm_runs_t.append(n_vals)
        lock_runs_t.append(l_vals)

        norm_t_std.append(np.std(n_vals, ddof=1))
        lock_t_std.append(np.std(l_vals, ddof=1))

        norm_p50.append(n_agg['op_p50_ns'].values[0])
        norm_p99.append(n_agg['op_p99_ns'].values[0])
        lock_p50.append(l_agg['op_p50_ns'].values[0])
        lock_p99.append(l_agg['op_p99_ns'].values[0])

        # Cumulative delta minor and major faults
        min_flt_norm.append(n_runs['delta_minor_faults'].sum())
        min_flt_lock.append(l_runs['delta_minor_faults'].sum())
        maj_flt_norm.append(n_runs['delta_major_faults'].sum())
        maj_flt_lock.append(l_runs['delta_major_faults'].sum())

    x = np.arange(len(configs))
    w = 0.35

    fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(16, 5))
    fig.suptitle('Figure 6 — Virtual Memory Locking (mlockall) and Page-Fault Analysis (Phase 5.3)', y=0.98)

    # Subplot A: Throughput
    bars_n_t = ax1.bar(
        x - w / 2, norm_t, width=w,
        color='#777777', edgecolor='#111111', linewidth=0.9, hatch='///',
        yerr=norm_t_std, capsize=4, error_kw={'elinewidth': 1.0, 'ecolor': '#222222'},
        label='Normal Memory (Demand Paged)'
    )
    bars_l_t = ax1.bar(
        x + w / 2, lock_t, width=w,
        color='#e8e8e8', edgecolor='#111111', linewidth=0.9, hatch='xxx',
        yerr=lock_t_std, capsize=4, error_kw={'elinewidth': 1.0, 'ecolor': '#222222'},
        label='Locked Memory (mlockall)'
    )

    # Overlay runs
    for i in range(len(configs)):
        ax1.scatter([x[i] - w / 2] * len(norm_runs_t[i]), norm_runs_t[i], color='#222222', s=22, zorder=5)
        ax1.scatter([x[i] + w / 2] * len(lock_runs_t[i]), lock_runs_t[i], color='#444444', marker='^', s=22, zorder=5)

    ax1.text(x[0] + w / 2, lock_t[0] + 1.2, '+7.2% Throughput\n(Modest Gain)', ha='center', va='bottom', fontsize=8, fontweight='bold')
    ax1.text(x[1] + w / 2, lock_t[1] + 1.2, '-3.3% Throughput\n(Slight Degradation)', ha='center', va='bottom', fontsize=8, fontweight='bold')

    ax1.set_title('(A) Throughput: Workload-Dependent Impact', pad=10)
    ax1.set_ylabel('Throughput (Million ops/sec)')
    ax1.set_xticks(x)
    ax1.set_xticklabels([c[0] for c in configs])
    ax1.set_ylim(0, 13.0)
    ax1.legend(loc='upper left', framealpha=0.9)

    # Subplot B: Operation Latency (P50 vs P99)
    w_sub = 0.20
    x_sub = np.array([0, 1])

    b_n_p50 = ax2.bar(x_sub - 1.5 * w_sub, norm_p50, width=w_sub, color='#999999', edgecolor='#111111', hatch='///', label='Normal P50')
    b_n_p99 = ax2.bar(x_sub - 0.5 * w_sub, norm_p99, width=w_sub, color='#444444', edgecolor='#111111', hatch='///', label='Normal P99')
    b_l_p50 = ax2.bar(x_sub + 0.5 * w_sub, lock_p50, width=w_sub, color='#f0f0f0', edgecolor='#111111', hatch='xxx', label='Locked P50')
    b_l_p99 = ax2.bar(x_sub + 1.5 * w_sub, lock_p99, width=w_sub, color='#aaaaaa', edgecolor='#111111', hatch='xxx', label='Locked P99')

    # Latency callouts
    ax2.text(x_sub[0] - 1.5 * w_sub, norm_p50[0] + 3, f"{norm_p50[0]:.0f}", ha='center', va='bottom', fontsize=7.5)
    ax2.text(x_sub[0] + 0.5 * w_sub, lock_p50[0] + 3, f"{lock_p50[0]:.0f}", ha='center', va='bottom', fontsize=7.5, fontweight='bold')
    ax2.text(x_sub[0] - 0.5 * w_sub, norm_p99[0] + 3, f"{norm_p99[0]:.0f}", ha='center', va='bottom', fontsize=7.5)
    ax2.text(x_sub[0] + 1.5 * w_sub, lock_p99[0] + 3, f"{lock_p99[0]:.0f}", ha='center', va='bottom', fontsize=7.5, fontweight='bold')

    ax2.text(x_sub[1] - 1.5 * w_sub, norm_p50[1] + 3, f"{norm_p50[1]:.0f}", ha='center', va='bottom', fontsize=7.5)
    ax2.text(x_sub[1] + 0.5 * w_sub, lock_p50[1] + 3, f"{lock_p50[1]:.0f}", ha='center', va='bottom', fontsize=7.5, fontweight='bold')
    ax2.text(x_sub[1] - 0.5 * w_sub, norm_p99[1] + 3, f"{norm_p99[1]:.0f}", ha='center', va='bottom', fontsize=7.5)
    ax2.text(x_sub[1] + 1.5 * w_sub, lock_p99[1] + 3, f"{lock_p99[1]:.0f}", ha='center', va='bottom', fontsize=7.5, fontweight='bold')

    ax2.set_title('(B) Operation Latency (P50 & P99 in ns)', pad=10)
    ax2.set_ylabel('Operation Latency (ns)')
    ax2.set_xticks(x_sub)
    ax2.set_xticklabels([c[0] for c in configs])
    ax2.set_ylim(0, 210)
    ax2.legend(loc='upper right', framealpha=0.9, ncol=2, fontsize=8)

    # Subplot C: Page Fault Accounting
    bars_min_n = ax3.bar(x_sub - 1.5 * w_sub, min_flt_norm, width=w_sub, color='#777777', edgecolor='#111111', hatch='///', label='Normal Minor Faults')
    bars_min_l = ax3.bar(x_sub - 0.5 * w_sub, min_flt_lock, width=w_sub, color='#cccccc', edgecolor='#111111', hatch='xxx', label='Locked Minor Faults')
    bars_maj_n = ax3.bar(x_sub + 0.5 * w_sub, maj_flt_norm, width=w_sub, color='#222222', edgecolor='#111111', label='Normal Major Faults')
    bars_maj_l = ax3.bar(x_sub + 1.5 * w_sub, maj_flt_lock, width=w_sub, color='#ffffff', edgecolor='#111111', label='Locked Major Faults')

    ax3.text(x_sub[0] - 1.5 * w_sub, min_flt_norm[0] + 5, f"{min_flt_norm[0]}", ha='center', va='bottom', fontsize=8, fontweight='bold')
    ax3.text(x_sub[0] - 0.5 * w_sub, min_flt_lock[0] + 5, f"{min_flt_lock[0]}", ha='center', va='bottom', fontsize=8, fontweight='bold')
    ax3.text(x_sub[0] + w_sub, 8, "0 Major Faults", ha='center', va='bottom', fontsize=7.5, fontstyle='italic')
    ax3.text(x_sub[1] - w_sub, 8, "0 Minor Faults", ha='center', va='bottom', fontsize=7.5, fontstyle='italic')
    ax3.text(x_sub[1] + w_sub, 8, "0 Major Faults", ha='center', va='bottom', fontsize=7.5, fontstyle='italic')

    ax3.annotate(
        "Major Faults = 0 across all runs\n(RAM ~867 MB >> ~20 MB working set;\nno system swap active)",
        xy=(0.5, 120), xytext=(0.5, 170),
        ha='center', fontsize=8,
        bbox=dict(boxstyle='round,pad=0.3', facecolor='#ffffff', edgecolor='#333333', lw=0.7)
    )

    ax3.set_title('(C) Page Faults (Delta from getrusage)', pad=10)
    ax3.set_ylabel('Page Fault Count (Total 5 Runs)')
    ax3.set_xticks(x_sub)
    ax3.set_xticklabels([c[0] for c in configs])
    ax3.set_ylim(0, 270)
    ax3.legend(loc='upper right', framealpha=0.9, ncol=2, fontsize=7.5)

    plt.tight_layout()
    return save_figure(fig, output_dir, 'figure6_memory_locking', dpi)


# ==============================================================================
# Main Orchestration
# ==============================================================================
def main():
    parser = argparse.ArgumentParser(description='Generate KernelTick Academic Benchmark Visualizations.')
    parser.add_argument('--csv-dir', default='benchmark_results', help='Directory containing benchmark CSV files')
    parser.add_argument('--output-dir', default='benchmarks/figures', help='Output directory for generated figures')
    parser.add_argument('--dpi', type=int, default=300, help='Resolution DPI for raster PNG output')
    args = parser.parse_args()

    setup_matplotlib_style()
    os.makedirs(args.output_dir, exist_ok=True)

    print("=" * 70)
    print("KernelTick — Academic Benchmark Visualization Generator")
    print(f"CSV Directory   : {args.csv_dir}")
    print(f"Output Directory: {args.output_dir}")
    print(f"Raster DPI      : {args.dpi}")
    print("=" * 70)

    csv_map = {
        'fig1': os.path.join(args.csv_dir, 'order_book_benchmark.csv'),
        'fig2': os.path.join(args.csv_dir, 'memory_benchmark.csv'),
        'fig3': os.path.join(args.csv_dir, 'concurrency_benchmark.csv'),
        'fig4': os.path.join(args.csv_dir, 'cpu_affinity_benchmark.csv'),
        'fig5': os.path.join(args.csv_dir, 'scheduling_benchmark.csv'),
        'fig6': os.path.join(args.csv_dir, 'memory_locking_benchmark.csv'),
    }

    # Validate CSV existence
    for fig_id, path in csv_map.items():
        if not os.path.exists(path):
            print(f"Error: Required dataset '{path}' not found for {fig_id}!", file=sys.stderr)
            sys.exit(1)

    generated_files = []

    # Generate all 6 figures
    p1 = plot_figure1_order_book(csv_map['fig1'], args.output_dir, args.dpi)
    p2 = plot_figure2_memory_pool(csv_map['fig2'], args.output_dir, args.dpi)
    p3 = plot_figure3_concurrency(csv_map['fig3'], args.output_dir, args.dpi)
    p4 = plot_figure4_cpu_affinity(csv_map['fig4'], args.output_dir, args.dpi)
    p5 = plot_figure5_scheduling(csv_map['fig5'], args.output_dir, args.dpi)
    p6 = plot_figure6_memory_locking(csv_map['fig6'], args.output_dir, args.dpi)

    generated_files.extend([p1, p2, p3, p4, p5, p6])

    print("\n" + "=" * 70)
    print("Visualization Generation Complete!")
    print(f"Total Figures Generated: {len(generated_files)} (each in PNG and SVG)")
    print("All figures successfully saved to:", args.output_dir)
    print("=" * 70)


if __name__ == '__main__':
    main()
