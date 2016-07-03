CC:=k1om-mpss-linux-gcc
OBJCOPY:=k1om-mpss-linux-objcopy
CFLAGS:=-nostdinc \
	   -std=c99 \
	   -O2 \
	   -g \
	   -mno-red-zone \
	   -fno-stack-protector \
	   -fomit-frame-pointer \
	   -U__linux__ \
	   -D__k1om__ \
	   -Iinclude \
	   -Iinclude/oldc \
	   -Iinclude/c \
	   -Iinclude/arch/k1om \
       -Wall \
	   -Wshadow \
	   -Wstrict-prototypes \
	   -Wold-style-definition \
	   -Wmissing-prototypes \
	   -Wmissing-declarations \
	   -Wno-unused-function \
	   -Wmissing-field-initializers \
	   -Wredundant-decls \
	   -Wno-packed-bitfield-compat \
	   -Wno-unused-but-set-variable \
	   -Werror \
       -Wl,-estartup_32 \
       -fPIC \
	   -mk1om \
	   -Wa,-march=k1om \
	   -m64 \
	   -fno-tree-vectorize \
	   -fno-builtin \
	   -nostdlib \
	   -mno-mmx \
	   -mno-sse \
	   -mno-sse2 \
	   -mno-sse3 \
	   -mno-sse4.1 \
	   -mno-sse4.2 \
	   -mno-sse4 \
	   -mno-sse4a \
	   -mno-3dnow


LINK_SCRIPT:=inky.lds

LDFLAGS:=-Wl,-N \
		-fno-builtin \
		-nostdlib \
		-Wl,-estartup_32 \
		-m64 \
		-Wl,-melf_k1om \
		-fPIC \
		-Wl,--fatal-warnings \
		-Wl,--build-id=none \
		-T $(LINK_SCRIPT)


all: inkythecat

multiboot/inky_mbgen: multiboot/inky_mbgen.c
	@echo "COMPILING MULTIBOOT GENERATOR..."
	$(MAKE) -C multiboot

patcher/inky_patcher:
	@echo "COMPILING K1OM PATCH PROGRAM..."
	$(MAKE) -C patcher
	
mbi.c: multiboot/inky_mbgen
	@echo "GENERATING MULTIBOOT ROUTINES..."
	multiboot/inky_mbgen k1om.cfg mbi.c

loader.o: loader.c
	$(CC) $(CFLAGS) -c $<

lib.o: lib.c
	$(CC) $(CFLAGS) -c $<

elf64.o: elf64.c
	$(CC) $(CFLAGS) -c $<

mbi.o: mbi.c
	$(CC) $(CFLAGS) -c $<

boot.o: boot.S
	$(CC) $(CFLAGS) -c $<

inky.bin: inky_elf
	$(OBJCOPY) -O binary -R .note -R .comment -S $< $@
  
inky_elf: boot.o loader.o lib.o elf64.o mbi.o 
	@echo "LINKING..."
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

inkythecat: inky.bin patcher/inky_patcher
	@echo "PATCHING..."
	patcher/inky_patcher inky.bin > $@

clean:
	@rm -f inkythecat inky.bin inky_elf mbi.c *.o
	$(MAKE) -C multiboot clean
	$(MAKE) -C patcher clean

.PHONY: all clean

