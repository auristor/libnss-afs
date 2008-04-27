
/*****************************************************************************
 * libnss-afs (nss_afs.c)
 *
 * Copyright 2008, licensed under GNU Library General Public License (LGPL)
 * see COPYING file for details
 *
 * by Adam Megacz <megacz@hcoop.net>
 * derived from Frank Burkhardt's libnss_ptdb,
 * which was derived from Todd M. Lewis' libnss_pts
 *****************************************************************************/

/*
 *  If you are reading this code for the first time, read the rest of
 *  this comment block, then start at the bottom of the file and work
 *  your way upwards.
 *
 *  All functions which return an int use zero to signal success --
 *  except cpstr(), which returns zero on *failure*.  This should be
 *  fixed.
 *
 *  A note about memory allocation:
 *
 *    NSS plugins generally ought to work without attempting to call
 *    malloc() (which may fail).  Therefore, glibc allocates a buffer
 *    before calling NSS library functions, and passes that buffer to
 *    the NSS library; library functions store their results in the
 *    buffer and return pointers into that buffer.
 *
 *    The convention used throughout this library is to pass around a
 *    char** which points to a pointer to the first unused byte in the
 *    provided buffer, and a size_t* which points to an int indicating
 *    how many bytes are left between the char** and the end of the
 *    available region.
 */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <netinet/in.h>
#include <nss.h>
#include <pthread.h>
#include <pwd.h>
#include <rx/rx.h>
#include <rx/xdr.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <afs/afs.h>
#include <afs/afsutil.h>
#include <afs/cellconfig.h>
#include <afs/com_err.h>
#include <afs/param.h>
#include <afs/ptclient.h>
#include <afs/pterror.h>
#include <afs/stds.h>

#define HOMEDIR_AUTO      0
#define HOMEDIR_ADMINLINK 1
#define HOMEDIR_PREFIX    2
#define SHELL_BASH        0
#define SHELL_ADMINLINK   1
#define SHELL_USERLINK    2

#define AFS_MAGIC_ANONYMOUS_USERID 32766
#define MIN_PAG_GID                0x41000000L
#define MAX_PAG_GID                0x41FFFFFFL
#define MIN_OLDPAG_GID             0x3f00
#define MAX_OLDPAG_GID             0xff00

#define MAXCELLNAMELEN             256
#define MAXUSERNAMELEN             256

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

extern struct ubik_client *pruclient;

int  afs_initialized = 0;
char cellname[MAXCELLNAMELEN];
char homedir_prefix[MAXPATHLEN];
char cell_root[MAXPATHLEN];
int  homedir_prefix_len=0;
char homedirs_method=0;
char shells_method=0;

/**
 *  The cpstr() function copies a null-terminated string from str*
 *  (the first argument) into buf and updates both buf and buflen.  If
 *  the string would overflow the buffer, no action is taken.  The
 *  number of bytes copied is returned (zero indicates failure).
 */
int cpstr( char *str, char **buf, size_t *buflen) {
  int len = strlen(str);
  if ( len >= *buflen-1 ) return 0;
  strcpy(*buf,str);
  *buflen -= len + 1;
  *buf    += len + 1;
  return len;
}

/**
 * Look up the name corresponding to uid, store in buffer.
 */
enum nss_status ptsid2name(int uid, char **buffer, int *buflen) {
  int ret, i;
  idlist lid;
  namelist lnames;

  init_afs();

  if (uid==AFS_MAGIC_ANONYMOUS_USERID) {
    if (!cpstr("anonymous", buffer, buflen)) return NSS_STATUS_UNAVAIL;
    return NSS_STATUS_SUCCESS;
  }

  if (pthread_mutex_lock(&mutex)) return NSS_STATUS_UNAVAIL;
  
  lid.idlist_val = (afs_int32*)&uid;
  lid.idlist_len = 1;
  lnames.namelist_val = 0;
  lnames.namelist_len = 0;

  if (ubik_Call(PR_IDToName,pruclient,0,&lid,&lnames) != PRSUCCESS) {
    pthread_mutex_unlock(&mutex);
    return NSS_STATUS_UNAVAIL;
  }

  ret = NSS_STATUS_NOTFOUND;
  for (i=0;i<lnames.namelist_len;i++) {
    int delta = strlen(lnames.namelist_val[i]);
    if ( (delta < buflen) && islower(*(lnames.namelist_val[i])) ) {
      cpstr(lnames.namelist_val[i], buffer, buflen);
      ret = NSS_STATUS_SUCCESS;
    }
  }
  free(lnames.namelist_val);
  /* free(lid.idlist_val); */
  lid.idlist_val = 0;
  lid.idlist_len = 0;

  pthread_mutex_unlock(&mutex);
  return ret;
}

/**
 * Look up the uid corresponding to name in ptserver.
 */
enum nss_status ptsname2id(char *name, uid_t* uid) {
  int res;
  idlist lid;
  namelist lnames;
  char uname[MAXUSERNAMELEN];

  init_afs();
  
  if (!strcmp(name,"anonymous")) {
    *uid = AFS_MAGIC_ANONYMOUS_USERID;
    return NSS_STATUS_SUCCESS;
  }

  if (pthread_mutex_lock(&mutex)) return NSS_STATUS_UNAVAIL;

  lid.idlist_val = 0;
  lid.idlist_len = 0;
  lnames.namelist_val = (prname*)uname;
  // apparently ubik expects to be able to modify this?
  strncpy(uname, name, MAXUSERNAMELEN);
  lnames.namelist_len = 1;

  if (ubik_Call(PR_NameToID,pruclient,0,&lnames,&lid) != PRSUCCESS) {
    pthread_mutex_unlock(&mutex);
    return NSS_STATUS_UNAVAIL;
  }
  pthread_mutex_unlock(&mutex);

  res = (uid_t)lid.idlist_val[0];
  if (res == AFS_MAGIC_ANONYMOUS_USERID) return NSS_STATUS_NOTFOUND;
  *uid = res;
  return NSS_STATUS_SUCCESS;
}

/**
 *  Initialize the library; returns zero on success
 */
int init_afs() {
  FILE *thiscell;
  int len;
  struct stat statbuf;

  if (afs_initialized) {
    /* wait until /afs/@cell/ appears as a proxy for "the network is up" */
    if (stat(cell_root, &statbuf)) return -1;
    return 0;
  }
  
  if (pthread_mutex_lock(&mutex)) return -1;
  do {
    homedirs_method=HOMEDIR_PREFIX;
    shells_method=SHELL_USERLINK;

    len = snprintf(cellname, MAXCELLNAMELEN,
                   "%s/ThisCell", AFSDIR_CLIENT_ETC_DIRPATH);
    if (len < 0 || len >= MAXCELLNAMELEN) return -1;

    thiscell=fopen(cellname,"r");
    if (thiscell == NULL) break;
    len=fread(cellname,1,MAXCELLNAMELEN,thiscell);
    if (!feof(thiscell)) {
      // Cellname too long
      fclose(thiscell);
      strcpy(homedir_prefix,"/tmp/\0");
      homedir_prefix_len=5;
      break;
    }
    fclose(thiscell);

    if (cellname[len-1] == '\n') len--;
    cellname[len]='\0';

    /* wait until /afs/@cell/ appears as a proxy for "the network is up" */
    sprintf(cell_root,"/afs/%s/",cellname);
    if (stat(cell_root, &statbuf)) break;

    sprintf(homedir_prefix,"/afs/%s/user/",cellname);
    homedir_prefix_len=strlen(homedir_prefix);

    /* time out requests after 5 seconds to avoid hanging things */
    rx_SetRxDeadTime(5);    

    if (pr_Initialize(0L,AFSDIR_CLIENT_ETC_DIRPATH, 0)) break;
    
    afs_initialized = 1;
    pthread_mutex_unlock(&mutex);
    return 0;

  } while(0);
  pthread_mutex_unlock(&mutex);
  return -1;
}


/**
 * Retrieves the homedir for a given user; returns 0 on success.
 */
int get_homedir(char *name, char **buffer, size_t *buflen) {
  char buf[256];
  int temp;
  char *b;
  b=*buffer;
  switch (homedirs_method) {
    case HOMEDIR_PREFIX:
      homedir_prefix[homedir_prefix_len+0]=name[0];
      homedir_prefix[homedir_prefix_len+1]='/';
      homedir_prefix[homedir_prefix_len+2]=name[0];
      homedir_prefix[homedir_prefix_len+3]=name[1];
      homedir_prefix[homedir_prefix_len+4]='/';
      homedir_prefix[homedir_prefix_len+5]=0;
      strncpy(&homedir_prefix[homedir_prefix_len+5],name,40);
      if (! cpstr(homedir_prefix,buffer,buflen) ) return -1;
      break;
    case HOMEDIR_AUTO:
      homedir_prefix[homedir_prefix_len]=0;
      strncpy(&homedir_prefix[homedir_prefix_len],name,40);
      if (! cpstr(homedir_prefix,buffer,buflen) ) return -1;
      break;
    case HOMEDIR_ADMINLINK:
      if ( snprintf(buf,256,"/afs/%s/admin/homedirs/%s",cellname,name) > 0 ) {
        temp=readlink(buf,*buffer,*buflen);
        if ( temp > -1) {
          b[temp]=0;
          *buflen = *buflen - temp - 1;
          return -1;
        }
      }
      if (! cpstr("/tmp",buffer,buflen) ) return -1;
      break;
  }
  return 0;
}

/**
 * Retrieves the shell for a given user; returns 0 on success.
 */
int get_shell(char *name, char **buffer, size_t *buflen) {
  char buf[256];
  int temp;
  char *b;
  char* bufx = buf;
  int bufxlen = 256;
  b=*buffer;

  switch (shells_method) {
    case SHELL_BASH:
      break;

    case SHELL_ADMINLINK:
      if (snprintf(buf,256,"/afs/%s/admin/shells/%s",cellname,name)<=0) break;
      temp = readlink(buf,*buffer,*buflen);
      if (temp < 0) break;
      b[temp]=0;
      *buflen = *buflen - temp - 1;
      return 0;

    case SHELL_USERLINK:
      if (get_homedir(name, &bufx, &bufxlen)) break;
      if (strncpy(buf+strlen(buf),"/.loginshell",bufxlen)<=0) break;
      temp = readlink(buf,*buffer,*buflen);
      if (temp < 0) break;
      b[temp]=0;
      *buflen = *buflen - temp - 1;
      return 0;
  }
  if (! cpstr("/bin/bash",buffer,buflen) )
    return -1;
  return 0;
}


/*
 * This function is exported; glibc will invoke it in order to find
 * the name and list of members of a group specified by a numerical
 * groupid.
 */
enum nss_status _nss_afs_getgrgid_r (gid_t gid,
                                     struct group *result,
                                     char *buffer,
                                     size_t buflen,
                                     int *errnop) {
  int length;
  int showgid = 0;
  if (gid >= MIN_PAG_GID && gid <= MAX_PAG_GID) {
    showgid = gid-MIN_PAG_GID;
  } else if (gid >= MIN_OLDPAG_GID && gid <= MAX_OLDPAG_GID) {
    showgid = gid-MIN_OLDPAG_GID;
  } else {
    *errnop=ENOENT;
    return NSS_STATUS_NOTFOUND;
  }
  do {
    result->gr_gid=gid;

    result->gr_name=buffer;
    length=snprintf(buffer,buflen,"AfsPag-%x",showgid);
    
    if (length < 0) break;
    length += 1;
    buflen -= length;
    buffer += length;

    result->gr_passwd=buffer;

    if (!cpstr("x",&buffer,&buflen)) break;

    if (buflen < sizeof(char*)) break;
    result->gr_mem=buffer;
    result->gr_mem[0] = NULL;

    *errnop=errno;
    return NSS_STATUS_SUCCESS;

  } while(0);
  *errnop=ENOENT;
  return NSS_STATUS_UNAVAIL;
}

/**
 * A helper function to fill in the fields of "struct passwd"; used by
 * both _nss_afs_getpwuid_r() and _nss_afs_getpwnam_r().
 */
enum nss_status fill_result_buf(uid_t uid,
                                char* name,
                                struct passwd *result_buf,
                                char *buffer,
                                size_t buflen,
                                int *errnop) {
  result_buf->pw_name = name;
  do {
    /* set the password to "x" */
    result_buf->pw_passwd = buffer;
    if ( ! cpstr("x",&buffer, &buflen) ) break;

    /* the uid and gid are both the uid passed in */
    result_buf->pw_uid = uid;
    result_buf->pw_gid = 65534;

    /* make the gecos the same as the PTS name */
    result_buf->pw_gecos = buffer;
    if ( ! cpstr(result_buf->pw_name, &buffer, &buflen ) ) break;

    // Set the homedirectory
    result_buf->pw_dir = buffer;
    if ( get_homedir(result_buf->pw_name,&buffer,&buflen) ) break;

    // Set the login shell
    result_buf->pw_shell = buffer;
    if ( get_shell(result_buf->pw_name,&buffer,&buflen) ) break;

#ifdef LIMIT_USERNAME_CHARS
    if ( strlen(result_buf->pw_name) > LIMIT_USERNAME_CHARS ) {
      result_buf->pw_name[LIMIT_USERNAME_CHARS] = '\0';
      buflen = buflen + ( buffer - &result_buf->pw_name[LIMIT_USERNAME_CHARS+1] );
      buffer = &result_buf->pw_name[LIMIT_USERNAME_CHARS+1];
    }
#endif

    *errnop = errno;
    return NSS_STATUS_SUCCESS;
  } while(0);

  *errnop = ERANGE;
  return NSS_STATUS_UNAVAIL;
}


/**
 * This function is exported; glibc will invoke it in order to gather
 * the user information (userid, homedir, shell) associated with a
 * numerical userid.
 */
enum nss_status _nss_afs_getpwuid_r (uid_t uid,
                                     struct passwd *result_buf,
                                     char *buffer,
                                     size_t buflen,
                                     int *errnop) {
  int temp;
  char* name;

  if (init_afs()) return NSS_STATUS_UNAVAIL;

  name = buffer;
  temp = ptsid2name( uid, &buffer, &buflen);
  if (temp != NSS_STATUS_SUCCESS) {
    *errnop = ENOENT;
    return temp;
  }

  return fill_result_buf(uid, name, result_buf, buffer, buflen, errnop);
}

/**
 * This function is exported; glibc will invoke it in order to gather
 * the user information (userid, homedir, shell) associated with a
 * username.
 */
enum nss_status _nss_afs_getpwnam_r (char *name,
                                     struct passwd *result_buf,
                                     char *buffer,
                                     size_t buflen,
                                     int *errnop) {
  uid_t uid;
  int temp;

  if (init_afs()) return NSS_STATUS_UNAVAIL;

  temp = ptsname2id(name,&uid);
  if (temp != NSS_STATUS_SUCCESS) {
    *errnop = ENOENT;
    return temp;
  }

  return fill_result_buf(uid, name, result_buf, buffer, buflen, errnop);
}

