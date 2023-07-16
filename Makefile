obj-m += fingerprint.o
PWD = $(shell pwd)
KDIR = /lib/modules/$(shell uname -r)/build

default: modules

modules:
	sudo make -C $(KDIR) M=$(PWD) modules

clean:
	sudo make -C $(KDIR) M=$(PWD) clean

remove:
	sudo rmmod fingerprint.ko

insert:
	sudo insmod fingerprint.ko

rmins: remove insert

test: test.c
	gcc test.c -o test