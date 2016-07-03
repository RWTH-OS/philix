
all: inky_mbgen

inky_mbgen: inky_mbgen.c
	@echo "THE MULTIBOOT"
	gcc -o $@ -std=gnu99 $<

clean: 
	@rm -f inky_mbgen


.PHONY: clean all

