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

ACLOCAL_AMFLAGS = -I config

EXTRA_DIST = README NEWS LICENSE

SUBDIRS = test

all:
	@echo "==============================="
	@echo "= Sandia OpenSHMEM Test Suite ="
	@echo "==============================="
	@echo
	@echo "To build and run the test suite:"
	@echo " $$ make check"
	@echo
	@echo "The following make variables can be used:"
	@echo " TESTS       - Specify tests to be run (TESTS=\"\" runs no tests)"
	@echo " NPROCS      - Specify number of processes"
	@echo " TEST_RUNNER - Specify command used to run tests, e.g.,"
	@echo "               'make check TEST_RUNNER=\"oshrun -np 4 -f hosts\"'"
	@echo
