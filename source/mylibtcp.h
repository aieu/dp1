
#ifndef _MYLIBTCP_H
#define _MYLIBTCP_H

int tcp_listen(const char *host, const char *serv, socklen_t *addrlenp);
int	tcp_connect(const char *, const char *);

#endif
