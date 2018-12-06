/** ---------------------------------------------------------------------------
 * Assignment - Client management library
 *
 * @author dcr
 * ----------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>   // NAME_MAX
#include <sys/select.h> // FD_SETSIZE

#include	"error.h"
#include    "myclients.h"

#define BUF_MAX			(NAME_MAX+7) // 255 + 6 chars + '\0'

int
add_client(int sockfd,
		   struct sclient **clients,
		   struct sready_clients *ready_clients)
{
	if ( ready_clients ) {
		if ( (ready_clients->n_rdcli) == FD_SETSIZE ) {
			err_msg("ERROR: maximum nr. of clients reached.\n");
			return -1;
		}
	}

	if ( clients[sockfd] != NULL ) {
		err_msg("ERROR: client already registered!.\n");
		return -1;
	}

	clients[sockfd] = malloc(sizeof(struct sclient));
	if ( !(clients[sockfd]) ) {
		err_msg("ERROR: cannot alloc client.\n");
		return -2;
	}

	/* init client */
	clients[sockfd]->cfp                = NULL;
	clients[sockfd]->files              = NULL;
	clients[sockfd]->bytesToBeWrittenCF = 0;
	clients[sockfd]->sendError 			= 0;

	if ( ready_clients ) {
		/* add client to available clients */
		ready_clients->rdcli[(ready_clients->n_rdcli)++]=sockfd;
	}

	return 1;
}



int
rm_client(int sockfd,
		  struct sclient **clients,
		  struct sready_clients *ready_clients)
{
	struct sfiles *c = NULL;
	struct sfiles *n = NULL;
	size_t i         = 0;
	size_t tail      = 0;

	if ( clients[sockfd] == NULL )
		return 1;

	if ( (clients[sockfd]->cfp) != NULL ) {
        if ( ( fclose(clients[sockfd]->cfp) ) != 0 )
            return -2;
	}

	if ( (clients[sockfd]->files) != NULL ) {
		c = clients[sockfd]->files;
		do {
			n = c->next_file;
			if ( (c->filename) != NULL )
				free(c->filename);
			free(c);
			c = n;
		} while ( c != NULL );

        clients[sockfd]->files = NULL;
	}

	free(clients[sockfd]);
	clients[sockfd] = NULL;

	if ( ready_clients ) {
		/* remove client from ready clients queue */
		tail = ready_clients->n_rdcli;
		if ( tail > 0 ) {
			for ( i = 0; i < tail; i++ ) {
				if ( ready_clients->rdcli[i] == sockfd ) {
					ready_clients->rdcli[i] = ready_clients->rdcli[tail-1]; // "so the last shall be first"
					ready_clients->n_rdcli -= 1;
					break;
				}
			}
		}
	}

	return 1;
}



int
add_file_client(char* filename,
                int file_size,
                struct sclient *client)
{
    struct sfiles **p = NULL;

	/* search for last item in files list with 'next_file'==NULL */
    for ( p = &(client->files); *p != NULL; p = &((*p)->next_file) );

    *p = calloc (1, sizeof(struct sfiles));
    if ( !(*p) )
        return -2;

    (*p)->filename  = strndup(filename, file_size);
    (*p)->next_file = NULL;

    return 1;
}


char *
get_next_file_client(struct sclient *client)
{
    struct sfiles *p = client->files;
    if ( p == NULL )
        return NULL;

    return p->filename;
}



int
rm_head_file_client(struct sclient *client)
{
    struct sfiles **p = &(client->files);
    struct sfiles *n  = NULL;

    if ( (*p) == NULL )
        return -1; // nothing to remove

    n = (*p)->next_file;

    if ( ((*p)->filename) != NULL )
        free((*p)->filename);

    free(*p);

    *p = n; // update 'list head'

    return 1;
}


int
there_are_more_files(struct sclient *client)
{
    if ( (client->files) == NULL )
        return 0;

    return 1;
}
