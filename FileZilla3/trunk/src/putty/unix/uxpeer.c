/*
 * Unix: wrapper for getsockopt(SO_PEERCRED), conditionalised on
 * appropriate autoconfery.
 */

#include "putty.h"

#include <sys/socket.h>

#ifdef HAVE_SO_PEERCRED
#define _GNU_SOURCE
#include <features.h>
#endif


int so_peercred(int fd, int *pid, int *uid, int *gid)
{
#ifdef HAVE_SO_PEERCRED
    struct ucred cr;
    socklen_t crlen = sizeof(cr);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &crlen) == 0) {
        *pid = cr.pid;
        *uid = cr.uid;
        *gid = cr.gid;
        return TRUE;
    }
#endif
    return FALSE;
}
