#!/bin/sh

./lzma --keep -z ${1}
./mkimage -A arm -O linux -T kernel -C lzma -a 80008000 -n Linux-2.6.34 -d ${1}.lzma ${2}.stage1
./mkimage -A arm -O linux -T firmware -C none -a ffffffff -e 00000000 -n LK:ZT280_K0_3m -d ${2}.stage1 ${2}

