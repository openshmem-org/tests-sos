/*
 *  Copyright (c) 2017 Intel Corporation. All rights reserved.
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

#include <stdio.h>
#include <shmem.h>

#define NREPS 50

#ifdef ENABLE_DEPRECATED_TESTS
long sync_psync0[SHMEM_BARRIER_SYNC_SIZE];
long sync_psync1[SHMEM_BARRIER_SYNC_SIZE];
#endif

int main(void)
{
    int i, me, npes, n_loops;

    shmem_init();

    me = shmem_my_pe();
    npes = shmem_n_pes();

#ifdef ENABLE_DEPRECATED_TESTS
    for (i = 0; i < SHMEM_BARRIER_SYNC_SIZE; i++) {
        sync_psync0[i] = SHMEM_SYNC_VALUE;
        sync_psync1[i] = SHMEM_SYNC_VALUE;
    }
    n_loops = me;
#else
    n_loops = 1;
#endif

    shmem_sync_all();

    /* A total of npes tests are performed, where the active set in each test
     * includes PEs i..npes-1.
     * Teams must share the same value for "start" (according to the OpenSHMEM
     * specification), so in that case sync across SHMEM_TEAM_WORLD. */
    for (i = 0; i <= n_loops; i++) {
        int j;

        if (me == i)
            printf(" + iteration %d\n", i);

        /* Test that sync can be called repeatedly with the *same* pSync */
        for (j = 0; j < NREPS; j++) {
#ifdef ENABLE_DEPRECATED_TESTS
            shmem_sync(i, 0, npes-i, (i % 2) ? sync_psync0 : sync_psync1);
#else
            shmem_team_sync(SHMEM_TEAM_WORLD);
#endif
        }
    }

    shmem_finalize();

    return 0;
}
