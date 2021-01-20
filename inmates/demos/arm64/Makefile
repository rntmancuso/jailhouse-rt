#
# Jailhouse AArch64 support
#
# Copyright (C) 2015 Huawei Technologies Duesseldorf GmbH
#
# Authors:
#  Antonios Motakis <antonios.motakis@huawei.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#

include $(INMATES_LIB)/Makefile.lib

INMATES := gic-demo.bin uart-demo.bin ivshmem-demo.bin mem-bomb.bin mem-bomb-stride.bin mem-bomb-fence.bin cbf-mem-bomb.bin cbf-mem-bomb-fence.bin

gic-demo-y	:= ../arm/gic-demo.o
uart-demo-y	:= ../arm/uart-demo.o
ivshmem-demo-y	:= ../ivshmem-demo.o
mem-bomb-y	:= ../arm/mem-bomb.o
cbf-mem-bomb-y	:= ../arm/cbf-mem-bomb.o
mem-bomb-fence-y	:= ../arm/mem-bomb-fence.o
cbf-mem-bomb-fence-y := ../arm/cbf-mem-bomb-fence.o
mem-bomb-stride-y	:= ../arm/mem-bomb-stride.o

$(eval $(call DECLARE_TARGETS,$(INMATES)))
