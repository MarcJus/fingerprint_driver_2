obj-m += fingerprint.o
PWD = $(shell pwd)

default: modules

modules:
	sudo make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules