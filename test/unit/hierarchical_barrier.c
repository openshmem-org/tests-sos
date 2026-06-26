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
 * Correctness test for the hierarchical barrier (--enable-hierarchical-barrier).
 * The test is also valid and useful on non-hierarchical builds.
 *
 * Four subtests, each run for <loops> iterations:
 *
 * 1. barrier_all write visibility
 *    Each PE puts its PE number into a symmetric buffer on its right
 *    neighbor ((me+1)%npes), then calls shmem_barrier_all().  After
 *    the barrier, every PE verifies the received value equals the sender's
 *    PE number.  Failure means the barrier returned before the put was
 *    globally visible.
 *
 * 2. Rapid successive barriers
 *    Runs N back-to-back shmem_barrier_all() calls with a put and verify
 *    each round.  Stresses pSync slot reset: a bug that lets a signal
 *    from call N+1 land before call N's slot is cleared causes a hang or
 *    spurious completion.
 *
 * 3. Asymmetric arrival
 *    Even PEs busy-wait before entering the barrier; odd PEs enter
 *    immediately.  Verifies that the barrier still completes correctly
 *    when PEs arrive at very different times.
 *
 * 4. Atomic counter consistency
 *    All PEs atomically add 1 to a counter on PE 0 inside each loop
 *    iteration.  After shmem_barrier_all(), PE 0 reads its local counter
 *    and checks it equals (me==0 ? npes*loop : previous value).  After
 *    all loops, PE 0 verifies the final value is npes*loops.
 *
 * Usage: hierarchical_barrier [-v] [-l <loops>]
 *   -v  verbose output
 *   -l  number of iterations per subtest (default 100)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <shmem.h>

#define DFLT_LOOPS 100

static int Verbose;

/* Symmetric buffers */
static long put_buf;
static long counter;

static int
test_write_visibility(int me, int npes, int loops)
{
    int errors = 0;
    int right = (me + 1) % npes;
    int left  = (me - 1 + npes) % npes;

    for (int i = 0; i < loops; i++) {
        put_buf = -1;
        shmem_barrier_all();

        shmem_long_put(&put_buf, (long *)&me, 1, right);
        shmem_barrier_all();

        if (put_buf != left) {
            if (Verbose || errors < 3)
                fprintf(stderr,
                        "[%d] write_visibility loop %d: expected %d got %ld\n",
                        me, i, left, put_buf);
            errors++;
        }
    }
    return errors;
}

static int
test_successive_barriers(int me, int npes, int loops)
{
    int errors = 0;
    int right = (me + 1) % npes;
    int left  = (me - 1 + npes) % npes;

#define SUCCESSIVE_N 8
    for (int i = 0; i < loops; i++) {
        put_buf = -1;
        shmem_barrier_all();

        for (int r = 0; r < SUCCESSIVE_N; r++) {
            long val = (long)(me * SUCCESSIVE_N + r);
            shmem_long_put(&put_buf, &val, 1, right);
            shmem_barrier_all();

            long expected = (long)(left * SUCCESSIVE_N + r);
            if (put_buf != expected) {
                if (Verbose || errors < 3)
                    fprintf(stderr,
                            "[%d] successive loop %d round %d: expected %ld got %ld\n",
                            me, i, r, expected, put_buf);
                errors++;
            }
        }
    }
    return errors;
}

static int
test_asymmetric_arrival(int me, int npes, int loops)
{
    int errors = 0;
    int right = (me + 1) % npes;
    int left  = (me - 1 + npes) % npes;

    for (int i = 0; i < loops; i++) {
        put_buf = -1;
        shmem_barrier_all();

        /* Even PEs spin briefly to create a staggered arrival */
        if (me % 2 == 0) {
            volatile int spin = 10000;
            while (spin-- > 0) ;
        }

        shmem_long_put(&put_buf, (long *)&me, 1, right);
        shmem_barrier_all();

        if (put_buf != left) {
            if (Verbose || errors < 3)
                fprintf(stderr,
                        "[%d] asymmetric loop %d: expected %d got %ld\n",
                        me, i, left, put_buf);
            errors++;
        }
    }
    return errors;
}

static int
test_atomic_counter(int me, int npes, int loops)
{
    int errors = 0;

    counter = 0;
    shmem_barrier_all();

    for (int i = 1; i <= loops; i++) {
        shmem_long_atomic_add(&counter, 1L, 0);
        shmem_barrier_all();

        if (me == 0) {
            long expected = (long)npes * i;
            if (counter != expected) {
                if (Verbose || errors < 3)
                    fprintf(stderr,
                            "[0] atomic_counter loop %d: expected %ld got %ld\n",
                            i, expected, counter);
                errors++;
            }
        }
    }
    return errors;
}

int
main(int argc, char *argv[])
{
    int loops = DFLT_LOOPS;
    int opt;

    shmem_init();
    int me   = shmem_my_pe();
    int npes = shmem_n_pes();

    const char *pgm = strrchr(argv[0], '/');
    pgm = pgm ? pgm + 1 : argv[0];

    while ((opt = getopt(argc, argv, "vl:")) != -1) {
        switch (opt) {
        case 'v': Verbose++;              break;
        case 'l': loops = atoi(optarg);   break;
        default:
            if (me == 0)
                fprintf(stderr, "Usage: %s [-v] [-l loops]\n", pgm);
            shmem_finalize();
            return 1;
        }
    }

    if (npes < 2) {
        if (me == 0)
            fprintf(stderr, "%s: requires at least 2 PEs\n", pgm);
        shmem_finalize();
        return 77;
    }

    if (me == 0 && Verbose)
        fprintf(stdout, "%s: %d PEs, %d loops per subtest\n", pgm, npes, loops);

    int total_errors = 0;
    int e;

    e = test_write_visibility(me, npes, loops);
    if (me == 0) {
        if (e) fprintf(stderr, "%s: FAIL write_visibility (%d errors)\n", pgm, e);
        else if (Verbose) fprintf(stdout, "%s: PASS write_visibility\n", pgm);
    }
    total_errors += e;

    e = test_successive_barriers(me, npes, loops);
    if (me == 0) {
        if (e) fprintf(stderr, "%s: FAIL successive_barriers (%d errors)\n", pgm, e);
        else if (Verbose) fprintf(stdout, "%s: PASS successive_barriers\n", pgm);
    }
    total_errors += e;

    e = test_asymmetric_arrival(me, npes, loops);
    if (me == 0) {
        if (e) fprintf(stderr, "%s: FAIL asymmetric_arrival (%d errors)\n", pgm, e);
        else if (Verbose) fprintf(stdout, "%s: PASS asymmetric_arrival\n", pgm);
    }
    total_errors += e;

    e = test_atomic_counter(me, npes, loops);
    if (me == 0) {
        if (e) fprintf(stderr, "%s: FAIL atomic_counter (%d errors)\n", pgm, e);
        else if (Verbose) fprintf(stdout, "%s: PASS atomic_counter\n", pgm);
    }
    total_errors += e;

    if (me == 0) {
        if (total_errors)
            fprintf(stderr, "%s: FAILED (%d total errors)\n", pgm, total_errors);
        else
            fprintf(stdout, "%s: all subtests passed\n", pgm);
    }

    shmem_finalize();
    return total_errors ? 1 : 0;
}
