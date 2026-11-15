# Out-of-tree build of the TBS QBOX22 (TBS 5922) DVB-S2 USB driver.
# Works both standalone and under DKMS.
ifneq ($(KERNELRELEASE),)
obj-m += tas2101.o
obj-m += av201x.o
obj-m += dvb-usb-tbsqbox22.o

dvb-usb-tbsqbox22-y := tbs-qbox22.o

ccflags-y += -I$(src) -I$(src)/include
ccflags-y += -include linux/version.h
ccflags-y += -DCONFIG_DVB_TAS2101=1 -DCONFIG_DVB_TAS2101_MODULE=1
ccflags-y += -DCONFIG_MEDIA_TUNER_AV201X=1 -DCONFIG_MEDIA_TUNER_AV201X_MODULE=1
else
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
	depmod -a
endif
