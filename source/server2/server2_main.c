/** ---------------------------------------------------------------------------
 * Server2 - Concurrent server
 *
 * @author dcr
 * ----------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>      // errno
#include <string.h>     // snprintf() memcpy()
#include <limits.h>     // NAME_MAX
#include <ctype.h>      // isspace()
#include <netinet/in.h> // accept()
#include <sys/stat.h>   // struct stat
#include <sys/wait.h>   // wait()
#include <sys/time.h>   // FD_SET().. (earlier standards)
#include <sys/select.h> // FD_SET()..
#include <sys/types.h>  // getsockopt(), pid_t
#include <sys/socket.h> // getsockopt()

#include "../error.h"
#include "../mysignal.h"
#include "../mylibsock.h"
#include "../mylibtcp.h"
#include "../myclients.h"


#ifndef CHILD_MAX
#define CHILD_MAX 128
#endif

#define BUF_MAX			(NAME_MAX+7)
#define SO_SNDBUF_MAX	(8120)
#define SEL_TIMEOUT		(10)
#define ATTEMPTS_MAX	(2)

/* FUCNTIONS PROTOTYPES */
int     serve_client_rd(int client_socket, struct sclient **client);
int     serve_client_wr(int client_socket, struct sclient **client, \
						int buflen);
int     get_info_file(char* filename, uint32_t* ts, uint32_t* size);
int 	get_SO_SNDBUF(int sock);
void 	init_server(struct sclient **client);

void handle_SIGINT(int sig);
void handle_SIGCHLD(int sig);

/* GLOBAL VARIABLES */
static volatile pid_t childs[CHILD_MAX];
static volatile int nchilds;
static pid_t dadpid;


int main (int argc, char *argv[])
{
	signed int listen_socket = 0; // listening socket for new connections
	signed int new_socket;
	unsigned int attempts = 0;

	pid_t new_pid;

	struct sclient *client[1];

	unsigned int sndbuflen     = SO_SNDBUF_MAX; // socket send buffer size
	socklen_t addrlen = 0;

	signed int n = 0;
	fd_set active_rset, active_wset;
	fd_set ready_rset, ready_wset;
	struct timeval tv;

	if ( argc < 2 )
		err_quit("ERROR - usage: %s <server port>", argv[0]);

	dadpid = getpid();
	Signal(SIGINT, handle_SIGINT);
	Signal(SIGCHLD, handle_SIGCHLD);

	init_server(client); // initialize variables

	if ( ( listen_socket = tcp_listen(NULL, argv[1], &addrlen) ) < 0 )
		exit(-1);

	/* get socket options rcvbuflen */
	sndbuflen = get_SO_SNDBUF(listen_socket);

	for ( ; ; ) {

		new_socket = accept(listen_socket, NULL, NULL);
		if ( new_socket < 0 ) {
			err_ret("ERROR: could not accept client");
			continue;
		}

		if ( ( new_pid = fork() ) < 0 ) {

			/* ERROR */
			err_ret("ERROR: fork() failed");
	    	close(new_socket);

		} else if ( new_pid > 0 ) {

			/* parent process */
			close(new_socket);

			childs[nchilds] = new_pid;
			++nchilds;

		} else {

			/* child process */
			close(listen_socket);

			if ( ( add_client(0, client, NULL) ) < 0 ) {
				err_msg("ERROR: could not add client.");
				close(new_socket);
				exit(-1);
			}

			/* initialize sets */
			FD_ZERO(&active_rset);
			FD_ZERO(&active_wset);
			FD_SET(new_socket, &active_rset);

			for ( attempts = 0; attempts < ATTEMPTS_MAX ; ) {

				/* select active socket */
				memcpy((char *) &ready_rset, (char *) &active_rset, sizeof(fd_set));
				memcpy((char *) &ready_wset, (char *) &active_wset, sizeof(fd_set));
				/* initialize time */
				tv.tv_sec  = SEL_TIMEOUT;
				tv.tv_usec = 0;

				n = Select(FD_SETSIZE, &ready_rset, &ready_wset, NULL, &tv);
				if ( n < 0 ) {
					rm_client(0, client, NULL);
					close(new_socket);
					exit(-1); // select failed
				}

				if ( n == 0 ) {
					++attempts;
					continue;
				}

				if ( FD_ISSET(new_socket, &ready_rset) ) {

					attempts = 0;
					switch ( serve_client_rd(new_socket, client) ) {

						case 2: // client received all files
							rm_client(0, client, NULL);
							close(new_socket);
							exit(0);
							break;
						case 1: // OK
							FD_SET(new_socket, &active_rset);
							break;
						case 0: // OK, there's data to be sent to client
							FD_SET(new_socket, &active_rset);
							FD_SET(new_socket, &active_wset);
							break;
						case -1: // error
							rm_client(0, client, NULL);
							close(new_socket);
							exit(-1);
							break;
						default:
							/* should never get here */
							close(new_socket);
							rm_client(0, client, NULL);
							exit(-1);
							break;
					}

				} else if ( FD_ISSET(new_socket, &ready_wset) ) {

					attempts = 0;
					switch ( serve_client_wr(new_socket, client, sndbuflen) ) {

						case 1: // OK, all data sent to client
							FD_SET(new_socket, &active_rset);
							FD_CLR(new_socket, &active_wset);
							break;

						case 0: // Ok, more data to be sent to client
							FD_SET(new_socket, &active_rset);
							FD_SET(new_socket, &active_wset);
							break;

						case -1: // funciton error
							FD_CLR(new_socket, &active_rset);
							FD_CLR(new_socket, &active_wset);
							rm_client(0, client, NULL);
							close(new_socket);
							exit(-1);
							break;

						default: // case -2: // system error
							close(new_socket);
							rm_client(0, client, NULL);
							exit(-1);
							break;
					}
				}
			}
			rm_client(0, client, NULL);
			exit(-1); // because attempts >= ATTEMPTS_MAX
		}
	}

	/* should never get here */
	return -1;
}


/**
 * @brief Serves a client reading what he has sent
 *
 * @param csock		Client socket
 * @param client	Client info
 *
 * @return  2 if client requested to close connection
 * @return  1 if OK,
 * @return  0 if OK and there is data to be sent to the client,
 * @return -1 on error
 */
int
serve_client_rd(int client_socket,
				struct sclient **client)
{
	int cid   = 0; // client ID, only one client in this case
	int fnlen = 0; // filename length
	char inbuf[BUF_MAX];
	int there_is_data_to_send = 0;


	/* read data from client */
	if ( (Readn(client_socket, inbuf, 4)) <= 0 )
		return -1; // remove client and exit

	/* identify command received */
	if ( strncmp((char*) inbuf, "QUIT", 4) == 0 ) {
		Readn(client_socket, inbuf, 2); // consume '\r\n'
		return 2; // remove client and exit gracefully
	}

	if ( strncmp((char*) inbuf, "GET ", 4) != 0 ) {
		/* wrong command */
		client[cid]->sendError = 1;
		return 0;

	} else {

		/* get file name */
		for ( fnlen = 0; fnlen < BUF_MAX ; fnlen++ ) {
			if ( (Readn(client_socket, inbuf+fnlen, 1)) <= 0 )
				return -1;

			if ( fnlen < 2 ) continue;	// filename len at least 1

			if ( (inbuf[fnlen-1] == '\r') && (inbuf[fnlen] == '\n') ){
				inbuf[fnlen-1] = '\0';
				break;
			}
		}
		inbuf[fnlen] = '\0'; // if fnlen == BUF_MAX

		/* if file exists add it to files waiting to be uploaded */
		if ( access(inbuf, F_OK) != -1 ) {

			if ( ( add_file_client(inbuf, fnlen, client[cid]) ) < 0 ) {
				client[cid]->sendError = 1;
				return 0; // notify user there was an error (ERR)
			}

			there_is_data_to_send = 1;

		} else {
			client[cid]->sendError = 1;
			return 0; // notify user 'file not found' (ERR)
		}

		if ( there_is_data_to_send )
			return 0;
	}

	/* should never get here */
	return -1;
}


/**
 * @brief Serves a client sending him data.
 *
 * @param csock		Client socket
 * @param client	Client info
 * @param buflen	Send buffer lenght
 *
 * @return  1 if OK and all data sent to client,
 * @return  0 if OK and there is data to be sent to the client,
 * @return -1 on function error (close client connection)
 * @return -2 on system error (server shutdown)
 */
int
serve_client_wr(int client_socket,
				struct sclient **client,
				int buflen)
{
	int cid        = 0; // client ID, only one client in this case

	FILE     **fp    = &(client[cid]->cfp);
	int      sError  = client[cid]->sendError;
	uint32_t *btbwcf = &(client[cid]->bytesToBeWrittenCF);

	char* filename     = NULL;
	uint32_t file_ts   = 0; // file timestamp
	uint32_t file_size = 0; // file size

	unsigned char *outbuf = NULL;
	int n    = 0;
	int serr = 0;

	if ( ( outbuf = calloc(buflen, sizeof(unsigned char)) ) == NULL ){
		err_ret("ERROR: could not malloc buffer");
		return -2;
	}

	/* client must be notified of an error, connection will be closed */
	if ( sError ) {

		if ( ( snprintf((char*) outbuf, 7, "-ERR\r\n") ) < 0 ) {
			free(outbuf);
			return -2; // sys error
		} else {
			Writen(client_socket, outbuf, 6);
		}

		free(outbuf);
		return -1; // user will be deleted, process terminated
	}


	if ( (*fp) != NULL ) {

		/* if a file was already being uploaded,read new data from file
		 * and send it
		 */
		n = ( (*btbwcf) < ((uint32_t) buflen) ? (*btbwcf) : buflen ); // NOTE: buflen is always < INT_MAX

		/* read new data from file */
		if ( (serr = fread(outbuf, sizeof(unsigned char), n, *fp)) < n ) {
			if ( feof(*fp) || ferror(*fp) ) {
				err_msg("ERROR: cannot read file.");
				client[cid]->sendError = 1;
				free(outbuf);
				return 0; // error reading file, notify user
			} else {
				free(outbuf);
				return -2; // sys error
			}
		}

		/* upload new data */
		if ( (Writen(client_socket, outbuf, serr)) < 0 ) {
			free(outbuf);
			return -1; // user will be deleted, process terminated
		}

		*btbwcf -= serr; // update n. of Bytes To Be Written from Current File

	} else {

		/* if no file was being uploaded
		 * - read files list and retrieve next filename
		 * - read size and last modified timestamp for file
		 * - open file and send header + first bytes from file
		 */

		/* get next file name to send to client */
		if ( ( filename = get_next_file_client(client[cid]) ) == NULL ) {
			free(outbuf);
			return 1; // sent all client-requested files till now
		}

		/* read file size and timestamp */
		if ( ( get_info_file(filename, &file_ts, &file_size) ) < 0 ){
			err_msg("ERROR: file size or timestamp too big.");
			client[cid]->sendError = 1;
			free(outbuf);
			return 0; // file may be bigger than 2^32, there would be overflow
		}
		*btbwcf = file_size; // save file size into client structure

		/* open file */
		if ( ( (*fp) = fopen(filename, "rb") ) == NULL ) {
			err_ret("ERROR: could not open file\"%s\"", filename);
			client[cid]->sendError = 1;
			free(outbuf);
			return 0;
		}

		/* prepare response header */
		if ( snprintf((char*) outbuf, 6,"+OK\r\n") < 0 ) {
			free(outbuf);
			return -2; // sys error
		}

		*((uint32_t*) (outbuf+5)) = htonl(file_size);
		*((uint32_t*) (outbuf+9)) = htonl(file_ts);

		/* read also some bytes from file */
		n = ( (*btbwcf) < ((uint32_t) (buflen-13)) ? (*btbwcf) : (buflen-13) );
		if ( (serr = fread(outbuf+13, sizeof(unsigned char), n, *fp) ) < n ) {
			if ( feof(*fp) || ferror(*fp) ) {
				err_msg("ERROR: cannot read file.");
				client[cid]->sendError = 1;
				free(outbuf);
				return 0; // error reading file, notify user
			} else {
				free(outbuf);
				return -2;
			}
		}
		/* send data to client */
		if ( ( Writen(client_socket, outbuf, n+13) ) < 0 ) {
			free(outbuf);
			return -1; // user will be deleted, process terminated
		}

		*btbwcf -= serr; // update n. of Bytes To Be Written in Current File
	}

	free(outbuf);

	/* if all file was sent, check if there is another file to send */
	if ( (*btbwcf) == 0 ) {
		fclose(*fp);
		*fp = NULL;

		if ( ( rm_head_file_client(client[cid]) ) < 0 )
			return -1; // user will be deleted, process terminated

		if ( there_are_more_files(client[cid]) )
			return 0; // more data to send to user
	 	else
			return 1; // no more data to send to user

	} else
		return 0; // there is still data to be sent from current file

	/* should never get here */
	return -1;
}


/**
 * @brief Gets the default length of the output socket buffer
 *
 * @param sock		server's listening socket
 *
 * @return	output buffer length
 */
int
get_SO_SNDBUF(int sock)
{
	int 	  opt_sbl = SO_SNDBUF_MAX;
	socklen_t opt_len = 0;

#ifdef SO_SNDBUF
	opt_len = sizeof(opt_sbl);
	if ( ( getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &opt_sbl, &opt_len) ) < 0 ) {
		err_ret("ERROR: could not get socket options");
		close(sock);
		exit(1);
	}
#else
	opt_sbl = SO_SNDBUF_MAX;
#endif // SO_SNDBUF

	return opt_sbl;
}



/**
 * @brief Retrieves file size and last modified timestamp
 *
 * @param filename			file name
 * @param ts				reference to file timestamp
 * @param size				reference to file size
 *
 * @return	 0 if OK
 * @return	-1 on error
 */
int
get_info_file(char* filename, uint32_t* ts, uint32_t* size)
{
	struct stat sfile;

	if ( (stat(filename, &sfile)) < 0 ) {
		err_ret("ERROR: could not get file stat");
		return -1;
	}

	if ( ( sfile.st_size > UINT32_MAX ) || ( sfile.st_mtime > UINT32_MAX ) )
		return -1; // file size > 4GB or timestamp > int32_t max value

	*ts		= sfile.st_mtime;
	*size 	= sfile.st_size;
	return 0;
}

void
init_server(struct sclient **client)
{
	int i = 0;

	client[0] = NULL;

	for ( i = 0; i < CHILD_MAX; i++ )
		childs[i] = 0;

	nchilds = 0;

	return;
}


void
handle_SIGINT(int sig)
{
	int i = 0;

	if ( dadpid == getpid() ) {
		for ( i = 0; i < nchilds; i++ ) {
			if ( kill(childs[i], SIGKILL) < 0 ) {
				exit(-1);
			}
		}
	}
	exit(0);
}


void
handle_SIGCHLD(int sig)
{
	int saverrno = errno;
	int i = 0;
	pid_t deadchild = wait(NULL);

	if ( deadchild < 0 )
		raise(SIGINT);

	for ( i = 0; i < nchilds; i++  ) {
		if ( childs[i] == deadchild ) {
			childs[i] = childs[nchilds-1];
			childs[nchilds-1] = 0;
			--nchilds;
			break;
		}
	}

	errno = saverrno;
	return;
}
