/*
 *  Copyright (c) 2024 Intel Corporation. All rights reserved.
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

int main(int argc, char* argv[])
{
    int initialized;
    int init_count = 3;
    int finalize_count = 3;
    int inits_done = 0;
    int finalizes_done = 0;

    // Check if SHMEM is initialized before calling init
    shmem_query_initialized(&initialized);
    if (initialized != 0) {
        printf("ERR: Query_initialized not initially returning 0\n");
        return 1;
    }

    // Initialize SHMEM multiple times
    for (int i = 0; i < init_count; i++) {
        shmem_init();
        inits_done++;

        shmem_query_initialized(&initialized);
        if (initialized == 0) {
            printf("ERR: Initialization failed on iteration %d\n", i + 1);
            return 1;
        }
    }

    // Finalize SHMEM multiple times
    for (int i = 0; i < finalize_count; i++) {
        shmem_finalize();
        finalizes_done++;

        shmem_query_initialized(&initialized);

        if (inits_done > finalizes_done && initialized == 0) {
            printf("ERR: SHMEM should still be initialized after finalize iteration %d\n", i + 1);
            return 1;
        } else if (initialized != 0) {
            printf("ERR: SHMEM should be finalized after finalize iteration %d\n", i + 1);
            return 1;
        }
    }

    // Verify that init can be called again after finalizing 
    shmem_init();
    shmem_query_initialized(&initialized);
    if (initialized == 0) {
        printf("ERR: Re-initialization failed after finalization. Expected SHMEM to be initialized again.\n");
        return 1;
    }

    shmem_finalize();
    return 0;
}
