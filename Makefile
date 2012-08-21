

obj-m += newrl.o

newrl-y := stats.o prl.o netfilter.o main.o
EXTRA_CFLAGS += -DNETFILTER -O2

all:
	make -C /lib/modules/$(shell uname -r)/build M=`pwd`
	modinfo ./newrl.ko

default:
	make -C ~/home/linux-source-3.2.0 M=`pwd`
	modinfo ./newrl.ko

prod:
	make -C ~/home/linux-3.5 M=`pwd`
	modinfo ./newrl.ko

clean:
	rm *.o *.ko

