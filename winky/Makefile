# A simple makefile for the dummy kernel
#
CC:=k1om-mpss-linux-gcc
LD:=k1om-mpss-linux-ld
AS:=k1om-mpss-linux-as

# the key options here are -fno-common and -ffreestanding
CFLAGS:= -O2 \
		 -march=k1om \
		 -nostdlib \
		 -Wall \
		 -fno-omit-frame-pointer \
		 -fno-common \
		 -ffreestanding \
		 -fno-stack-protector \
		 -fno-strict-aliasing \
		 -fno-optimize-sibling-calls \
		 -mno-red-zone \
		 -std=gnu99 \
		 -Wno-unused-variable \
		 -Wno-unused-function

LDFLAGS:= -z max-page-size=0x1000
		
# we use a custom link script for LD
LINKSCRIPT :=linker.lds

# the kernel name
KERNEL:=winkythecat
COBJS:=$(patsubst %.c, %.o, $(wildcard *.c))
SOBJS:=$(patsubst %.S, %.o, $(wildcard *.S))
OBJS:= $(COBJS) $(SOBJS)

all: $(KERNEL)

$(KERNEL): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -T $(LINKSCRIPT) -o $@ $^

boot.o: boot.S
	$(CC) $(CFLAGS) -c $<

%.o: %.c
	$(CC) $(CFLAGS) -c $<


clean:
	@rm -f $(OBJS) $(KERNEL)
