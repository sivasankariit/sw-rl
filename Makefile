

obj-m += newrl.o

newrl-y := stats.o prl.o netfilter.o main.o
EXTRA_CFLAGS += -DNETFILTER -O2

all:
	make -C /lib/modules/$(shell uname -r)/build M=`pwd`
	modinfo ./prl.ko

clean:
	rm *.o *.ko

