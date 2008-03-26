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
	char *buf;
	char *arg;
	int uid;
	int res;
	
	buf=buffer;
	if ( argc != 2 ) {
		printf("Usage: %s [name or id]\n\nAsks (a hopefully running) ptdbnssd for an AFSID or a PT-username, returns both.\n",argv[0]);
		exit(1);
	}
	arg=argv[1];
	if ( ( arg[0] > '0' ) && ( arg[0] < '9' ) ) {
		// NAME-Lookup
		uid=atoi(arg);
		res=ptsid2name(uid,&buf,&buflen);
		if ( res == NSS_STATUS_SUCCESS ) {
			printf("uid=%i name=%s\n",uid,buffer);
			//printf("homedir_method=%i, shell_method=%i\n",conf.homedirs_method,conf.shells_method);
			return 0;
		} else {
			if ( res == NSS_STATUS_NOTFOUND ) {
				printf("W: AFSID %i not found.\n",uid);
				return 1;
			} else {
				if ( res == NSS_STATUS_UNAVAIL ) {
					printf("E: Error talking to ptdbnssd.\n");
					return 2;
				}
			}
		}
	} else {
		buf=buffer;
		res=ptsname2id(arg,&uid);
		if ( res == NSS_STATUS_SUCCESS ) {
			printf("uid=%i name=%s\n",uid,arg);
			//printf("homedir_method=%i, shell_method=%i\n",conf.homedirs_method,conf.shells_method);
			return 0;
		} else {
			if ( res == NSS_STATUS_NOTFOUND ) {
				printf("W: PT-user '%s' not found.\n",buf);
				return 1;
			} else {
				if ( res == NSS_STATUS_UNAVAIL ) {
					printf("E: Error talking to ptdbnssd.\n");
					return 2;
				}
			}
		}
	}
}
