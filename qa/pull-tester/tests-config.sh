#!/bin/bash
# Copyright (c) 2013-2014 The oaccoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

BUILDDIR="/home/tsinghua-yincheng/桌面/oaccoin"
EXEEXT=""

# These will turn into comments if they were disabled when configuring.
ENABLE_WALLET=1
ENABLE_UTILS=1
ENABLE_OACCOIND=1

REAL_OACCOIND="$BUILDDIR/src/oaccoind${EXEEXT}"
REAL_OACCOINCLI="$BUILDDIR/src/oaccoin-cli${EXEEXT}"

