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
 * Regression test for the hierarchical-barrier per-team sense counter
 * (--enable-hierarchical-barrier).  The test is also valid and useful on
 * non-hierarchical builds, where it simply exercises mixed team syncs.
 *
 * Two OVERLAPPING teams are created from SHMEM_TEAM_WORLD:
 *
 *   Team A = even PEs            (0, 2, 4, ...)
 *   Team B = upper half of PEs   (npes/2 .. npes-1)
 *
 * The two sets are chosen so that some PEs belong to BOTH teams and some
 * belong to only ONE.  Neither team is exactly (0, 1, npes), so on a buggy
 * build neither uses per-team sense state.
 *
 * Each team is sync'd a DIFFERENT number of times (Team A: 3, Team B: 5),
 * interleaved, and then a final shmem_team_sync(SHMEM_TEAM_WORLD) /
 * shmem_barrier_all() joins everyone.  This is a valid OpenSHMEM program
 * that MUST complete on any conforming implementation.
 *
 * On a correct per-team-sense build, every team tracks its own count, so all
 * participants in any given barrier agree on the signal and every barrier
 * completes.  The test then passes.
 *
 * Usage: hier_barrier_sense [-v]
 *   -v  verbose output
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <shmem.h>

#define A_SYNCS 3
#define B_SYNCS 5

static int Verbose;

int
main(int argc, char *argv[])
{
    int opt;

    shmem_init();
    int me   = shmem_my_pe();
    int npes = shmem_n_pes();

    const char *pgm = strrchr(argv[0], '/');
    pgm = pgm ? pgm + 1 : argv[0];

    while ((opt = getopt(argc, argv, "v")) != -1) {
        switch (opt) {
        case 'v': Verbose++; break;
        default:
            if (me == 0)
                fprintf(stderr, "Usage: %s [-v]\n", pgm);
            shmem_finalize();
            return 1;
        }
    }

    /*
     * We need two overlapping teams, each with >= 2 members, that overlap on
     * some PEs but not all.
     *   Team A = even PEs:           start=0, stride=2, size=(npes+1)/2
     *   Team B = upper half of PEs:  start=npes/2, stride=1, size=npes-npes/2
     * For the overlap-but-not-equal property to hold with both teams having
     * at least 2 members we need a reasonable number of PEs.  npes >= 4 with
     * even npes is the simplest safe regime; smaller / odd configs are
     * skipped (success) rather than risking a degenerate set.
     */
    if (npes < 4) {
        if (me == 0)
            fprintf(stdout,
                    "%s: requires at least 4 PEs to build overlapping teams; "
                    "skipping (npes=%d)\n", pgm, npes);
        shmem_finalize();
        return 77;
    }

    int a_start  = 0;
    int a_stride = 2;
    int a_size   = (npes + 1) / 2;       /* count of even PEs in [0, npes) */

    int b_start  = npes / 2;
    int b_stride = 1;
    int b_size   = npes - (npes / 2);    /* upper half */

    if (a_size < 2 || b_size < 2) {
        if (me == 0)
            fprintf(stdout,
                    "%s: cannot build two teams with >= 2 members "
                    "(a_size=%d b_size=%d); skipping\n",
                    pgm, a_size, b_size);
        shmem_finalize();
        return 77;
    }

    int in_a = (me % 2 == 0);            /* even PE  -> Team A */
    int in_b = (me >= b_start);          /* upper PE -> Team B */

    /*
     * The scenario is genuinely exercised: PE 0 is always in A and (since
     * b_start = npes/2 >= 2 for npes >= 4) never in B, so it is single-team;
     * an even PE in the upper half (e.g. npes-2 when npes is even) is in both
     * teams.  Thus some PEs belong to exactly one team and some to both,
     * which is exactly what drives the per-PE sense counters apart.
     */

    if (me == 0 && Verbose)
        fprintf(stdout,
                "%s: %d PEs.  Team A = even PEs (start=%d stride=%d size=%d), "
                "Team B = upper half (start=%d stride=%d size=%d)\n",
                pgm, npes, a_start, a_stride, a_size,
                b_start, b_stride, b_size);

    shmem_team_t team_a = SHMEM_TEAM_INVALID;
    shmem_team_t team_b = SHMEM_TEAM_INVALID;
    int rc;

    rc = shmem_team_split_strided(SHMEM_TEAM_WORLD, a_start, a_stride, a_size,
                                  NULL, 0, &team_a);
    if (rc != 0 && in_a) {
        fprintf(stderr, "[%d] %s: split Team A failed rc=%d\n", me, pgm, rc);
    }

    rc = shmem_team_split_strided(SHMEM_TEAM_WORLD, b_start, b_stride, b_size,
                                  NULL, 0, &team_b);
    if (rc != 0 && in_b) {
        fprintf(stderr, "[%d] %s: split Team B failed rc=%d\n", me, pgm, rc);
    }

    /* PEs not in a given team get SHMEM_TEAM_INVALID and must not sync it. */
    if (in_a && team_a == SHMEM_TEAM_INVALID) {
        fprintf(stderr, "[%d] %s: expected to be in Team A but got INVALID\n",
                me, pgm);
    }
    if (in_b && team_b == SHMEM_TEAM_INVALID) {
        fprintf(stderr, "[%d] %s: expected to be in Team B but got INVALID\n",
                me, pgm);
    }

    /* Global join: everyone is past team creation. */
    shmem_barrier_all();
    if (me == 0)
        fprintf(stdout, "%s: teams created; beginning interleaved syncs\n", pgm);

    /*
     * Interleave team syncs with mismatched counts.  Each PE only participates
     * in the syncs for teams it actually belongs to, so PEs accumulate
     * different participation counts:
     *   - even PE not in B:        A_SYNCS
     *   - upper PE not even:       B_SYNCS
     *   - even upper PE (in both): A_SYNCS + B_SYNCS
     *   - PE in neither:           0
     * On the buggy shared-static sense build these divergent per-PE counts
     * desynchronize the next overlapping barrier and it hangs here.
     */
    int max_iter = (A_SYNCS > B_SYNCS) ? A_SYNCS : B_SYNCS;
    for (int it = 0; it < max_iter; it++) {

        if (in_a && it < A_SYNCS) {
            if (me == a_start)
                fprintf(stdout, "%s:   [A] sync %d/%d ...\n", pgm, it + 1, A_SYNCS);
            shmem_team_sync(team_a);
            if (me == a_start && Verbose)
                fprintf(stdout, "%s:   [A] sync %d/%d done\n", pgm, it + 1, A_SYNCS);
        }

        if (in_b && it < B_SYNCS) {
            if (me == b_start)
                fprintf(stdout, "%s:   [B] sync %d/%d ...\n", pgm, it + 1, B_SYNCS);
            shmem_team_sync(team_b);
            if (me == b_start && Verbose)
                fprintf(stdout, "%s:   [B] sync %d/%d done\n", pgm, it + 1, B_SYNCS);
        }
    }

    if (me == 0)
        fprintf(stdout, "%s: interleaved syncs complete; final TEAM_WORLD sync ...\n",
                pgm);

    /* Final join across all PEs.  This barrier's membership (TEAM_WORLD)
     * spans the PEs whose private counters diverged above. */
    shmem_team_sync(SHMEM_TEAM_WORLD);

    if (me == 0)
        fprintf(stdout, "%s: final TEAM_WORLD sync done; shmem_barrier_all ...\n",
                pgm);

    shmem_barrier_all();

    if (team_a != SHMEM_TEAM_INVALID)
        shmem_team_destroy(team_a);
    if (team_b != SHMEM_TEAM_INVALID)
        shmem_team_destroy(team_b);

    if (me == 0)
        fprintf(stdout, "%s: all barriers completed -- PASS\n", pgm);

    shmem_finalize();
    return 0;
}
