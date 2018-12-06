

#ifndef _MYLIBSOCK_H
#define _MYLIBSOCK_H




ssize_t	 writen(int, const void *, size_t);
ssize_t	 Writen(int, void *, size_t);

ssize_t	 readn(int, void *, size_t);
ssize_t	 Readn(int, void *, size_t);

int Select(int nfds,
           fd_set *readfds,
           fd_set *writefds,
           fd_set *exceptfds,
           struct timeval *timeout);

#endif // _MYLIBSOCK_H
