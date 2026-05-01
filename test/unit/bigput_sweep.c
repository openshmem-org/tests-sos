/*
 * bigput_sweep: each PE puts successive chunks of a large symmetric buffer
 * to ((my_pe()+1) mod num_pes()), sweeping the full allocation.  After all
 * chunks are transferred, every element is verified against the expected
 * value.  This exercises a large portion of the symmetric heap rather than
 * sending to the same fixed address repeatedly.
 *
 * Usage: bigput_sweep [-v] [-t <total_elements>] [-c <chunk_elements>] [-l <loops>]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/time.h>
#include <unistd.h>

#include <shmem.h>
#include "tests_sos/wtime.h"

/* Defaults: 128 MB total (32M ints), 1 MB chunks (256K ints) */
#define DFLT_TOTAL_ELEMENTS  (32 * 1024 * 1024)
#define DFLT_CHUNK_ELEMENTS  (256 * 1024)
#define DFLT_LOOPS           1

static int Verbose;

static long
atol_scaled(const char *s)
{
    char *e;
    long val = strtol(s, &e, 0);
    if (e && *e) {
        if (*e == 'k' || *e == 'K') val *= 1024;
        else if (*e == 'm' || *e == 'M') val *= 1024 * 1024;
        else if (*e == 'g' || *e == 'G') val *= 1024 * 1024 * 1024;
    }
    return val;
}

static void
usage(const char *pgm)
{
    fprintf(stderr,
        "usage: %s [-v] [-t total_elements] [-c chunk_elements] [-l loops]\n"
        "  -v                 verbose\n"
        "  -t total_elements  total int elements per PE (default %d = %d MB)\n"
        "  -c chunk_elements  ints per put (default %d = %d MB)\n"
        "  -l loops           sweep repetitions (default %d)\n",
        pgm,
        DFLT_TOTAL_ELEMENTS, (int)(DFLT_TOTAL_ELEMENTS * sizeof(int) / (1024*1024)),
        DFLT_CHUNK_ELEMENTS, (int)(DFLT_CHUNK_ELEMENTS * sizeof(int) / (1024*1024)),
        DFLT_LOOPS);
}

int
main(int argc, char **argv)
{
    int me, npes;
    int *Source, *Target;
    long total_elems = DFLT_TOTAL_ELEMENTS;
    long chunk_elems = DFLT_CHUNK_ELEMENTS;
    int  loops       = DFLT_LOOPS;
    int  i, loop;
    int  target_PE;
    long n_chunks;
    double start_time, elapsed = 0.0;
    long total_bytes;
    int  errors = 0;

    shmem_init();
    me   = shmem_my_pe();
    npes = shmem_n_pes();

    const char *pgm = strrchr(argv[0], '/');
    pgm = pgm ? pgm + 1 : argv[0];

    while ((i = getopt(argc, argv, "hvt:c:l:")) != EOF) {
        switch (i) {
        case 'v': Verbose++;  break;
        case 't':
            total_elems = atol_scaled(optarg);
            if (total_elems <= 0) {
                if (me == 0) fprintf(stderr, "ERR: bad total_elements\n");
                shmem_global_exit(1);
            }
            break;
        case 'c':
            chunk_elems = atol_scaled(optarg);
            if (chunk_elems <= 0) {
                if (me == 0) fprintf(stderr, "ERR: bad chunk_elements\n");
                shmem_global_exit(1);
            }
            break;
        case 'l':
            loops = (int)atol_scaled(optarg);
            if (loops <= 0) {
                if (me == 0) fprintf(stderr, "ERR: bad loop count\n");
                shmem_global_exit(1);
            }
            break;
        case 'h':
            if (me == 0) usage(pgm);
            shmem_finalize();
            return 0;
        default:
            if (me == 0) { fprintf(stderr, "unknown option\n"); usage(pgm); }
            shmem_global_exit(1);
        }
    }

    /* Clamp chunk to total */
    if (chunk_elems > total_elems)
        chunk_elems = total_elems;

    n_chunks    = total_elems / chunk_elems;
    total_bytes = (long)loops * n_chunks * chunk_elems * sizeof(int);
    target_PE   = (me + 1) % npes;

    Source = (int *)shmem_malloc(total_elems * sizeof(int));
    Target = (int *)shmem_malloc(total_elems * sizeof(int));
    if (!Source || !Target) {
        fprintf(stderr, "PE %d: shmem_malloc failed for %ld ints\n",
                me, total_elems);
        shmem_global_exit(1);
    }

    /* Every PE announces itself */
    for (int p = 0; p < npes; p++) {
        if (me == p) {
            char hostname[256];
            gethostname(hostname, sizeof(hostname));
            fprintf(stdout, "PE %d: host=%s\n", me, hostname);
            fflush(stdout);
        }
        shmem_barrier_all();
    }

    /* Fill source with a known pattern; clear target */
    for (long j = 0; j < total_elems; j++) {
        Source[j] = (int)(j + 1);
        Target[j] = -1;
    }

    if (Verbose && me == 0) {
        fprintf(stdout,
                "%s: %d PEs, total=%ld ints (%ld MB), chunk=%ld ints (%ld MB), "
                "%ld chunks/sweep, %d loop(s)\n",
                pgm, npes,
                total_elems, total_elems * sizeof(int) / (1024*1024),
                chunk_elems, chunk_elems * sizeof(int) / (1024*1024),
                n_chunks, loops);
    }

    shmem_barrier_all();

    for (loop = 0; loop < loops; loop++) {
        start_time = tests_sos_wtime();

        /* Sweep: put each chunk to the corresponding offset on target_PE */
        for (long c = 0; c < n_chunks; c++) {
            long offset = c * chunk_elems;
            shmem_int_put(Target + offset, Source + offset,
                          chunk_elems, target_PE);
        }

        elapsed += tests_sos_wtime() - start_time;
    }

    shmem_barrier_all();

    /* Verify: Target should now match Source (written by the previous PE) */
    for (long j = 0; j < total_elems; j++) {
        if (Target[j] != (int)(j + 1)) {
            if (errors < 10)
                fprintf(stderr, "PE %d: Target[%ld] = %d, expected %ld\n",
                        me, j, Target[j], j + 1);
            errors++;
        }
    }

    if (errors)
        fprintf(stderr, "PE %d: %d verification error(s)\n", me, errors);
    else if (Verbose)
        fprintf(stdout, "PE %d: all %ld elements verified OK\n", me, total_elems);

    /* Aggregate and report bandwidth from PE 0 */
    if (me == 0 && Verbose) {
        double rate = ((double)total_bytes / (1024.0 * 1024.0)) / elapsed;
        fprintf(stdout, "%s: %.4f MB/sec (%.6f sec, %ld bytes)\n",
                pgm, rate, elapsed, total_bytes);
    }

    shmem_free(Source);
    shmem_free(Target);

    shmem_finalize();
    return errors ? 1 : 0;
}
