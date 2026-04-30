/*
 * Copyright 2011 Sandia Corporation. Under the terms of Contract
 * DE-AC04-94AL85000 with Sandia Corporation, the U.S.  Government
 * retains certain rights in this software.
 *
 * This software is available to you under the BSD license.
 *
 * This file is part of the Sandia OpenSHMEM software package. For license
 * information, see the LICENSE file in the top level directory of the
 * distribution.
 */

/*
 * Barrier latency benchmark.
 *
 * Each PE times its own wait inside shmem_barrier_all().  After all
 * iterations, per-PE average latencies are gathered to PE 0 via
 * shmem_double_fcollect so the slowest and fastest PEs can be identified
 * by rank.  Per-iteration max and min reductions give the true worst-case
 * and best-case barrier latency each call.  All are summarized over
 * iterations as min/avg/max.
 *
 * Usage: shmem_barrier_perf [-n <iters>] [-w <warmup>]
 *   -n  number of timed iterations (default 1000)
 *   -w  number of warmup iterations (default 100)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <shmem.h>

static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static double reduce_src, reduce_dst;
static double fcollect_src, *fcollect_dst;

int main(int argc, char *argv[])
{
    int niters  = 1000;
    int nwarmup = 100;
    int opt;

    while ((opt = getopt(argc, argv, "n:w:")) != -1) {
        switch (opt) {
        case 'n': niters  = atoi(optarg); break;
        case 'w': nwarmup = atoi(optarg); break;
        default:
            fprintf(stderr, "Usage: %s [-n iters] [-w warmup]\n", argv[0]);
            return 1;
        }
    }

    shmem_init();
    int me   = shmem_my_pe();
    int npes = shmem_n_pes();

    /* fcollect_dst is symmetric so it must be shmem_malloc'd */
    fcollect_dst = shmem_malloc(npes * sizeof(double));
    if (!fcollect_dst) {
        fprintf(stderr, "PE %d: shmem_malloc failed\n", me);
        shmem_finalize();
        return 1;
    }

    double *samples = malloc(niters * sizeof(double));
    if (!samples) {
        fprintf(stderr, "PE %d: malloc failed\n", me);
        shmem_free(fcollect_dst);
        shmem_finalize();
        return 1;
    }

    /* warmup */
    for (int i = 0; i < nwarmup; i++)
        shmem_barrier_all();

    /* timed iterations — each PE records its own wait time */
    for (int i = 0; i < niters; i++) {
        double t0 = now_us();
        shmem_barrier_all();
        samples[i] = now_us() - t0;
    }

    /* Compute each PE's average latency over all iterations */
    double my_avg = 0.0;
    for (int i = 0; i < niters; i++)
        my_avg += samples[i];
    my_avg /= niters;

    /* Gather per-PE averages to all PEs (PE 0 will scan them) */
    fcollect_src = my_avg;
    shmem_double_fcollect(SHMEM_TEAM_WORLD, fcollect_dst, &fcollect_src, 1);

    /* Per-iteration reductions: max (slowest PE) and min (fastest PE) */
    double max_sum = 0.0, max_mn = 1e18, max_mx = 0.0;
    double min_sum = 0.0, min_mn = 1e18, min_mx = 0.0;
    double avg_sum = 0.0, avg_mn = 1e18, avg_mx = 0.0;

    for (int i = 0; i < niters; i++) {
        reduce_src = samples[i];
        shmem_double_max_reduce(SHMEM_TEAM_WORLD, &reduce_dst, &reduce_src, 1);
        if (me == 0) {
            max_sum += reduce_dst;
            if (reduce_dst < max_mn) max_mn = reduce_dst;
            if (reduce_dst > max_mx) max_mx = reduce_dst;
        }

        reduce_src = samples[i];
        shmem_double_min_reduce(SHMEM_TEAM_WORLD, &reduce_dst, &reduce_src, 1);
        if (me == 0) {
            min_sum += reduce_dst;
            if (reduce_dst < min_mn) min_mn = reduce_dst;
            if (reduce_dst > min_mx) min_mx = reduce_dst;
        }

        reduce_src = samples[i];
        shmem_double_sum_reduce(SHMEM_TEAM_WORLD, &reduce_dst, &reduce_src, 1);
        if (me == 0) {
            double avg = reduce_dst / npes;
            avg_sum += avg;
            if (avg < avg_mn) avg_mn = avg;
            if (avg > avg_mx) avg_mx = avg;
        }
    }

    if (me == 0) {
        /* Find slowest and fastest PE by average latency */
        int slowest_pe = 0, fastest_pe = 0;
        for (int i = 1; i < npes; i++) {
            if (fcollect_dst[i] > fcollect_dst[slowest_pe]) slowest_pe = i;
            if (fcollect_dst[i] < fcollect_dst[fastest_pe]) fastest_pe = i;
        }

        printf("shmem_barrier_all latency: %d PEs, %d iterations\n\n",
               npes, niters);
        printf("  Per-PE average latency:\n");
        printf("    slowest: PE %d  %.2f us\n",
               slowest_pe, fcollect_dst[slowest_pe]);
        printf("    fastest: PE %d  %.2f us\n\n",
               fastest_pe, fcollect_dst[fastest_pe]);
        printf("  Per-iteration summary (min/avg/max over %d iterations):\n",
               niters);
        printf("  %-14s  %8s  %8s  %8s\n", "", "min", "avg", "max");
        printf("  %-14s  %8.2f  %8.2f  %8.2f  us\n", "slowest PE",
               max_mn, max_sum / niters, max_mx);
        printf("  %-14s  %8.2f  %8.2f  %8.2f  us\n", "fastest PE",
               min_mn, min_sum / niters, min_mx);
        printf("  %-14s  %8.2f  %8.2f  %8.2f  us\n", "mean PE wait",
               avg_mn, avg_sum / niters, avg_mx);
    }

    free(samples);
    shmem_free(fcollect_dst);
    shmem_finalize();
    return 0;
}
