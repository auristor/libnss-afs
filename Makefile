
SYS=$(shell uname)
CC=gcc
LIBNAMES=libnss_afs.so.2
EXTRALIBS=-lresolv
AFSROOT=/usr

# LIMIT_USERNAME_CHARS=n will cut down usernames to
# a maximum of n characters
CFLAGS=-I$(AFSROOT)/include -DHAVE_NSS_H -DLIMIT_USERNAME_CHARS=20

all: $(LIBNAMES) nss_afs_test

LDFLAGS=-L$(AFSROOT)/lib/afs -L$(AFSROOT)/lib -lprot \
	-lubik -lauth -lrxkad -lrxstat -lrx -llwp -ldes -lcom_err -laudit \
	$(AFSROOT)/lib/afs/util.a -lsys -lnsl $(EXTRALIBS)

nss_afs.o: nss_afs.c
	$(CC) $(CFLAGS) -c nss_afs.c

libnss_afs.so.2: nss_afs.o
	$(CC) -shared -o libnss_afs.so.2 nss_afs.o \
	-Wl,-soname,libnss_afs.so.2 $(LDFLAGS)

nss_afs_test: nss_afs.o nss_afs_test.c
	$(CC) -o nss_afs_test nss_afs_test.c nss_afs.o $(LDFLAGS)

install:
	mkdir -p $(DESTDIR)/lib/
	install -m 644 libnss_afs.so.2 $(DESTDIR)/lib/
	install -m 755 nss_afs_test $(DESTDIR)/usr/bin/nss_afs_test

clean:
	rm -f *.so.2 *.o $(LIBNAME) nss_afs_test
