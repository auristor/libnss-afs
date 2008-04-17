/*****************************************************************************
 * libnss-afs (nss_afs_test.c)
 *
 * Copyright 2008, licensed under GNU Library General Public License (LGPL)
 * see COPYING file for details
 *
 * by Adam Megacz <megacz@hcoop.net>
 * derived from Frank Burkhardt's libnss_ptdb,
 * which was derived from Todd M. Lewis' libnss_pts
 *****************************************************************************/

#include <nss.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/select.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

int main(int argc,char **argv,char **envp) {
  int buflen=1000;
  char buffer[buflen];
  char *buf, *arg, *name;
  int uid;
  int res;
	
  buf=buffer;
  if ( argc != 2 ) {
    printf("Usage: %s [name or id]\n\n",argv[0]);
    printf("Attempts lookup of a username or userid.\n");
    printf("Statically linked against nss_afs.c.\n");
    exit(1);
  }
  arg=argv[1];
  if ( ( arg[0] > '0' ) && ( arg[0] < '9' ) ) {
    uid=atoi(arg);
    name = buf;
    res=ptsid2name(uid,&buf,&buflen);
  } else {
    name = arg;
    res=ptsname2id(arg,&uid);
  }
  switch(res) {
  case NSS_STATUS_SUCCESS:
    printf("uid=%i name=%s\n",uid,name);
    break;
  case NSS_STATUS_NOTFOUND:
    printf("not found.\n");
    break;
  case NSS_STATUS_UNAVAIL:
    printf("unable to contact ptserver or library internal error.\n");
    break;
  }
}
