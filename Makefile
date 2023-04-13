CFLAGS=-I/usr/include

ifeq ($(shell uname), Linux)
	obj-m = unitxt.o
	CFLAGS_unitxt.o := -I/usr/include -Wno-error
else ifeq ($(shell uname), NetBSD)

endif

.PHONY: unitxt.c

all: unitxt.c
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules CFLAGS="${CFLAGS}"

unitxt.c:
	ib unitxt.c.ib

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
