/* -*- C -*-
 *
 * Copyright 2006 Sandia Corporation. Under the terms of Contract
 * DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government
 * retains certain rights in this software.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

/*
 * Adapted to Cray SHMEM by Brian Barrett
 */

#include <shmem.h>
#include <shmemx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

/* configuration parameters - setable by command line arguments */
int npeers = 2;
int niters = 128;
int nmsgs = 128;
int nbytes = 8;
int cache_size = (8 * 1024 * 1024 / sizeof(int));
int ppn = 1;
int machine_output = 0;

/* globals */
int *send_peers;
int *recv_peers;
int *cache_buf;
char *send_buf;
char *recv_buf;
int start_err = 0;
double tmp = 0;
double total = 0;

int rank = -1;
int world_size = -1;

static void
abort_app(const char *msg)
{
    perror(msg);
    abort();
}


static void
cache_invalidate(void)
{
    int i;

    cache_buf[0] = 1;
    for (i = 1 ; i < cache_size ; ++i) {
        cache_buf[i] = cache_buf[i - 1];
    }
}


static inline double
timer(void)
{
#ifdef HAVE_SHMEMX_WTIME
    return shmemx_wtime();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + (double) tv.tv_usec / 1000000.0;
#endif /* HAVE_SHMEMX_WTIME */
}


static void
display_result(const char *test, const double result)
{
    if (0 == rank) {
        if (machine_output) {
            printf("%.2f ", result);
        } else {
            printf("%16s: %.2f\n", test, result);
        }
    }
}


static void
test_one_way(void)
{
    int i, k;

    tmp = 0;
    total = 0;

    shmem_barrier_all();

    shmem_team_t sync_team;
    if (world_size % 2 == 1 && world_size != 1)
        shmem_team_split_strided(SHMEM_TEAM_WORLD, 0, 1, world_size - 1, NULL, 0, &sync_team);
    else
        sync_team = SHMEM_TEAM_WORLD;

    if (!(world_size % 2 == 1 && rank == (world_size - 1))) {
        if (rank < world_size / 2) {
            for (i = 0 ; i < niters ; ++i) {
                cache_invalidate();

                shmem_quiet();
                shmem_team_sync(sync_team);

                tmp = timer();
                for (k = 0 ; k < nmsgs ; ++k) {
                    shmem_putmem(recv_buf + (nbytes * k),
                                 send_buf + (nbytes * k),
                                 nbytes, rank + (world_size / 2));
                }
                shmem_quiet();
                total += (timer() - tmp);
            }
        } else {
            for (i = 0 ; i < niters ; ++i) {
                cache_invalidate();

                shmem_quiet();
                shmem_team_sync(sync_team);

                tmp = timer();
                shmem_short_wait_until((short*) (recv_buf + (nbytes * (nmsgs - 1))), SHMEM_CMP_NE, 0);
                total += (timer() - tmp);
                memset(recv_buf, 0, npeers * nmsgs * nbytes);
            }
        }

        shmem_double_sum_reduce(sync_team, &tmp, &total, 1);
        display_result("single direction", (niters * nmsgs) / (tmp / world_size));
    }

    shmem_barrier_all();
}


static void
test_same_direction(void)
{
    /* Not implemented yet */
}


static void
test_prepost(void)
{
    int i, j, k;

    tmp = 0;
    total = 0;

    shmem_barrier_all();

    for (i = 0 ; i < niters - 1 ; ++i) {
        cache_invalidate();

        shmem_barrier_all();

        tmp = timer();
        for (j = 0 ; j < npeers ; ++j) {
            for (k = 0 ; k < nmsgs ; ++k) {
                shmem_putmem(recv_buf + (nbytes * (k + j * nmsgs)),
                             send_buf + (nbytes * (k + j * nmsgs)),
                             nbytes, send_peers[npeers - j - 1]);
            }
        }
        shmem_quiet();
        shmem_short_wait_until((short*) (recv_buf + (nbytes * ((nmsgs - 1) + (npeers - 1) * nmsgs))), SHMEM_CMP_NE, 0);
        total += (timer() - tmp);
        memset(recv_buf, 0, npeers * nmsgs * nbytes);
    }

    shmem_double_sum_reduce(SHMEM_TEAM_WORLD, &tmp, &total, 1);
    display_result("pre-post", (niters * npeers * nmsgs * 2) / (tmp / world_size));
}


static void
test_allstart(void)
{
    /* BWB: Not implemented */
}


static void
usage(void)
{
    fprintf(stderr, "Usage: msgrate -n <ppn> [OPTION]...\n\n");
    fprintf(stderr, "  -h           Display this help message and exit\n");
    fprintf(stderr, "  -p <num>     Number of peers used in communication\n");
    fprintf(stderr, "  -i <num>     Number of iterations per test\n");
    fprintf(stderr, "  -m <num>     Number of messages per peer per iteration\n");
    fprintf(stderr, "  -s <size>    Number of bytes per message\n");
    fprintf(stderr, "  -c <size>    Cache size in bytes\n");
    fprintf(stderr, "  -n <ppn>     Number of procs per node\n");
    fprintf(stderr, "  -o           Format output to be machine readable\n");
    fprintf(stderr, "\nReport bugs to <bwbarre@sandia.gov>\n");
}


int
main(int argc, char *argv[])
{
    int i;

    shmem_init();

    rank = shmem_my_pe();
    world_size = shmem_n_pes();

    /* root handles arguments and bcasts answers */
    if (0 == rank) {
        int ch;
        while (start_err != 1 &&
               (ch = getopt(argc, argv, "p:i:m:s:c:n:oh")) != -1) {
            switch (ch) {
            case 'p':
                npeers = atoi(optarg);
                break;
            case 'i':
                niters = atoi(optarg);
                break;
            case 'm':
                nmsgs = atoi(optarg);
                break;
            case 's':
                nbytes = atoi(optarg);
                break;
            case 'c':
                cache_size = atoi(optarg) / sizeof(int);
                break;
            case 'n':
                ppn = atoi(optarg);
                break;
            case 'o':
                machine_output = 1;
                break;
            case 'h':
            case '?':
            default:
                start_err = 1;
                usage();
            }
        }

        /* sanity check */
        if (start_err != 1) {
            if (world_size < npeers) {
                fprintf(stderr, "Error: job size (%d) < number of peers (%d)\n",
                                 world_size, npeers);
                start_err = 77;
            } else if (ppn < 1) {
                fprintf(stderr, "Error: must specify process per node (-n #)\n");
                start_err = 77;
            } else if (world_size / ppn < npeers) {
                fprintf(stderr, "Error: node count < number of peers\n");
                start_err = 77;
            }
        }
    }

    shmem_barrier_all();

    /* broadcast results */
    shmem_int_broadcast(SHMEM_TEAM_WORLD, &start_err, &start_err, 1, 0);
    if (0 != start_err) {
        shmem_finalize();
        exit(start_err);
    }
    shmem_barrier_all();
    shmem_int_broadcast(SHMEM_TEAM_WORLD, &npeers, &npeers, 1, 0);
    shmem_barrier_all();
    shmem_int_broadcast(SHMEM_TEAM_WORLD, &niters, &niters, 1, 0);
    shmem_barrier_all();
    shmem_int_broadcast(SHMEM_TEAM_WORLD, &nmsgs, &nmsgs, 1, 0);
    shmem_barrier_all();
    shmem_int_broadcast(SHMEM_TEAM_WORLD, &nbytes, &nbytes, 1, 0);
    shmem_barrier_all();
    shmem_int_broadcast(SHMEM_TEAM_WORLD, &cache_size, &cache_size, 1, 0);
    shmem_barrier_all();
    shmem_int_broadcast(SHMEM_TEAM_WORLD, &ppn, &ppn, 1, 0);
    shmem_barrier_all();
    if (0 == rank) {
        if (!machine_output) {
            printf("job size:   %d\n", world_size);
            printf("npeers:     %d\n", npeers);
            printf("niters:     %d\n", niters);
            printf("nmsgs:      %d\n", nmsgs);
            printf("nbytes:     %d\n", nbytes);
            printf("cache size: %d\n", cache_size * (int)sizeof(int));
            printf("ppn:        %d\n", ppn);
        } else {
            printf("%d %d %d %d %d %d %d ",
                   world_size, npeers, niters, nmsgs, nbytes,
                   cache_size * (int)sizeof(int), ppn);
        }
    }

    /* allocate buffers */
    send_peers = malloc(sizeof(int) * npeers);
    if (NULL == send_peers) abort_app("malloc");
    recv_peers = malloc(sizeof(int) * npeers);
    if (NULL == recv_peers) abort_app("malloc");
    cache_buf = malloc(sizeof(int) * cache_size);
    if (NULL == cache_buf) abort_app("malloc");
    send_buf = malloc(npeers * nmsgs * nbytes);
    if (NULL == send_buf) abort_app("malloc");
    memset(send_buf, 1, npeers * nmsgs * nbytes);

    recv_buf = shmem_malloc(npeers * nmsgs * nbytes);
    if (NULL == recv_buf) abort_app("malloc");
    memset(recv_buf, 0, npeers * nmsgs * nbytes);

    /* calculate peers */
    for (i = 0 ; i < npeers ; ++i) {
        if (i < npeers / 2) {
            send_peers[i] = (rank + world_size + ((i - npeers / 2) * ppn)) % world_size;
        } else {
            send_peers[i] = (rank + world_size + ((i - npeers / 2 + 1) * ppn)) % world_size;
        }
    }
    if (npeers % 2 == 0) {
        /* even */
        for (i = 0 ; i < npeers ; ++i) {
            if (i < npeers / 2) {
                recv_peers[i] = (rank + world_size + ((i - npeers / 2) *ppn)) % world_size;
            } else {
                recv_peers[i] = (rank + world_size + ((i - npeers / 2 + 1) * ppn)) % world_size;
            }
        }
    } else {
        /* odd */
        for (i = 0 ; i < npeers ; ++i) {
            if (i < npeers / 2 + 1) {
                recv_peers[i] = (rank + world_size + ((i - npeers / 2 - 1) * ppn)) % world_size;
            } else {
                recv_peers[i] = (rank + world_size + ((i - npeers / 2) * ppn)) % world_size;
            }
        }
    }

    /* BWB: FIX ME: trash the free lists / malloc here */

    /* sync, although tests will do this on their own (in theory) */
    shmem_barrier_all();

    /* run tests */
    test_one_way();
    test_same_direction();
    test_prepost();
    test_allstart();

    if (rank == 0 && machine_output) printf("\n");

    /* done */
    shmem_finalize();
    return 0;
}
