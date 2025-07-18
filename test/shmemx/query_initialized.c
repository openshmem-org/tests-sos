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
#include <stdlib.h>
#include <string.h>
#include <shmem.h>
#include <shmemx.h>

#define MAX_NAME_LEN  256

int main(int argc, char* argv[])
{
    int initialized = -1;
    const int max_init_count = 10;
    int min_init_count = 0;

    while (min_init_count < max_init_count) {
      min_init_count++;

      /* initialize and check if state is initialized */
      for (int i = 0; i < min_init_count; i++) {
          shmemx_query_initialized(&initialized);
          if ((!i && initialized) || (i && !initialized)) {
              fprintf(stderr, "error: query_initialized() returned invalid value"
                      " at %s:%d [init] (query=%d i=%d, min=%d, max=%d)\n",
                      __FILE__, __LINE__, initialized, i, min_init_count, max_init_count);
              abort();
          }
          shmem_init();
      }

      /* finalize and check if state is initialized */
      for (int i = 0; i < min_init_count; i++) {
          shmemx_query_initialized(&initialized);
          if (!initialized) {
              fprintf(stderr, "error: query_initialized() returned invalid"
                      " value at %s:%d [fini] (query=%d i=%d, min=%d, max=%d)\n",
                      __FILE__, __LINE__, initialized, i, min_init_count, max_init_count);
              abort();
          }
          shmem_finalize();
      }
    }

    /* Sandia OpenSHMEM does not currently support switching thread levels validate this */
    /* Add a version check if this switching thread level is supported in future */

    char vendor_name[MAX_NAME_LEN];
    int provided = -1;
    shmemx_query_initialized(&initialized);

    shmem_info_get_name(vendor_name);
    if (!strncmp(vendor_name, "Sandia OpenSHMEM", MAX_NAME_LEN)) {
        if (shmem_init_thread(SHMEM_THREAD_SINGLE, &provided) || provided != SHMEM_THREAD_SINGLE) {
            fprintf(stderr, "error: initializing SHMEM SHMEM_THREAD_SINGLE"
                    " provided=%d\n", provided);
            abort();
        }

        if (shmem_init_thread(SHMEM_THREAD_MULTIPLE, &provided) || provided != SHMEM_THREAD_SINGLE) {
            fprintf(stderr, "error: switching thread mode unsupported, but reported by OpenSHMEM"
                    " provided=%d\n", provided);
            abort();
        }

        shmem_finalize();
        shmem_finalize();

        shmemx_query_initialized(&initialized);

        if (initialized) {
            fprintf(stderr, "error: query_initialized() returned invalid"
                    " value at %s:%d [init_thread]\n", __FILE__, __LINE__);
            abort();
        }
    }

    return 0;
}
