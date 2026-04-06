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
 * Unit test for hwloc CPU binding in shmem_internal_heap_postinit().
 *
 * Exercises the code path added/fixed in:
 *   - "Adds initial work to update hwloc implementation" (08c89be2)
 *   - "Fix hwloc CPU binding bugs in shmem_internal_heap_postinit" (4fe8faac)
 *
 * The hwloc binding logic inside shmem_init() (when built with USE_HWLOC):
 *   1. Calls hwloc_get_proc_cpubind() to query the current CPU affinity.
 *      The bug fixed in 4fe8faac added a return-value check here; without it,
 *      the bitmap would be uninitialized if cpubind is unsupported, causing
 *      hwloc_get_next_obj_covering_cpuset_by_type() to operate on garbage.
 *   2. Finds the covering NUMA node for that affinity, falling back to the
 *      covering socket/package.
 *   3. Calls hwloc_bitmap_copy(bindset_covering_obj, covering_obj->cpuset).
 *      The original bug passed the hwloc_obj_t pointer directly instead of
 *      its ->cpuset bitmap member, silently corrupting the destination bitmap.
 *   4. Calls hwloc_set_proc_cpubind() with the (now correct) covering cpuset.
 *
 * After a correct shmem_init(), every CPU in each PE's process affinity mask
 * must belong to a single physical socket (the covering NUMA node or package).
 * This is the direct, observable effect of the binding code.
 *
 * Test strategy:
 *   1. Snapshot CPU affinity with sched_getaffinity() BEFORE shmem_init().
 *   2. Call shmem_init().
 *   3. Snapshot CPU affinity AFTER shmem_init().
 *   4. Verify all post-init CPUs belong to the same physical_package_id
 *      (sysfs: /sys/devices/system/cpu/cpuN/topology/physical_package_id).
 *      With the old hwloc_bitmap_copy bug the destination bitmap would hold
 *      a corrupt pointer value, binding the process to an arbitrary or empty
 *      CPU set -- causing this check to fail or the process to stall.
 *   5. Report whether binding was narrowed to a single NUMA node (informational).
 *   6. Report the before/after CPU counts per PE so the effect is visible.
 */

#define _GNU_SOURCE
#include <shmem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>      /* sched_getaffinity, cpu_set_t */
#include <sys/stat.h>

#define Rprintf  if (shmem_my_pe() == 0) printf

/* Read a single integer from a sysfs file; return -1 on failure. */
static int sysfs_read_int(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int val = -1;
    fscanf(f, "%d", &val);
    fclose(f);
    return val;
}

static int cpu_socket(int cpu)
{
    char path[256];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);
    return sysfs_read_int(path);
}

/* Return the NUMA node for a CPU by scanning for the nodeX symlink under
 * /sys/devices/system/cpu/cpuN/.  Returns -1 if not found (no NUMA). */
static int cpu_numa_node(int cpu)
{
    char path[256];
    struct stat st;
    for (int n = 0; n < 256; n++) {
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/node%d", cpu, n);
        if (lstat(path, &st) == 0)
            return n;
    }
    return -1;
}

/* Check that every CPU set in `mask` shares the same value returned by
 * `get_attr(cpu)`.  Returns the common value, or -2 on disagreement,
 * or -1 if the attribute is unavailable for all CPUs. */
static int affinity_uniform_attr(const cpu_set_t *mask,
                                 int (*get_attr)(int cpu),
                                 const char *attr_name,
                                 int me)
{
    int first = -1;
    int uniform = 1;

    for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
        if (!CPU_ISSET(cpu, mask)) continue;
        int val = get_attr(cpu);
        if (val < 0) continue;   /* attribute unavailable for this CPU */
        if (first < 0) {
            first = val;
        } else if (val != first) {
            fprintf(stderr,
                    "PE %d: post-init affinity spans multiple %s "
                    "(cpu %d -> %d, earlier cpu -> %d)\n",
                    me, attr_name, cpu, val, first);
            uniform = 0;
        }
    }
    return uniform ? first : -2;
}

static long target = -1L;

int
main(int argc, char *argv[])
{
    int me, npes, failures = 0;
    cpu_set_t pre_set, post_set;

    /* Snapshot affinity before shmem_init() touches it. */
    CPU_ZERO(&pre_set);
    sched_getaffinity(0, sizeof(pre_set), &pre_set);

    shmem_init();

    me   = shmem_my_pe();
    npes = shmem_n_pes();

    /* Snapshot affinity after shmem_init(). */
    CPU_ZERO(&post_set);
    sched_getaffinity(0, sizeof(post_set), &post_set);

    int pre_count  = CPU_COUNT(&pre_set);
    int post_count = CPU_COUNT(&post_set);

    /* --- Core assertion: post-init affinity is confined to one socket ----- *
     * shmem_internal_heap_postinit() binds each PE to the cpuset of the      *
     * covering NUMA node or socket.  All CPUs in the resulting affinity must  *
     * share a single physical_package_id.  The hwloc_bitmap_copy bug would   *
     * corrupt the bitmap, producing an invalid or cross-socket binding here. */
    int socket = affinity_uniform_attr(&post_set, cpu_socket,
                                       "physical socket", me);
    if (socket == -2) {
        /* Disagreement found; message already printed by helper. */
        failures++;
    }
    /* socket == -1 means sysfs unavailable; treat as inconclusive, not fail. */

    /* --- Informational: was binding narrowed to a single NUMA node? ------- */
    int numa = affinity_uniform_attr(&post_set, cpu_numa_node, "NUMA node", me);
    /* Spanning multiple NUMA nodes (falling back to socket) is correct;      *
     * only report it, do not fail.                                           */

    fprintf(stdout,
            "PE %d: CPUs before init=%d, after init=%d, socket=%d, numa=%s\n",
            me, pre_count, post_count,
            (socket >= 0 ? socket : -1),
            (numa >= 0 ? "single" : (numa == -2 ? "multi (socket fallback)"
                                                 : "unavailable")));
    fflush(stdout);

    /* --- Final barrier and result ----------------------------------------- */
    shmem_barrier_all();

    if (me == 0) {
        if (failures == 0)
            printf("shmem_init_hwloc: PASSED\n");
        else
            fprintf(stderr, "shmem_init_hwloc: FAILED (%d error(s))\n", failures);
    }

    shmem_finalize();
    return failures;
}
