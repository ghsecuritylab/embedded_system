# Generated automatically from Makefile.in by configure.
# Generated automatically from Makefile.in by configure.
#***********************************************************************
#
# Makefile
#
# Makefile for GUI for Roaring Penguin's Linux user-space PPPoE client.
#
# Copyright (C) 2001 Roaring Penguin Software Inc.
#
# This program may be distributed according to the terms of the GNU
# General Public License, version 2 or (at your option) any later version.
#
# LIC: GPL
#
# $Id: Makefile.in,v 1.7 2002/04/09 17:28:38 dfs Exp $
#***********************************************************************
DEFINES=
prefix=../build
exec_prefix=${prefix}
mandir=${prefix}/man
install=/usr/bin/install -c
install_dir=/usr/bin/install -c -d
sbindir=${exec_prefix}/sbin
bindir=${exec_prefix}/bin
datadir=${prefix}/share

ADSL_START_PATH=${exec_prefix}/sbin/adsl-start
ADSL_STOP_PATH=${exec_prefix}/sbin/adsl-stop
ADSL_STATUS_PATH=${exec_prefix}/sbin/adsl-status

PATHS='-DADSL_START_PATH="$(ADSL_START_PATH)"' '-DADSL_STOP_PATH="$(ADSL_STOP_PATH)"' '-DADSL_STATUS_PATH="$(ADSL_STATUS_PATH)"'

CFLAGS= -D__Linux__ -DDEBUG -g -I../../ppp-2.4.2/include -I/backup/products-cd/HHARM/arm9-edu/HHARM9-EDU/kernel/include -Wall -Wstrict-prototypes -ansi -pedantic $(DEFINES) $(PATHS)

all: pppoe-wrapper
	@echo ""
	@echo "Type 'make install' as root to install the software."

pppoe-wrapper: wrapper.o
	gcc -o pppoe-wrapper wrapper.o

wrapper.o: wrapper.c
	gcc $(CFLAGS) -c -o wrapper.o wrapper.c

install: all
	-mkdir -p $(RPM_INSTALL_ROOT)$(sbindir)
	-mkdir -p $(RPM_INSTALL_ROOT)$(bindir)
	-mkdir -p $(RPM_INSTALL_ROOT)/etc/ppp/rp-pppoe-gui
	$(install) -m 4755 -s pppoe-wrapper $(RPM_INSTALL_ROOT)$(sbindir)
	$(install) -m 755 tkpppoe $(RPM_INSTALL_ROOT)$(bindir)
	-mkdir -p $(RPM_INSTALL_ROOT)$(mandir)/man1
	$(install) -m 644 pppoe-wrapper.1 $(RPM_INSTALL_ROOT)$(mandir)/man1
	$(install) -m 644 tkpppoe.1 $(RPM_INSTALL_ROOT)$(mandir)/man1
	-mkdir -p $(RPM_INSTALL_ROOT)$(datadir)/tkpppoe
	for i in tkpppoe.html mainwin-busy.png mainwin-nonroot.png mainwin.png props-advanced.png props-basic.png props-nic.png props-options.png; do \
		$(install) -m 644 html/$$i $(RPM_INSTALL_ROOT)$(datadir)/tkpppoe; \
	done
	for i in en.msg ja.msg; do \
		$(install) -m 644 $$i $(RPM_INSTALL_ROOT)$(datadir)/tkpppoe; \
	done

clean:
	rm -f *.o *~ pppoe-wrapper

distclean: clean
	rm -f Makefile tkpppoe

.PHONY: clean

.PHONY: distclean
