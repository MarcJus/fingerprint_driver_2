obj-m += fingerprint.o
PWD = $(shell pwd)
KDIR = /lib/modules/$(shell uname -r)/build

default: modules

modules:
	sudo make -C $(KDIR) M=$(PWD) modules

clean:
	sudo make -C $(KDIR) M=$(PWD) clean

test: test.c
	gcc test.c -o test