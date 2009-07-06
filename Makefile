
SYS=$(shell uname)
CC=gcc
MAJORVERSION=2
MINORVERSION=0.0
LIBNAMES=libnss_afs.so.$(MAJORVERSION)
EXTRALIBS=-lresolv
AFSROOT=/usr

# LIMIT_USERNAME_CHARS=n will cut down usernames to
# a maximum of n characters
CFLAGS=-I$(AFSROOT)/include -DHAVE_NSS_H -DLIMIT_USERNAME_CHARS=20 -fPIC

all: $(LIBNAMES) nss_afs_test

#-lprot \
#	-lubik -lauth -lrxkad -lrxstat -lrx -ldes -lcom_err -laudit
# $(AFSROOT)/lib/afs/util.a

LDFLAGS=-L$(AFSROOT)/lib/afs -L$(AFSROOT)/lib -lafsauthent -lafsrpc -lpthread \
	 -lsys -lnsl $(EXTRALIBS) -g

nss_afs.o: nss_afs.c
	$(CC) $(CFLAGS) -c nss_afs.c

libnss_afs.so.$(MAJORVERSION): nss_afs.o
	$(CC) -shared -fPIC -o libnss_afs.so.$(MAJORVERSION) nss_afs.o \
	-Wl,-soname,libnss_afs.so.$(MAJORVERSION) $(LDFLAGS)

nss_afs_test: nss_afs.o nss_afs_test.c
	$(CC) -o nss_afs_test nss_afs_test.c nss_afs.o $(LDFLAGS)

install:
	mkdir -p $(DESTDIR)/lib/
	install -m 644 libnss_afs.so.$(MAJORVERSION) $(DESTDIR)/lib/libnss_afs.so.$(MAJORVERSION).$(MINORVERSION)
	ln -s libnss_afs.so.$(MAJORVERSION).$(MINORVERSION) $(DESTDIR)/lib/libnss_afs.so.$(MAJORVERSION)

clean:
	rm -f *.so.2 *.o $(LIBNAME) nss_afs_test
