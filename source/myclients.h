
#ifndef _MYCLIENTS_H
#define _MYCLIENTS_H

#include <inttypes.h> // uint32_t

/* DATA DEFINITION */
struct sfiles {
	char 			*filename;
	struct sfiles	*next_file;
};

struct sclient {
	FILE*         cfp;			      // Current File (Pointer) transferring
	struct sfiles *files;             // files requested list
	uint32_t      bytesToBeWrittenCF; // bytes still to be written on current file
	int		      sendError;          // says if server has to send error to client
};

struct sready_clients {
	int rdcli[FD_SETSIZE]; // ready clients array
	int n_rdcli;           // number of ready clients
};

/* FUNCTIONS */

/**
 * @brief Adds a client to the 'server's clients database'
 *
 * @param sockfd            client's associated socket
 * @param clients           clients database
 * @param ready_clients     available clients list
 *
 * @return   1 if OK
 * @return  -1 on function error
 * @return  -2 on system error
 */
int add_client(int sockfd,
		  	   struct sclient **clients,
		  	   struct sready_clients *ready_clients);


/**
 * @brief Removes a client to the 'server's clients database'
 *
 * @param sockfd            client's associated socket
 * @param clients           clients database
 * @param ready_clients     available clients list
 *
 * @return   1 if OK
 * @return  -1 on function error
 * @return  -2 on system error
 */
int rm_client(int sockfd,
		  	  struct sclient **clients,
		  	  struct sready_clients *ready_clients);

/**
 * @brief Adds a file to the client's files list
 *
 * @param filename      the filename
 * @param file_size     lenght of the filename
 * @param client        reference to client
 *
 * @return   1 if OK
 * @return  -1 on function error
 * @return  -2 on system error
 */
int add_file_client(char* filename,
                    int file_size,
                    struct sclient *client);

/**
 * @brief Retrieve the name of the next file to be sent to client
 *
 * @param client        reference to client
 *
 * @return  pointer to file name if OK
 * @return  NULL on error
 */
char * get_next_file_client(struct sclient *client);

/**
 * @brief Removes the head file (presumably sent) in client's files list
 *
 * @param client        reference to client
 *
 * @return   1 if Ok
 * @return  -1 on function error
 */
int rm_head_file_client(struct sclient *client);

/**
 * @brief Checks if client has requested other files
 *
 * @param client        reference to client
 *
 * @return  1 to say YES
 * @return  0 to say NO
 */
int there_are_more_files(struct sclient *client);

#endif
