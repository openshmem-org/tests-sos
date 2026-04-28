/*
 * dual_plane_barrier: correctness test for the dual-plane hierarchical barrier.
 *
 * Exercises shmem_barrier_all() and shmem_sync_all() for correctness under
 * repeated calls, then verifies that atomic increments remain consistent after
 * each barrier — catching cases where the barrier returns too early (stale
 * data visible) or not at all (hang).
 *
 * The test is meaningful on any build: with USE_DUAL_PLANE_ATOMICS it exercises
 * the hierarchical path; without it exercises the standard path.  In both cases
 * the observable semantics must be identical.
 *
 * Usage: dual_plane_barrier [-v] [-l <loops>]
 *   -v        verbose: print per-iteration results from PE 0
 *   -l loops  number of barrier/sync iterations (default 100)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <shmem.h>

#define Rprintf  if (me == 0) printf
#define RVprintf if (Verbose && me == 0) printf

static int Verbose;

/*
 * Test 1: shmem_barrier_all() correctness.
 *
 * Each PE writes its PE number into every other PE's symmetric slot, then
 * calls barrier_all.  After the barrier every slot must equal the writing PE's
 * value, proving no PE read a slot before the remote write completed.
 */
static int
test_barrier_all(int me, int npes, int loops)
{
    static long *target;
    int errors = 0;

    target = shmem_malloc(sizeof(long));
    if (!target) {
        fprintf(stderr, "[%d] shmem_malloc failed\n", me);
        return 1;
    }

    for (int iter = 0; iter < loops; iter++) {
        *target = -1;
        shmem_barrier_all();

        /* Every PE writes its rank into PE (me+1 mod npes)'s slot */
        int peer = (me + 1) % npes;
        shmem_long_p(target, (long)me, peer);

        shmem_barrier_all();

        /* Verify: our slot should have been written by PE (me-1 mod npes) */
        long expected = (long)((me - 1 + npes) % npes);
        if (*target != expected) {
            fprintf(stderr, "[%d] barrier_all iter %d: target=%ld expected=%ld\n",
                    me, iter, *target, expected);
            errors++;
        }
    }

    shmem_free(target);
    return errors;
}

/*
 * Test 2: shmem_sync_all() correctness.
 *
 * Same pattern as test 1 but using shmem_sync_all() after a quiet, confirming
 * that sync_all provides the same ordering guarantee.
 */
static int
test_sync_all(int me, int npes, int loops)
{
    static long *target;
    int errors = 0;

    target = shmem_malloc(sizeof(long));
    if (!target) {
        fprintf(stderr, "[%d] shmem_malloc failed\n", me);
        return 1;
    }

    for (int iter = 0; iter < loops; iter++) {
        *target = -1;
        shmem_sync_all();

        int peer = (me + 1) % npes;
        shmem_long_p(target, (long)me, peer);
        shmem_quiet();

        shmem_sync_all();

        long expected = (long)((me - 1 + npes) % npes);
        if (*target != expected) {
            fprintf(stderr, "[%d] sync_all iter %d: target=%ld expected=%ld\n",
                    me, iter, *target, expected);
            errors++;
        }
    }

    shmem_free(target);
    return errors;
}

/*
 * Test 3: atomic increment consistency across barriers.
 *
 * Every PE atomically increments a shared counter on PE 0 loops times, with a
 * barrier_all between each round.  After all rounds the counter on PE 0 must
 * equal npes * loops.  This catches barriers that return before remote AMOs
 * are visible.
 */
static int
test_atomic_consistency(int me, int npes, int loops)
{
    static long *counter;
    int errors = 0;

    counter = shmem_malloc(sizeof(long));
    if (!counter) {
        fprintf(stderr, "[%d] shmem_malloc failed\n", me);
        return 1;
    }

    *counter = 0;
    shmem_barrier_all();

    for (int iter = 0; iter < loops; iter++) {
        shmem_long_atomic_inc(counter, 0);
        shmem_barrier_all();

        if (me == 0) {
            long expected = (long)npes * (iter + 1);
            if (*counter != expected) {
                fprintf(stderr, "[0] atomic iter %d: counter=%ld expected=%ld\n",
                        iter, *counter, expected);
                errors++;
            }
        }
        shmem_barrier_all();
    }

    shmem_free(counter);
    return errors;
}

/*
 * Test 4: team-based sync correctness.
 *
 * Split PEs into two halves using shmem_team_split_strided and run
 * shmem_team_sync() on each half independently, verifying that a value
 * written before the sync is visible to all team members after it returns.
 */
static int
test_team_sync(int me, int npes, int loops)
{
    int errors = 0;

    if (npes < 4) {
        RVprintf("  team-sync test skipped (requires >= 4 PEs)\n");
        return 0;
    }

    /* Two halves: [0, npes/2) and [npes/2, npes) */
    int half     = npes / 2;
    int in_first = (me < half);
    int pe_start = in_first ? 0    : half;
    int pe_size  = in_first ? half : (npes - half);

    shmem_team_t half_team;
    int ret = shmem_team_split_strided(SHMEM_TEAM_WORLD, pe_start, 1, pe_size,
                                       NULL, 0, &half_team);
    if (ret != 0) {
        fprintf(stderr, "[%d] shmem_team_split_strided failed (%d)\n", me, ret);
        return 1;
    }

    long *target = shmem_malloc(sizeof(long));
    if (!target) {
        fprintf(stderr, "[%d] shmem_malloc failed\n", me);
        shmem_team_destroy(half_team);
        return 1;
    }

    int team_rank = shmem_team_my_pe(half_team);
    int team_size = pe_size;

    for (int iter = 0; iter < loops; iter++) {
        *target = -1;
        shmem_team_sync(half_team);

        /* Write to next PE within the same half-team */
        int next_rank = (team_rank + 1) % team_size;
        int peer      = pe_start + next_rank;
        shmem_long_p(target, (long)me, peer);
        shmem_quiet();

        shmem_team_sync(half_team);

        int prev_rank   = (team_rank - 1 + team_size) % team_size;
        long expected   = (long)(pe_start + prev_rank);
        if (*target != expected) {
            fprintf(stderr, "[%d] team-sync iter %d: target=%ld expected=%ld\n",
                    me, iter, *target, expected);
            errors++;
        }
    }

    shmem_barrier_all();
    shmem_free(target);
    shmem_team_destroy(half_team);
    return errors;
}

int
main(int argc, char **argv)
{
    int me, npes;
    int loops   = 100;
    int errors  = 0;
    int c;

    shmem_init();
    me   = shmem_my_pe();
    npes = shmem_n_pes();

    while ((c = getopt(argc, argv, "vl:")) != -1) {
        switch (c) {
        case 'v':
            Verbose++;
            break;
        case 'l':
            loops = atoi(optarg);
            if (loops <= 0 || loops > 1000000) {
                Rprintf("ERR - loops out of bounds '%d'\n", loops);
                shmem_finalize();
                return 1;
            }
            break;
        default:
            Rprintf("ERR - unknown option -%c\n", c);
            shmem_finalize();
            return 1;
        }
    }

    if (npes < 2) {
        Rprintf("ERR - requires > 1 PEs\n");
        shmem_finalize();
        return 0;
    }

    RVprintf("dual_plane_barrier: %d PEs, %d loops\n", npes, loops);

    RVprintf("  test 1: shmem_barrier_all\n");
    errors += test_barrier_all(me, npes, loops);

    RVprintf("  test 2: shmem_sync_all\n");
    errors += test_sync_all(me, npes, loops);

    RVprintf("  test 3: atomic consistency across barriers\n");
    errors += test_atomic_consistency(me, npes, loops);

    RVprintf("  test 4: team-based sync\n");
    errors += test_team_sync(me, npes, loops);

    /* Reduce errors across all PEs so a non-zero exit comes from any failure */
    long total_errors = (long)errors;
    shmem_long_sum_reduce(SHMEM_TEAM_WORLD, &total_errors, &total_errors, 1);

    if (me == 0) {
        if (total_errors == 0)
            printf("dual_plane_barrier: %d PEs, %d loops: PASSED\n", npes, loops);
        else
            printf("dual_plane_barrier: %d PEs, %d loops: FAILED (%ld errors)\n",
                   npes, loops, total_errors);
    }

    shmem_finalize();
    return (total_errors != 0) ? 1 : 0;
}
