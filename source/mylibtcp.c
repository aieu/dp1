

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/time.h>   // FD_SETSIZE (earlier standards)
#include <sys/select.h> // FD_SETSIZE
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "mylibsock.h"
#include "mylibtcp.h"
#include "error.h"

#define	LISTENQ			(FD_SETSIZE) // max queue length of pending connections
#define SEL_TIMEOUT		(5)

int
tcp_connect(const char *host, const char *serv)
{
	int sockfd = 0;
	int n      = 0;
	struct addrinfo *res     = NULL;
	struct addrinfo *ressave = NULL;
	struct addrinfo	hints;

	fd_set wset;
	struct timeval tv;
	int result = 0;
	socklen_t sltresult = sizeof(result);

	bzero(&hints, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ( ( n = getaddrinfo(host, serv, &hints, &res) ) != 0 )
		err_quit("tcp_connect error for %s, %s: %s", host, serv, gai_strerror(n));

	ressave = res;

	do {
		sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if ( sockfd < 0 )
			continue;	/* ignore this one */

		/* when connecting, if server IP or port are wrong tcp timeout takes a
		 * very long time, to reduce this time I manage directly the connection
		 * timeout:
		 * - set socket to non-blocking mode
		 * - try to connect
		 * - wait for SEL_TIMEOUT for connect result if it doesn't connect immediately
		 * - after the timeout if socket is connected correctly return the socket
		 *   otherwise try next address
		 * NOTE: before returning set the socket to blocking mode
		 */

		/* set socket to non-blocking mode */
		if ( fcntl(sockfd, F_SETFL, (fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK)) < 0 ) {
			close(sockfd);
			err_quit("fcntl error");
		}


		/* try to connect */
		n = connect(sockfd, res->ai_addr, res->ai_addrlen);

		if ( n == 0 ) { // connect success

			/* set back socket to blocking mode */
			if ( fcntl(sockfd, F_SETFL, (fcntl(sockfd, F_GETFL, 0) & (~O_NONBLOCK))) < 0 ) {
				close(sockfd);
				err_quit("fcntl error");
			}
			break; // success

		} else if ( n < 0 && errno == EINPROGRESS ) { // connect error

			/* wait for SEL_TIMEOUT and check if socket correctly connected */
			FD_ZERO(&wset);
			FD_SET(sockfd, &wset);
			tv.tv_sec  = SEL_TIMEOUT;
			tv.tv_usec = 0;

			if ( Select(sockfd+1, NULL, &wset, NULL, &tv) == 1 ) {

				/* reset socket to blocking mode */
				if ( fcntl(sockfd, F_SETFL, (fcntl(sockfd, F_GETFL, 0) & (~O_NONBLOCK))) < 0 ) {
					close(sockfd);
					err_quit("fcntl error");
				}

				/* check if socket is connected correctly */
				result    = 1;
				sltresult = sizeof(result);
				if ( getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &result, &sltresult) < 0 ) {
					close(sockfd);
					err_quit("ERROR: could not read socket options");
				}

				if ( result == 0 )
					break; // success

			}
		}

		// if ( connect(sockfd, res->ai_addr, res->ai_addrlen) == 0 )
		// 	break;		/* success */

		close(sockfd);	/* ignore this one */
	} while ( (res = res->ai_next) != NULL );

	if ( res == NULL ) {	/* errno set from final connect() */
		freeaddrinfo(ressave);
		close(sockfd);
		err_sys("tcp_connect error for %s, %s", host, serv);
	}
	freeaddrinfo(ressave);

	return(sockfd);
}


int
tcp_listen(const char *host, const char *serv, socklen_t *addrlenp)
{
	int listenfd, n;
	struct addrinfo	hints;
	struct addrinfo *res     = NULL;
	struct addrinfo *ressave = NULL;
	const int on = 1;

	bzero(&hints, sizeof(struct addrinfo));
	hints.ai_flags    = AI_PASSIVE;
	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ( ( n = getaddrinfo(host, serv, &hints, &res) ) != 0 ) {
		err_ret("tcp_listen error for %s, %s: %s", host, serv, gai_strerror(n));
		return -1;
	 }
	ressave = res;

	do {
		listenfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if ( listenfd < 0 )
			continue; 		/* error, try next one */

#ifdef SO_REUSEADDR
		n = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
		if ( n < 0 ) {
			err_ret("ERROR: could not set socket options");
			freeaddrinfo(ressave);
			close(listenfd);
			return -1;
		}
#endif // SO_REUSEADDR

#ifdef SO_REUSEPORT
	n = setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
	if ( n < 0 ) {
		err_ret("ERROR: could not set socket options");
		freeaddrinfo(ressave);
		close(listenfd);
		return -1;
	}
#endif // SO_REUSEPORT

		if ( bind(listenfd, res->ai_addr, res->ai_addrlen) == 0 )
			break;			/* success */

		close(listenfd);	/* bind error, close and try next one */
	} while ( (res = res->ai_next) != NULL);

	if ( res == NULL ) { /* errno from final socket() or bind() */
		close(listenfd);
		freeaddrinfo(ressave);
		err_ret("tcp_listen error for %s, %s", host, serv);
		return -1;
	}

	if ( ( n = listen(listenfd, LISTENQ) ) < 0 ) {
		err_ret("ERROR: could not listen to socket");
		freeaddrinfo(ressave);
		close(listenfd);
		return -1;
	}

	if ( addrlenp )
		*addrlenp = res->ai_addrlen;	/* return size of protocol address */

	freeaddrinfo(ressave);

	return(listenfd);
}
