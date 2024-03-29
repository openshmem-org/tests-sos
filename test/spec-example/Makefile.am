# -*- Makefile -*-
#
# Copyright 2011 Sandia Corporation. Under the terms of Contract
# DE-AC04-94AL85000 with Sandia Corporation, the U.S.  Government
# retains certain rights in this software.
#
# Copyright (c) 2017 Intel Corporation. All rights reserved.
# This software is available to you under the BSD license.
#
# This file is part of the Sandia OpenSHMEM software package. For license
# information, see the LICENSE file in the top level directory of the
# distribution.

check_PROGRAMS = \
        shmem_ctx_pipelined_reduce \
	shmem_wait_until_all \
	shmem_wait_until_any \
	shmem_wait_until_some \
	shmem_wait_until_any_vector \
	shmem_test_any \
	shmem_test_some \
	shmem_team_split_strided \
	shmem_team_split_2D \
	shmem_team_translate \
	shmem_team_context \
	shmem_team_broadcast \
	shmem_team_collect \
	shmem_team_alltoall \
	shmem_team_alltoalls

if HAVE_OPENMP
check_PROGRAMS += \
    shmem_ctx
endif

if HAVE_C11
check_PROGRAMS += \
    shmem_team_sync \
    shmem_reduce_example
endif

if USE_PMI_MPI
check_PROGRAMS += \
    hybrid_mpi_mapping_id \
    hybrid_mpi_mapping_id_shmem_comm
endif

TESTS = $(check_PROGRAMS)

NPROCS ?= 2
LOG_COMPILER = $(TEST_RUNNER)

AM_LDFLAGS = $(LIBTOOL_WRAPPER_LDFLAGS)

if EXTERNAL_TESTS
bin_PROGRAMS = $(check_PROGRAMS)
AM_CPPFLAGS = -I$(top_srcdir)/test/include
AM_FCFLAGS =
LDADD =
else
AM_CPPFLAGS = -I$(top_builddir)/mpp -I$(top_srcdir)/mpp -I$(top_srcdir)/modules/tests-sos/test/include
AM_FCFLAGS = -I$(top_builddir)/mpp
LDADD = $(top_builddir)/src/libsma.la
endif

if USE_PMI_SIMPLE
LDADD += $(top_builddir)/pmi-simple/libpmi_simple.la
endif

shmem_ctx_CFLAGS = $(AM_OPENMP_CFLAGS)
