
#include <errno.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "error.h"
#include "mylibsock.h"


ssize_t
writen(int fd, const void *vptr, size_t n)
{
	size_t		nleft    = n;
	ssize_t		nwritten = 0;
	const char	*ptr     = vptr;

	ptr = vptr;
	nleft = n;
	while ( nleft > 0 ) {
		if ( ( nwritten = write(fd, ptr, nleft) ) <= 0) {

			if ( (nwritten < 0) && (errno == EINTR) )
				nwritten = 0;		/* and call write() again */
			else
				return(-1);			/* error */
		}

		nleft -= nwritten;
		ptr   += nwritten;
	}
	return(n);
}

ssize_t
Writen(int fd, void *ptr, size_t nbytes)
{
	ssize_t n = 0;

	if ( ( n = writen(fd, ptr, nbytes) ) != nbytes) {
		err_ret("ERROR socket [%d]", fd);
		return -1;
	}

	return n;
}

ssize_t
readn(int fd, void *vptr, size_t n)
{
	size_t	nleft = n;
	ssize_t	nread = 0;
	char	*ptr  = vptr;

	ptr = vptr;
	nleft = n;
	while ( nleft > 0 ) {
		if ( ( nread = read(fd, ptr, nleft) ) < 0) {
			if ( errno == EINTR )
				nread = 0;		/* and call read() again */
			else
				return(-1);
		} else if ( nread == 0 )
			break;				/* EOF */

		nleft -= nread;
		ptr   += nread;
	}
	return(n - nleft);		/* return >= 0 */
}

ssize_t
Readn(int fd, void *ptr, size_t nbytes)
{
	ssize_t n = 0;

#ifndef MSG_WAITALL
	if ( ( n = readn(fd, ptr, nbytes) ) < 0)
		err_ret("ERROR socket [%d]", fd);
#else
	if ( ( n = recv(fd, ptr, nbytes, MSG_WAITALL) ) < 0 )
		err_ret("ERROR socket [%d]", fd);
#endif // MSG_WAITALL

	if ( n == 0 )
		err_msg("ERROR socket [%d]: Connection reset by peer.", fd);

	return(n);
}



int Select(int nfds,
           fd_set *readfds,
           fd_set *writefds,
           fd_set *exceptfds,
           struct timeval *timeout)
{
    int n;

    for ( ; ; ) {
        n = select(nfds, readfds, writefds, exceptfds, timeout);

        if ( n < 0 ) {
            if ( errno == EINTR )   // interrupted by signal
                continue;
            else                    // interrupted by error
                err_ret("ERROR: select returned an error");
        }

        return n;
    }

    /* should never get here */
    return n;
}
