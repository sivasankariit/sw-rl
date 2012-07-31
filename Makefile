

obj-m += newrl.o

newrl-y := stats.o prl.o direct.o main.o
EXTRA_CFLAGS += -DDIRECT -O2

all:
	make -C ~/home/linux-3.5 M=`pwd`
	modinfo ./prl.ko

clean:
	rm *.o *.ko

