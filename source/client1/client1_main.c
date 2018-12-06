/** ---------------------------------------------------------------------------
 * Client1
 *
 * @author dcr
 * ----------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>      // isspace()
#include <sys/time.h>   // time_t
#include <time.h>       // ctime()
#include <netinet/in.h> // htonl()..
#include <inttypes.h>   // PRIyXX
#include <sys/types.h>  // getsockopt(), pid_t
#include <sys/socket.h> // getsockopt()
#include <limits.h>     // NAME_MAX

#include "../error.h"
#include "../mylibsock.h"
#include "../mylibtcp.h"

#define BUF_MAX         (NAME_MAX+7) // 255 + 6 chars + '\0'
#define SO_RCVBUF_MAX   (8120)       // above 8120 gains in performance are negligible
#define SEL_TIMEOUT     (5)          // select() timeout
#define SEL_ATTEMPTS    (2)          // after SEL_ATTEMPTS select() timeouts client closes connection

/* FUCNTIONS PROTOTYPES */
void 	die_from_err(int sockfd, uint8_t* buf);
char*	str_trim(char* str, const size_t slen);
int 	get_SO_RCVBUF(int sock);
ssize_t download_file(const int sockfd, const char* filename, \
			  		  const uint32_t file_size, const int buf_size);


int main (int argc, char *argv[])
{
	signed int sockfd = 0;       // socket
	char buf[BUF_MAX];     // 255 + 6 chars + '\0'

	int rcvbuflen = SO_RCVBUF_MAX; // socket receive buffer length

	char          *filename = NULL; // filename to transfer
	unsigned char *rbuf     = NULL; // receive-buffer (uint8_t)

	uint32_t file_len_h = 0; // file size (host byte-order), NOTE: only files < 2^32 Bytes (~4GB)
	time_t   file_ts    = 0; // file timestamp, NOTE: might be defined as uint64_t

	int slen = 0;
	int i    = 0;


	if ( argc < 4 )
		err_quit("ERROR: use: %s <server_IP_address> <server_port> <files>", \
				  argv[0]);

	/* create socket and connect to server */
	/* NOTE: will try to estabilish a connection for no longer than 5s for each
	 * server address returned by getaddrinfo(). (see mylibtcp.c) */
	sockfd = tcp_connect(argv[1], argv[2]);

	/* get socket receive buffer length */
	rcvbuflen = get_SO_RCVBUF(sockfd);

	/* prepare receive buffer */
	if ( ( rbuf = calloc(rcvbuflen, sizeof(unsigned char)) ) == NULL ) {
		err_ret("ERROR: could not allocate receive buffer");
		die_from_err(sockfd, NULL);
	}

	/**
	 * start requesting files
	 */
	buf[0] = '\0';
	for ( i = 3; i < argc ; ++i ) {

		filename = str_trim(argv[i], strnlen(argv[i], NAME_MAX));

		/* prepare request for server */
		slen = snprintf(buf, BUF_MAX, "GET %s\r\n", filename);
		if ( slen < 0 || slen >= BUF_MAX ) {
			err_msg("ERROR: filename too long: \n\"%s\".", filename);
			continue; // skip this filename
		}

		/* send request to server */
		if ( ( Writen(sockfd, buf, slen) ) < 0 )
			die_from_err(sockfd, rbuf);

		/* get and check reply */
		if ( ( Readn(sockfd, rbuf, 1) ) <= 0 )
			die_from_err(sockfd, rbuf);

		switch ( rbuf[0] ) {

			case '+': // server found the file

				if ( ( Readn(sockfd, rbuf, 12) ) <= 0 )	// [O][K][\r][\n][B]{4}[T]{4}
					die_from_err(sockfd, rbuf);

				file_len_h 	= ntohl(*((uint32_t *) (rbuf+4)));
				file_ts		= ntohl(*((uint32_t *) (rbuf+8)));

				if ( ( download_file(sockfd, filename, file_len_h, rcvbuflen) ) < 0 )
					die_from_err(sockfd, rbuf);

				printf("Written %"PRIu32" bytes in file \"%s\".\n", file_len_h, filename);
				printf("Last modified: %s\n", ctime(&file_ts));
				break;

			case '-': // server side error (file not found, illegal command...)

				if ( ( Readn(sockfd, rbuf, 5) ) <= 0 ) // [E][R][R][\r][\n]
					die_from_err(sockfd, rbuf);
				else {
					rbuf[5] = '\0';
					err_msg("ERROR: server returned error: %s", rbuf);
					}
				break;

			default: // could not understand message from server

				err_msg("ERROR: wrong data format received from server.");
				die_from_err(sockfd, rbuf);
				break;
		}
	}

	/* send "QUIT" message to server */
	if ( snprintf(buf, 7, "QUIT\r\n") < 0 )
		err_msg("ERROR: snprintf error."); // shouldn't really happen
	else if ( ( Writen(sockfd, buf, slen) ) < 0 )
			die_from_err(sockfd, rbuf);

	close(sockfd);
	free(rbuf);
	exit(0);
}


void
die_from_err(int sockfd, uint8_t* buf)
{
	err_msg("An error occurred. Closing connection.");
	close(sockfd);

	if (buf != NULL)
		free(buf);

	exit(-1);
}


/**
 * @brief Manages file transfer from receiver buffer to file-system file.
 *
 * @param sockfd		Opened socked where to read
 * @param filename		Name of the file to be created
 * @param file_size		Size of the file to be received
 * @param buf_size		Default receiver buffer size
 *
 * @return	 1 if OK
 * @return	-1 on error
 * @return	-2 on file-system error
 */
ssize_t
download_file(const int sockfd,
			  const char* filename,
			  const uint32_t file_size,
			  const int buf_size)
{
	FILE *fp;

	uint8_t	*buf    = NULL;
	int buflen = ( ( file_size > ((uint32_t) buf_size) ) ? buf_size : file_size );
	uint32_t bytesToBeRead = file_size;

	int n      = 0;
	int rbytes = 0; // read bytes

	int err		 = 0;
	int sys_err  = 0;
	int attempts = 0;

	struct timeval tv;
	fd_set rset;


	if ( (filename == NULL) || (buf_size == 0) ) {
		err_msg("ERROR: wrong parameters.");
		return -1;
	}

	/* prepare buffer */
	if ( ( buf = calloc(buflen, sizeof(uint8_t)) ) == NULL ) { // note: if (buflen > SIZE_MAX) malloc returns NULL
		err_ret("ERROR: could not allocate buffer");
		return -2;
	}
	/* open file to be written */
	if ( ( fp = fopen(filename, "wb") ) == NULL ) {
		err_ret("ERROR: could not open file \"%s\"", filename);
		return -2;
	}

	FD_ZERO(&rset);
	sys_err  = 0;
	attempts = 0;
	rbytes   = 0;
	bytesToBeRead = file_size;
	while ( ( bytesToBeRead > 0 ) && ( attempts < SEL_ATTEMPTS ) ) {

		/* select setup */
		FD_SET(sockfd, &rset);
		tv.tv_sec  = SEL_TIMEOUT;
		tv.tv_usec = 0;

		err = Select(sockfd + 1, &rset, NULL, NULL, &tv);
		if ( err < 0 )
			break; // select error, (bytesToBeRead  still > 0, function returns error)
		if ( err == 0 ) {
			++attempts;
			continue; // select timeout, can occur max #SEL_ATTEMPTS times
		}

		if ( FD_ISSET(sockfd, &rset) ) {

			/* check how much to read */
			n      = ( ( bytesToBeRead > ((uint32_t) buflen) ) ? buflen : bytesToBeRead ); // NOTE: buflen is always < INT_MAX
			rbytes = 0;

			/* read bytes from socket */
			if ( ( rbytes = Readn(sockfd, buf, n) ) <= 0 )
				break; // bytesToBeRead will be > 0 and function will return -1

			/* write bytes on file */
			if ( ( fwrite(buf, sizeof(uint8_t), rbytes, fp) ) < 0 ) {
				err_ret("ERROR: cannot write file\"%s\"", filename);
				sys_err = 1;
				break;
			}
			if ( ( fflush(fp) ) == EOF ) {
				err_ret("ERROR: cannot sync file\"%s\"", filename);
				sys_err = 1;
				break;
			}

			bytesToBeRead -= rbytes; // update remaining bytes to be transferred
			attempts       = 0;      // reset attempts
		}
	}

	free(buf);
	fclose(fp);

	if ( attempts == SEL_ATTEMPTS )
		err_msg("ERROR: server is taking too much time to reply.");

	if ( sys_err )
		return -2;

	return (bytesToBeRead == 0 ? 1 : -1);
}


char *
str_trim(char* str, const size_t slen)
{
	size_t len 	 = slen;
    char *frontp = str;
    char *endp   = NULL;

    if ( str == NULL )
		return NULL;

	if ( str[0] == '\0' )
		return str;

    endp = str + len;

    /* Move the front and back pointers to address the first non-whitespace
     * characters from each end.
     */
    while ( isspace((unsigned char) *frontp) ) { ++frontp; }
    if ( endp != frontp )
        while ( isspace((unsigned char) *(--endp)) && endp != frontp ) {}

    if ( str + len - 1 != endp )
        *(endp + 1) = '\0';
    else if ( frontp != str &&  endp == frontp )
        *str = '\0';

    /* Shift the string so that it starts at str so that if it's dynamically
     * allocated, we can still free it on the returned pointer.  Note the reuse
     * of endp to mean the front of the string buffer now.
     */
    endp = str;
    if ( frontp != str ) {
	    while ( *frontp ) { *endp++ = *frontp++; }
	    *endp = '\0';
    }

    return str;
}

int
get_SO_RCVBUF(int sock)
{
	int 	  opt_rbl = SO_RCVBUF_MAX;	// socket receive buffer length
	socklen_t opt_len = 0;				// option lenght

#ifdef SO_RCVBUF
	opt_len = sizeof(opt_rbl);
	if ( ( getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &opt_rbl, &opt_len) ) < 0 ) {
		err_ret("ERROR: could not get socket options");
		close(sock);
		exit(1);
	}
#else
	opt_rbl = SO_RCVBUF_MAX;
#endif // SO_RCVBUF

	return opt_rbl;
}
