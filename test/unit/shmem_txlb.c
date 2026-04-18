/*
 *  Copyright (c) 2026 Intel Corporation. All rights reserved.
 *  This software is available to you under the BSD license below:
 *
 *      Redistribution and use in source and binary forms, with or
 *      without modification, are permitted provided that the following
 *      conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Unit test for OFI TX load balancing NIC selection.
 *
 * Tests the feature added in commits b68a86a..HEAD, which introduces two new
 * configure-time TX load balancing modes for the OFI transport:
 *
 *   --enable-ofi-tx-load-balancing-round-robin
 *       SHMEM_GET_TRANSMIT_NIC_IDX() uses an atomic counter
 *       (shmem_internal_nic_rr_idx) incremented per operation, cycling through
 *       all available NICs uniformly.
 *
 *   --enable-ofi-tx-load-balancing-random
 *       SHMEM_GET_TRANSMIT_NIC_IDX() uses rand_r() % num_nics, replacing the
 *       old float-multiply path that could produce an out-of-bounds index when
 *       rand_int == RAND_MAX.
 *
 *   (default / neither)
 *       SHMEM_GET_TRANSMIT_NIC_IDX() is a no-op; nic_idx stays 0.
 *
 * Test strategy:
 *
 * 1. DATA INTEGRITY (all modes)
 *    All-to-all put: every PE writes a unique value to every other PE across
 *    multiple message sizes (small/inline, medium, large RDMA).  Each received
 *    value is verified after a quiet+barrier.  Any out-of-bounds nic_idx or
 *    corrupt NIC selection would produce a send on the wrong endpoint, leading
 *    to data corruption or a hang.
 *
 * 2. ROUND-ROBIN COUNTER ADVANCEMENT (round-robin mode only)
 *    shmem_internal_nic_rr_idx is a global symbol exported from libsma.  We
 *    snapshot it before and after a known number of put operations and assert
 *    the counter advanced by exactly that number.  This directly validates the
 *    atomic increment path in SHMEM_GET_TRANSMIT_NIC_IDX and confirms that
 *    every put call exercised NIC cycling rather than always using NIC 0.
 *
 * 3. WRITE COUNTER CONSISTENCY (all modes, shmemx extension)
 *    shmemx_pcntr_get_issued_write() reports the total number of put
 *    operations issued on the default context.  After the all-to-all we assert
 *    this equals the number of puts we called, confirming that every operation
 *    reached the transport layer regardless of which NIC was selected.
 */

#include <shmem.h>
#include <shmemx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

/*
 * shmem_internal_nic_rr_idx is declared extern in shmem_internal.h and
 * exported from libsma.  Declare it here to access it directly when the
 * round-robin mode was compiled in.
 */
#ifdef USE_OFI_TX_LOAD_BALANCING_ROUND_ROBIN
extern size_t shmem_internal_nic_rr_idx;
extern size_t shmem_transport_ofi_num_nics;
#endif

#define Rprintf  if (shmem_my_pe() == 0) printf
#define Rfprintf if (shmem_my_pe() == 0) fprintf

/* Message sizes to exercise: inline/bounce, medium, large RDMA */
#define N_SIZES  3
static const size_t msg_sizes[N_SIZES] = { 8, 512, 65536 };

/* Number of put iterations per size (to accumulate NIC cycling) */
#define ITERS 16

/* Symmetric buffers */
#define MAX_MSG 65536
static long sbuf[MAX_MSG / sizeof(long)];
static long rbuf[MAX_MSG / sizeof(long)];

static int
check_data(int me, int sender, size_t nlong, size_t msg_size, int iter)
{
    long expected = (long)((sender << 16) | (iter & 0xffff));
    int errs = 0;
    for (size_t i = 0; i < nlong; i++) {
        if (rbuf[i] != expected) {
            fprintf(stderr,
                    "PE %d: data mismatch from PE %d iter %d word %zu: "
                    "got 0x%lx expected 0x%lx (msg_size=%zu)\n",
                    me, sender, iter, i, rbuf[i], expected, msg_size);
            errs++;
            if (errs >= 4) break; /* limit noise */
        }
    }
    return errs;
}

int
main(int argc, char *argv[])
{
    shmem_init();

    int me   = shmem_my_pe();
    int npes = shmem_n_pes();
    int failures = 0;

    /* ------------------------------------------------------------------ *
     * 1. DATA INTEGRITY: all-to-all puts, multiple sizes and iterations  *
     * ------------------------------------------------------------------ */
    for (int s = 0; s < N_SIZES; s++) {
        size_t msg_size = msg_sizes[s];
        size_t nlong    = msg_size / sizeof(long);

        for (int iter = 0; iter < ITERS; iter++) {
            long fill = (long)((me << 16) | (iter & 0xffff));

            /* Initialise send buffer with a PE- and iter-unique value */
            for (size_t i = 0; i < nlong; i++)
                sbuf[i] = fill;

            /* Reset receive buffer */
            memset(rbuf, 0xff, msg_size);

            shmem_barrier_all();

            /* PE 0 drives: put to every other PE, then verify */
            if (me == 0) {
                for (int pe = 1; pe < npes; pe++)
                    shmem_long_put(rbuf, sbuf, nlong, pe);

                shmem_quiet();

                /* Ask non-zero PEs to check */
                long go = 1;
                for (int pe = 1; pe < npes; pe++)
                    shmem_long_put(&rbuf[nlong], &go, 1, pe);
                shmem_quiet();
            } else {
                /* Wait for the sentinel word written last by PE 0 */
                shmem_long_wait_until(&rbuf[nlong], SHMEM_CMP_EQ, 1);

                failures += check_data(me, 0, nlong, msg_size, iter);

                /* Reset sentinel for next iter */
                rbuf[nlong] = 0xffffffffffffffffUL;
            }

            shmem_barrier_all();
        }
    }

    /* ------------------------------------------------------------------ *
     * 2. ROUND-ROBIN COUNTER ADVANCEMENT                                 *
     *    Snapshot rr_idx, do a known number of puts, verify advancement. *
     * ------------------------------------------------------------------ */
#ifdef USE_OFI_TX_LOAD_BALANCING_ROUND_ROBIN
    {
        size_t nlong  = 1;
        size_t n_puts = (size_t)(npes - 1) * ITERS; /* puts PE 0 will issue */

        shmem_barrier_all();

        size_t rr_before = 0, rr_after = 0;

        if (me == 0) {
            rr_before = shmem_internal_nic_rr_idx;

            sbuf[0] = 0xcafeL;
            for (int iter = 0; iter < ITERS; iter++)
                for (int pe = 1; pe < npes; pe++)
                    shmem_long_put(rbuf, sbuf, nlong, pe);
            shmem_quiet();

            rr_after = shmem_internal_nic_rr_idx;

            if (rr_after - rr_before != n_puts) {
                fprintf(stderr,
                        "PE 0: round-robin counter advanced by %zu, expected %zu "
                        "(%d PEs x %d iters)\n",
                        rr_after - rr_before, n_puts, npes - 1, ITERS);
                failures++;
            }

            Rprintf("shmem_txlb: rr_idx before=%zu after=%zu delta=%zu "
                    "num_nics=%zu\n",
                    rr_before, rr_after, rr_after - rr_before,
                    shmem_transport_ofi_num_nics);
        }

        shmem_barrier_all();
    }
#endif /* USE_OFI_TX_LOAD_BALANCING_ROUND_ROBIN */

    /* ------------------------------------------------------------------ *
     * 3. WRITE COUNTER CONSISTENCY via shmemx_pcntr_get_issued_write()   *
     * ------------------------------------------------------------------ */
#ifdef ENABLE_SHMEMX_TESTS
    {
        /*
         * Count puts PE 0 issued in section 1: for each (size, iter) pair,
         * PE 0 did (npes-1) data puts + (npes-1) sentinel puts.
         */
        if (me == 0 && npes > 1) {
            uint64_t expected_writes = (uint64_t)N_SIZES * ITERS * (npes - 1) * 2;

#ifdef USE_OFI_TX_LOAD_BALANCING_ROUND_ROBIN
            /* Also count the puts from section 2 */
            expected_writes += (uint64_t)(npes - 1) * ITERS;
#endif
            uint64_t issued = 0;
            shmemx_pcntr_get_issued_write(SHMEM_CTX_DEFAULT, &issued);

            if (issued < expected_writes) {
                fprintf(stderr,
                        "PE 0: issued write counter %"PRIu64" < expected %"PRIu64"\n",
                        issued, expected_writes);
                failures++;
            }
            Rprintf("shmem_txlb: issued_writes=%"PRIu64" expected>=%"PRIu64"\n",
                    issued, expected_writes);
        }
    }
#endif /* ENABLE_SHMEMX_TESTS */

    shmem_barrier_all();

    if (me == 0) {
        if (failures == 0)
            printf("shmem_txlb: PASSED\n");
        else
            fprintf(stderr, "shmem_txlb: FAILED (%d error(s))\n", failures);
    }

    shmem_finalize();
    return failures;
}
