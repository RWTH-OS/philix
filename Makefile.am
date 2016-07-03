AUTOMAKE_OPTIONS = foreign

AM_CFLAGS = -Wall -O2 

SUBDIRS = src

noinst_HEADERS =  include/phi_sbox.h \
				  include/phi_mmap.h \
				  include/phi_console.h \
				  include/phi_linux.h

EXTRA_DIST= inky \
			winky \
			boot \
			LICENSE.txt \
			README

boot: inky winky

inky: boot/inkythecat

boot/inkythecat:
	$(MAKE) -C inky
	cp inky/inkythecat boot

winky: boot/winkythecat

boot/winkythecat:
	$(MAKE) -C winky
	cp winky/winkythecat boot

clean-local:
	$(MAKE) -C winky clean
	$(MAKE) -C inky clean
	-rm -f boot/*

dist-hook:
	chmod -R u+w $(distdir)/winky $(distdir)/inky boot
	$(MAKE) -C $(distdir)/winky clean
	$(MAKE) -C $(distdir)/inky clean
	-rm -f $(distdir)/boot/*
