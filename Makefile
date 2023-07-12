obj-m += fingerprint.o
fingerprint-objs := fingerprint_main.o fingerprint_file.o
PWD = $(shell pwd)
KDIR = /lib/modules/$(shell uname -r)/build

default: modules

modules:
	sudo make -C $(KDIR) M=$(PWD) modules

clean:
	sudo make -C $(KDIR) M=$(PWD) clean