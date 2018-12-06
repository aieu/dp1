/** ---------------------------------------------------------------------------
 * Server1 - Sequential server
 *
 * @author dcr
 * ----------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>      // errno
#include <netinet/in.h>	// accept()
#include <sys/time.h>	// timeval, FD_SET().. (earlier standards)
#include <sys/select.h> // FD_SET()..
#include <sys/stat.h>   // struct stat
#include <limits.h>	    // NAME_MAX
#include <sys/types.h>  // getsockopt(), pid_t
#include <sys/socket.h> // getsockopt()

#include "../error.h"
#include "../mylibsock.h"
#include "../mylibtcp.h"
#include "../myclients.h"

#define BUF_MAX			(NAME_MAX+7) // 255 + 6 chars + '\0'
#define SO_SNDBUF_MAX	(8120)       // above 8120 gains in performance are negligible
#define	LISTENQ			(FD_SETSIZE) //max queue length of pending connections

/* FUCNTIONS PROTOTYPES */
int     serve_client_rd(int client_socket, struct sclient **clients);
int     serve_client_wr(int client_socket, struct sclient **clients, \
						int buflen);
int     get_info_file(char* filename, uint32_t* ts, uint32_t* size);
int 	get_SO_SNDBUF(int sock);
void 	shutdown_server(struct sclient **clients, \
						struct sready_clients *ready_clients);
void 	init_server(struct sclient **clients, \
					struct sready_clients *ready_clients);


int main (int argc, char *argv[])
{
	signed int listen_socket = 0; // listening socket for new connections
	signed int new_socket, csock;

	struct sclient *clients[FD_SETSIZE]; // NOTE: FD_SETSIZE is 1024
	struct sready_clients ready_clients;

	unsigned int sndbuflen     = SO_SNDBUF_MAX; // socket send buffer size
	socklen_t addrlen = 0;

	int n   = 0;
	int i   = 0;
	int sc  = 0; 	// served clients (after select)
	int nrc = 0; 	// nr. of ready clients (after select)
	fd_set active_rset, active_wset;
	fd_set ready_rset, ready_wset;


	if ( argc < 2 )
		err_quit("ERROR - usage: %s <server port>", argv[0]);

	init_server(clients, &ready_clients);

	if ( ( listen_socket = tcp_listen(NULL, argv[1], &addrlen) ) < 0 )
		exit(-1);

	/* get socket options rcvbuflen */
	sndbuflen = get_SO_SNDBUF(listen_socket);

	/* initialize sets */
	FD_ZERO(&active_rset);
	FD_ZERO(&active_wset);
	FD_SET(listen_socket, &active_rset);

	for ( ; ; ) {

		/* select active sockets */
		memcpy((char *) &ready_rset, (char *) &active_rset, sizeof(fd_set));
		memcpy((char *) &ready_wset, (char *) &active_wset, sizeof(fd_set));

		n = Select(FD_SETSIZE, &ready_rset, &ready_wset, NULL, NULL);
		if ( n < 0 ) {
			shutdown_server(clients, &ready_clients);
			exit(-1);
		}

		nrc = n; // save nr. of ready clients

		/* NOTE: I do give priority to new connections but it's risky since, if
		 * new connections continuously come, other clients may never be served.
		 * That's why the second condition, it won't allow more than FD_SETSIZE
		 * active connections and the server will serve other clients before
		 * accepting new ones */
		if ( ( FD_ISSET(listen_socket, &ready_rset) ) &&
	         ( ready_clients.n_rdcli < (FD_SETSIZE-1) ) ) {

			/* new connection */
			new_socket = accept(listen_socket, NULL, NULL);
			if ( new_socket < 0 ) {
				err_ret("ERROR: could not accept client");
			} else {
				if ( (add_client(new_socket, clients, &ready_clients)) < 0 ) {
					err_msg("ERROR: could not add client.");
					close(new_socket);
				} else {
					FD_SET(new_socket, &active_rset);
				}
			}

		} else {

			/* serve all ready clients */
			for ( i = 0, sc = 0;					/* for..                      */
				  (i < ready_clients.n_rdcli) && 	/* ..all available clients..  */
				  (ready_clients.n_rdcli > 0) &&	/* ..if they exist..          */
				  (sc < nrc);						/* .. and are ready           */
				  i++ ) {

				csock = ready_clients.rdcli[i];	// current client's socket
				if ( FD_ISSET(csock, &ready_rset) ) {

					++sc;
					switch ( serve_client_rd(csock, clients) ) {
						case 1:
							FD_SET (csock, &active_rset);
							break;
						case 0:
							FD_SET (csock, &active_rset);
							FD_SET (csock, &active_wset);
							break;
						case -1:
							FD_CLR(csock, &active_rset);
							FD_CLR(csock, &active_wset);
							rm_client(csock, clients, &ready_clients);
							close(csock);
							break;
						default:
							/* should never get here */
							shutdown_server(clients, &ready_clients);
							exit(-1);
							break;
					}

				} else if ( FD_ISSET(csock, &ready_wset) ) {

					++sc;
					switch ( serve_client_wr(csock, clients, sndbuflen) ) {
						case 1:
							FD_SET (csock, &active_rset);
							FD_CLR(csock, &active_wset);
							break;
						case 0:
							FD_SET (csock, &active_rset);
							FD_SET (csock, &active_wset);
							break;
						case -1:
							FD_CLR(csock, &active_rset);
							FD_CLR(csock, &active_wset);
							rm_client(csock, clients, &ready_clients);
							close(csock);
							break;
						default: // case -2:
							shutdown_server(clients, &ready_clients);
							exit(-1);
							break;
					}
				}
			}
		}
	}

	shutdown_server(clients, &ready_clients);
	exit(0);
}


/**
 * @brief Serves a client reading what he has sent
 *
 * @param csock		Client socket
 * @param clients	All clients list
 *
 * @return  1 if OK,
 * @return  0 if OK and there is data to be sent to the client,
 * @return -1 on error
 */
int
serve_client_rd(int client_socket,
				struct sclient **clients)
{
	char inbuf[BUF_MAX];
	int  there_is_data_to_send = 0;
	int  fnlen                 = 0; // filename length

	/* read data from client */
	if ( (Readn(client_socket, inbuf, 4)) <= 0 )
		return -1;

	/* identify command received */
	if ( strncmp((char*) inbuf, "QUIT", 4) == 0 ) {
		Readn(client_socket, inbuf, 2); // consume '\r\n'
		return -1;
	}

	if ( strncmp(inbuf, "GET ", 4) != 0 ) {
		/* wrong command */
		clients[client_socket]->sendError = 1;
		return 0;

	} else {

		/* get file name */
		for ( fnlen = 0; fnlen < BUF_MAX ; fnlen++ ) {
			if ( (Readn(client_socket, inbuf+fnlen, 1)) <= 0 )
				return -1;

			if ( fnlen < 2 ) continue;	// filename length at least 1

			if ( (inbuf[fnlen-1] == '\r') && (inbuf[fnlen] == '\n') ){
				inbuf[fnlen-1] = '\0';
				break;
			}
		}
		inbuf[fnlen] = '\0'; // if fnlen == BUF_MAX

		/* if file exists add it to files waiting to be uploaded */
		if ( access(inbuf, F_OK) != -1 ) {

			if ( ( add_file_client(inbuf, fnlen, clients[client_socket]) ) < 0 ) {
				clients[client_socket]->sendError = 1;
				return 0; // notify user there was an error (ERR)
			}

			there_is_data_to_send = 1;

		} else {
			clients[client_socket]->sendError = 1;
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
 * @param clients	All clients list
 * @param buflen	Send buffer lenght
 *
 * @return  1 if OK and all data sent to client,
 * @return  0 if OK and there is data to be sent to the client,
 * @return -1 on function error (close client connection)
 * @return -2 on system error (server shutdown)
 */
int
serve_client_wr(int client_socket,
				struct sclient **clients,
				int buflen)
{
	FILE     **fp    = &(clients[client_socket]->cfp);
	int      sError  = clients[client_socket]->sendError;
	uint32_t *btbwcf = &(clients[client_socket]->bytesToBeWrittenCF);

	char    *filename  = NULL;
	uint32_t file_size = 0; // file size,  NOTE: only files < 4 GB
	uint32_t file_ts   = 0; // file timestamp, NOTE: time_t might be defined on 64 bits

	unsigned char *outbuf = NULL;
	int n    = 0;
	int serr = 0;

	if ( ( outbuf = calloc(buflen, sizeof(unsigned char)) ) == NULL ) {
		err_ret("ERROR: could not malloc buffer");
		return -2; // sys error
	}

	/* client must be notified of an error, connection will be closed */
	if ( sError ) {

		if ( ( snprintf((char *) outbuf, 7, "-ERR\r\n") ) < 0 ) {
			free(outbuf);
			return -2; // sys error
		} else {
			Writen(client_socket, outbuf, 6);
		}

		free(outbuf);
		return -1; // user will be deleted
	}


	if ( (*fp) != NULL ) {

		/* a file was already being uploaded, send new data from this file */

		n = ( (*btbwcf) < ((uint32_t) buflen) ? (*btbwcf) : buflen ); // NOTE: buflen is always < INT_MAX

		/* read new data from file */
		if ( ( serr = fread(outbuf, sizeof(unsigned char), n, *fp) ) < n ) {
			if ( feof(*fp) || ferror(*fp) ) {
				err_msg("ERROR: cannot read file.");
				clients[client_socket]->sendError = 1;
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
			return -1; // user will be deleted
		}

		*btbwcf -= serr; // update n. of Bytes To Be Written from Current File

	} else {

		/* if no file was being uploaded
		 * - read files list and retrieve next filename
		 * - read size and last modified timestamp for file
		 * - open file and send header + first bytes from file
		 */

		/* get next file name to send to client */
		if ( ( filename = get_next_file_client(clients[client_socket]) ) == NULL ) {
			free(outbuf);
			return 1; // sent all client-requested files till now
		}

		/* read file size and timestamp */
		if ( ( get_info_file(filename, &file_ts, &file_size) ) < 0 ){
			err_msg("ERROR: file size or timestamp too big.");
			clients[client_socket]->sendError = 1;
			free(outbuf);
			return 0; // file may be bigger than 2^32, there would be overflow
		}
		*btbwcf = file_size; // save file size into client structure

		/* open file */
		if ( ( (*fp) = fopen(filename, "rb") ) == NULL ) {
			err_ret("ERROR: could not open file\"%s\"", filename);
			clients[client_socket]->sendError = 1;
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
		if ( ( serr = fread(outbuf+13, sizeof(uint8_t), n, *fp) ) < n ) {
			if ( feof(*fp) || ferror(*fp) ) {
				err_msg("ERROR: cannot read file.");
				clients[client_socket]->sendError = 1;
				free(outbuf);
				return 0; // error reading file
			} else {
				free(outbuf);
				return -2; // sys error
			}
		}

		/* send data to client */
		if ( ( Writen(client_socket, outbuf, n+13) ) < 0 ) {
			free(outbuf);
			return -1; // user will be deleted
		}

		*btbwcf -= serr; // update n. of Bytes To Be Written in Current File
	}

	free(outbuf);

	/* if all file was sent, check if there is another file to send */
	if ( (*btbwcf) == 0 ) {
		fclose(*fp);
		*fp = NULL;

		if ( ( rm_head_file_client(clients[client_socket]) ) < 0 )
			return -1; // user will be deleted

		if ( there_are_more_files(clients[client_socket]) )
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
shutdown_server(struct sclient **clients,
				struct sready_clients *ready_clients)
{
	int clisock = 0;

	for ( clisock = 0; clisock < FD_SETSIZE; clisock++ ) {
		if ( clients[clisock] != NULL ) {
			rm_client(clisock, clients, ready_clients);
			close(clisock);
		}
	}
}

void
init_server(struct sclient **clients,
			struct sready_clients *ready_clients)
{
	int i = 0;

	for ( i = 0; i < FD_SETSIZE; i++ )
		clients[i] = NULL;

	for ( i = 0; i < FD_SETSIZE; i++ )
		ready_clients->rdcli[i] = 0;

	ready_clients->n_rdcli = 0;
}
