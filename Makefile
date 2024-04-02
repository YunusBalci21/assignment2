#
# Module Makefile for DM510 (2024)
#

# Change this if you keep your files elsewhere
ROOT = /home/dm510/dm510
KERNELDIR = $(ROOT)/linux-6.6.9
PWD = $(shell pwd)

obj-m += dm510_dev.o

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) LDDINC=$(KERNELDIR)/include ARCH=um EXTRA_CFLAGS="-I$(PWD)" modules

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions

