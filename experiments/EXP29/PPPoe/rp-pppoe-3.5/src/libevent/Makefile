# Generated automatically from Makefile.in by configure.
# Generated automatically from Makefile.in by configure.
# $Id: Makefile.in,v 1.3 2002/04/09 17:28:40 dfs Exp $
#
# Makefile for event-handling library
#
# Copyright 2002 Roaring Penguin Software Inc.
#
# This software may be distributed according to the terms of the GNU
# General Public License, version 2 or (at your option) any later version.
# LIC: GPL

OBJS=event.o event_tcp.o hash.o event_sig.o
SRCS=$(OBJS:.o=.c)
HDRS=event.h event_tcp.h eventpriv.h hash.h
CFLAGS+=-D__Linux__ -DDEBUG -g -I../../ppp-2.4.2/include -I/backup/products-cd/HHARM/arm9-edu/HHARM9-EDU/kernel/include -Wall -Wstrict-prototypes -ansi -pedantic -I..

all: libevent.a

libevent.a: $(OBJS)
	rm -f libevent.a
	ar -cq libevent.a $(OBJS)
	ranlib libevent.a

event.o: event.c $(HDRS)
	$(CC) $(CFLAGS) -c -o event.o event.c

hash.o: hash.c $(HDRS)
	$(CC) $(CFLAGS) -c -o hash.o hash.c

event_sig.o: event_sig.c $(HDRS)
	$(CC) $(CFLAGS) -c -o event_sig.o event_sig.c

event_tcp.o: event_tcp.c $(HDRS)
	$(CC) $(CFLAGS) -c -o event_tcp.o event_tcp.c

clean: FORCE
	rm -f *.a *.o *~

FORCE:

.phony: FORCE
