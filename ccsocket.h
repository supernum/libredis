/*
 * Author: jiaofx
 * Description: The source file of socket
 */

#ifndef __CC_SOCKET_H__
#define __CC_SOCKET_H__

#include <stdlib.h>
#include <unistd.h>

#define FD_WRITE 1
#define FD_READ 2

#define SOCKET_ERRSTR_SIZE 128

/* timeout:millsecond */
int csocket_selectid(int fd, int timeout, int flags);
int csocket_tcpserver(char *err, char *bindaddr, int port, int backlog);
int csocket_tcp6server(char *err, char *bindaddr, int port, int backlog);
int csocket_tcpaccept(char *err, int sockid, char *ip, size_t iplen, int *prot);
int csocket_tcp_connect(char *err, char *addr, int port, unsigned int timeout);
int csocket_udpserver(char *err, char *bindaddr, int port);
int csocket_udp6server(char *err, char *bindaddr, int port);
int csocket_udp_connect(char *err, char *addr, int port);
int csocket_udp_connect_broadcast(char *err, char *addr, int port);
int csocket_udp_connect_multicast(char *err, char *addr, int port);
int csocket_block(char *err, int fd);
int csocket_non_block(char *err, int fd);
int csocket_enable_tcpnodelay(char *err, int sockid);
int csocket_disable_tcpnodelay(char *err, int sockid);
int csocket_set_keepalive(char *err, int sockid);
int csocket_set_deferaccept(char *err, int sockid);
int csocket_set_block_timeout(char *err, int sockid, long long ms);
int csocket_set_sendbuffer(char *err, int sockid, int buffsize);
int csocket_set_recvbuffer(char *err, int sockid, int buffsize);
int csocket_set_broadcast(char *err, int sockid, int val);
int csocket_set_multicast(char *err, int sockid, char *addr);
int csocket_get_sockname(char *err, int sockid, char *ip, size_t iplen, int *prot);
int csocket_get_peername(char *err, int sockid, char *ip, size_t iplen, int *prot);
int csocket_send(char *err, int sockid, const char *buf, int len, int timeout);
int csocket_recv(char *err, int sockid, const char *buf, int len, int timeout);

#endif /* __CC_SOCKET_H__ */

