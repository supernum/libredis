
#ifdef LINUX
 #include "ccfmacros.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/socket.h>
#ifdef SUNOS
 #include <sys/sockio.h>
#endif
#include "cctype.h"
#include "ccsocket.h"

#define UDP_MODE_NORMAL 0
#define UDP_MODE_BROADCAST 1
#define UDP_MODE_MULTICAST 2

#define ERROR_BUFFER_SIZE

static void cperror(char *err, const char *fmt, ...) {
    va_list ap; 
    if (!err) return;
    va_start(ap, fmt);
    vsnprintf(err, SOCKET_ERRSTR_SIZE, fmt, ap);
    va_end(ap); 
}

static int csocket_setv6only(char *err, int sockid) {
#ifdef LINUX
	int val = 1;
	if (setsockopt(sockid, IPPROTO_IPV6, IPV6_V6ONLY, &val, sizeof(val)) == -1) {
		cperror(err, "setsockopt IPV6_V6ONLY failed, %s", __ERRMSG__);
		return RET_ERR;
	}
#endif
	return RET_OK;
}

static int csocket_setreuseaddr(char *err, int sockid) {
	int val = 1;
	if (setsockopt(sockid, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) == -1) {
		cperror(err, "setsockopt SO_REUSEADDR failed, %s", __ERRMSG__);
		return RET_ERR;
	}
	return RET_OK;
}

static int csocket_listen(char *err, int sockid, struct sockaddr *sa, int len, int socktype, int backlog) {
	if (bind(sockid, sa, len) == -1) {
		cperror(err, "socket bind failed, %s", __ERRMSG__);
		return RET_ERR;
	}

	if (socktype == SOCK_STREAM) {
		if (listen(sockid, backlog) == -1) {
			cperror(err, "socket listen failed, %s", __ERRMSG__);
			return RET_ERR;
		}
	}
	return RET_OK;	
}

/* TCP\UDP */
static int csocket_create(char *err, int socktype) {
	int sockid;
	if ((sockid = socket(PF_INET, socktype, 0)) == -1) {
		cperror(err, "socket create failed, %s", __ERRMSG__);
		return RET_ERR;
	}
	return sockid;
}

static int csocket_set_block(char *err, int fd, int non_block) {
	int flags;

	if ((flags = fcntl(fd, F_GETFL)) == -1) {
		cperror(err, "socket fcntl(F_GETFL) failed,%s", __ERRMSG__);
		return RET_ERR;
	}	

	if (non_block) 
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;

	if ((flags = fcntl(fd, F_SETFL, flags)) == -1) {
		cperror("socket fcntl(F_SETFL) failed,%s", __ERRMSG__);
		return RET_ERR;
	}	
	return RET_OK;
}

int csocket_block(char *err, int fd) {
	return csocket_set_block(err, fd, 0);
}

int csocket_non_block(char *err, int fd) {
	return csocket_set_block(err, fd, 1);
}

int csocket_selectid(int fd, int timeout, int flags) {
	fd_set wset,rset;
    fd_set *pw, *pr;
    struct timeval tval, *ptval = NULL;
	int res;

    pw = pr = NULL;
	if (timeout > 0) {
		tval.tv_sec = timeout/1000;
		tval.tv_usec = (timeout%1000)*1000;
		ptval = &tval;
	}
    if (flags & FD_WRITE) {
        FD_ZERO(&wset);
        FD_SET(fd, &wset);
        pw = &wset;
    }
    if (flags & FD_READ) {
        FD_ZERO(&rset);
        FD_SET(fd, &rset);
        pr = &rset;
    }

	if ((res = select(fd+1, pr, pw, NULL, ptval)) <= 0 ) {
        return RET_ERR;
    }
	res = 0;
    if ((flags & FD_WRITE) && FD_ISSET(fd, &wset)) {
        res |= FD_WRITE;
    }
    if ((flags & FD_READ) && FD_ISSET(fd, &rset)) {
        res |= FD_READ;
    }
    return res;
}

/* send */
int csocket_send(char *err, int sockid, const char *buf, int len, int timeout) {
    int ret, pos = 0, count = 10;

	if (timeout > 0) {
		if ((ret = csocket_selectid(sockid, timeout, FD_WRITE)) == RET_ERR || !(ret & FD_WRITE)) { 
            cperror(err, "socket busy, errno=%d, errmsg=%s", errno, __ERRMSG__);       
			return RET_ERR;
		}
	}
    do {
		if ((ret = write(sockid, (char *)(buf + pos), len - pos)) < 1) {
			if (ret == 0) return pos;
			else if (count-- && (errno == EAGAIN || errno == EINTR)) 
				continue;
			cperror(err, "socket write failed, pos=%d, ret=%d, errno=%d, errmsg=%s", 
					pos, ret, errno, __ERRMSG__);
			return RET_ERR;
		}
		pos += ret;
    } while ((len - pos) > 0);

    return pos;
}

/* recv */
int csocket_recv(char *err, int sockid, const char *buf, int len, int timeout) {
    int ret = 0, pos = 0;

	if (timeout > 0) {
		if ((ret = csocket_selectid(sockid, timeout, FD_READ)) == RET_ERR || !(ret & FD_READ)) { 
            cperror(err, "socket recv timeout, errno=%d, errmsg=%s", errno, __ERRMSG__);       
			return RET_ERR;
		}
	}
    do {
		if ((ret = read(sockid, (char *)(buf + pos), len - pos)) < 1) {
			if (ret == 0) return pos;
			else if (errno == EAGAIN) return pos;
#ifndef LINUX
			if (pos != 0) return pos;
#endif
			cperror(err, "socket read failed, pos=%d, ret=%d, errno=%d, errmsg=%s", 
					pos, ret, errno, __ERRMSG__);
			return RET_ERR;   
		}
		pos += ret;
    } while (len > pos);
    
    return pos;
}

int csocket_tcp_connect(char *err, char *ip, int port, unsigned int timeout) {
	int res, s = 0;
	struct sockaddr_in svr_addr;

	if ((s = csocket_create(err, SOCK_STREAM)) == RET_ERR)
		return RET_ERR;	

	if (timeout) {
		if (csocket_non_block(err, s) == RET_ERR)
			goto error;			
	} else {
		if (csocket_block(err, s) == RET_ERR)
			goto error;			
	}

	memset(&svr_addr, 0, sizeof(struct sockaddr_in));
	svr_addr.sin_family = AF_INET;
	svr_addr.sin_addr.s_addr = inet_addr(ip);
	svr_addr.sin_port = htons(port);

	if (connect(s, (struct sockaddr *)&svr_addr, sizeof(struct sockaddr_in)) == -1) {
		if (errno && (errno != EINPROGRESS)) {
			cperror(err, "socket connect failed, %s:%d, %s", ip, port, __ERRMSG__);	
			goto error;	
		}
		if ((res = csocket_selectid(s, timeout, FD_READ|FD_WRITE)) == RET_ERR) {
			cperror(err, "socket connect timeout, %s:%d, %s", ip, port, __ERRMSG__);	
			goto error;
		}
        if (res & FD_READ) {
			cperror(err, "socket connect exception, %s:%d", ip, port);	
			goto error;
        }
	}
	if (timeout && csocket_block(err, s) == RET_ERR)
		goto error;	

	return s;

error:
	if (s) close(s);
	return RET_ERR;
}

static int _csocket_udp_connect(char *err, char *addr, int port, int type) {
	int s = 0;
	struct sockaddr_in svr_addr;

	if ((s = csocket_create(err, SOCK_DGRAM)) == RET_ERR)
		return RET_ERR;
	if (UDP_MODE_BROADCAST == type) {
		if (csocket_set_broadcast(err, s, 1) == RET_ERR) 
			goto error;
	}
	
	memset(&svr_addr, 0, sizeof(struct sockaddr_in));
	svr_addr.sin_family = AF_INET;
	svr_addr.sin_addr.s_addr = inet_addr(addr);
	svr_addr.sin_port = htons(port);

	if (connect(s, (struct sockaddr *)&svr_addr, sizeof(struct sockaddr_in)) == -1) {
		cperror(err, "socket connect failed, %s:%d, %s", addr, port, __ERRMSG__);	
		goto error;
	}

	return s;
error:
	if (s) close(s);
	return RET_ERR;
}

int csocket_udp_connect(char *err, char *addr, int port) {
	return _csocket_udp_connect(err, addr, port, UDP_MODE_NORMAL);	
}

int csocket_udp_connect_broadcast(char *err, char *addr, int port) {
	return _csocket_udp_connect(err, addr, port, UDP_MODE_BROADCAST);	
}

int csocket_udp_connect_multicast(char *err, char *addr, int port) {
	return _csocket_udp_connect(err, addr, port, UDP_MODE_MULTICAST);
}

/**/
int csocket_set_keepalive(char *err, int sockid) {
	int val = 1;
	if (setsockopt(sockid, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1) {
		cperror(err, "setsockopt SO_KEEPALIVE failed, %s", __ERRMSG__);
		return RET_ERR;
	}
#ifdef LINUX
	/* start keepalive time */
	val = 9;
	if (setsockopt(sockid, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) == -1) {
		cperror(err, "setsockopt TCP_KEEPIDLE failed, %s", __ERRMSG__);
		return RET_ERR;
	}
	/* keepalive interval */
	val = 3;
	if (setsockopt(sockid, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) == -1) {
		cperror(err, "setsockopt TCP_KEEPINTVL failed, %s", __ERRMSG__);
		return RET_ERR;
	}
	/* keepalive count */
	val = 3;
	if (setsockopt(sockid, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) == -1) {
		cperror(err, "setsockopt TCP_KEEPCNT failed, %s", __ERRMSG__);
		return RET_ERR;
	}
#endif
	return RET_OK;
}

int csocket_set_deferaccept(char *err, int sockid) {
    NOMORE(err);
    NOMORE(sockid);
#ifdef LINUX
	int val = 1;
	if (setsockopt(sockid, SOL_TCP, TCP_DEFER_ACCEPT, &val, sizeof(val)) == -1) {
		cperror(err, "setsockopt TCP_DEFER_ACCEPT failed, %s", __ERRMSG__);
		return RET_ERR;
	}
#endif
	return RET_OK;
}

int csocket_set_block_timeout(char *err, int sockid, long long ms) {
    NOMORE(err);
    NOMORE(sockid);
    NOMORE(ms);
#ifdef LINUX
	struct timeval tv;

	tv.tv_sec = ms/1000;
	tv.tv_usec = (ms%1000)*1000;	
	if (setsockopt(sockid, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == -1) {
		cperror(err, "setsockopt SO_SNDTIMEO failed, %s", __ERRMSG__);
		return RET_ERR;
	}
	if (setsockopt(sockid, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
		cperror(err, "setsockopt SO_RCVTIMEO failed, %s", __ERRMSG__);
		return RET_ERR;
	}
#endif
	return RET_OK;
}

int csocket_set_sendbuffer(char *err, int sockid, int buffsize) {
	if (setsockopt(sockid, SOL_SOCKET, SO_SNDBUF, &buffsize, sizeof(buffsize)) == -1) {
		cperror(err, "setsockopt SO_SNDBUF failed, %s", __ERRMSG__);
		return RET_ERR;
	}
	return RET_OK;
}

int csocket_set_recvbuffer(char *err, int sockid, int buffsize) {
	if (setsockopt(sockid, SOL_SOCKET, SO_RCVBUF, &buffsize, sizeof(buffsize)) == -1) {
		cperror(err, "setsockopt SO_RCVBUF failed, %s", __ERRMSG__);
		return RET_ERR;
	}
	return RET_OK;
}

/* ON: val=1  OFF: val=0 (default:0) */
int csocket_set_broadcast(char *err, int sockid, int val) {
	if (setsockopt(sockid, SOL_SOCKET, SO_BROADCAST, &val, sizeof(val)) == -1) {
		cperror(err, "setsockopt SO_BROADCAST failed, %s", __ERRMSG__);
		return RET_ERR;
	}
	return RET_OK;
}

int csocket_set_multicast(char *err, int sockid, char *addr) {
	struct ip_mreq mreq;
	mreq.imr_multiaddr.s_addr = inet_addr(addr);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	if (setsockopt(sockid, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == -1) {
		cperror(err, "setsockopt multicast failed, %s", __ERRMSG__);
		return RET_ERR;
	}
	return RET_OK;
}

static int csocket_settcpnodelay(char *err, int sockid, int val) {
	if (setsockopt(sockid, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == -1) {
		cperror(err, "setsockopt TCP_NODELAY failed, %s", __ERRMSG__);
		return RET_ERR;
	}
	return RET_OK;
}

int csocket_enable_tcpnodelay(char *err, int sockid) {
	return csocket_settcpnodelay(err, sockid, 1);	
}

int csocket_disable_tcpnodelay(char *err, int sockid) {
	return csocket_settcpnodelay(err, sockid, 0);	
}

static int _csocket_server(char *err, char *bindaddr, int port, int af, int socktype, int backlog) {
	int sockid = 0;
	char _port[6];
	struct addrinfo hints, *srvinfo, *p;

	snprintf(_port, 6, "%d", port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = af;
	hints.ai_socktype = socktype;
	hints.ai_flags = AI_PASSIVE;
	
	if (getaddrinfo(bindaddr, _port, &hints, &srvinfo) != 0) {
		cperror(err, "socket getaddrinfo failed, %s", __ERRMSG__);	
		return RET_ERR;
	}
	for (p = srvinfo; p != NULL; p = p->ai_next) {
		if ((sockid = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) 
			continue;	
		if (af == AF_INET6 && csocket_setv6only(err, sockid) == RET_ERR) goto error;
		if (csocket_setreuseaddr(err, sockid) == RET_ERR) goto error;
		if (csocket_listen(err, sockid, p->ai_addr, p->ai_addrlen, socktype, backlog) == RET_ERR) goto error;
		goto end;
	}
	if (p == NULL) {
		return RET_ERR;
	}

error:
	close(sockid);
	sockid = RET_ERR;
end:
	freeaddrinfo(srvinfo);	
	return sockid;
}

int csocket_udpserver(char *err, char *bindaddr, int port) {
	return _csocket_server(err, bindaddr, port, AF_INET, SOCK_DGRAM, 0);
}

int csocket_udp6server(char *err, char *bindaddr, int port) {
	return _csocket_server(err, bindaddr, port, AF_INET6, SOCK_DGRAM, 0);
}

int csocket_tcpserver(char *err, char *bindaddr, int port, int backlog) {
	return _csocket_server(err, bindaddr, port, AF_INET, SOCK_STREAM, backlog);
}

int csocket_tcp6server(char *err, char *bindaddr, int port, int backlog) {
	return _csocket_server(err, bindaddr, port, AF_INET6, SOCK_STREAM, backlog);
}

int csocket_tcpaccept(char *err, int sockid, char *ip, size_t iplen, int *port) {
	int fd;
	struct sockaddr_storage sa;
	socklen_t salen = sizeof(sa);

	do {
		fd = accept(sockid, (struct sockaddr *)&sa, &salen);
	} while(fd == -1 && errno == EINTR);
	if (fd == -1) {
		cperror(err, "socket accept failed, %s", __ERRMSG__);	
		return RET_ERR;
	}

	if (sa.ss_family == AF_INET) {
		struct sockaddr_in *s = (struct sockaddr_in *)&sa;
		if (ip) inet_ntop(AF_INET, (void *)&(s->sin_addr), ip, iplen);
		if (port) *port = ntohs(s->sin_port);
	} else if (sa.ss_family == AF_INET6) {
		struct sockaddr_in6 *s = (struct sockaddr_in6 *)&sa;
		if (ip) inet_ntop(AF_INET6, (void *)&(s->sin6_addr), ip, iplen);
		if (port) *port = ntohs(s->sin6_port);
	} else {
		cperror(err, "socket accept unknown ss_family type, %d", sa.ss_family);	
	}
	return fd;
}

int csocket_get_sockname(char *err, int sockid, char *ip, size_t iplen, int *port) {
	struct sockaddr_storage sa;
	socklen_t salen = sizeof(sa);

	if (getsockname(sockid, (struct sockaddr *)&sa, &salen) == -1) {
		if (ip) ip[0] = 0;
		if (port) *port = 0;
		return RET_ERR;
	}
	if (sa.ss_family == AF_INET) {
		struct sockaddr_in *s = (struct sockaddr_in *)&sa;
		if (ip) inet_ntop(AF_INET, (void *)&(s->sin_addr), ip, iplen);
		if (port) *port = ntohs(s->sin_port);
	} else if (sa.ss_family == AF_INET6) {
		struct sockaddr_in6 *s = (struct sockaddr_in6 *)&sa;
		if (ip) inet_ntop(AF_INET6, (void *)&(s->sin6_addr), ip, iplen);
		if (port) *port = ntohs(s->sin6_port);
	} else {
		cperror(err, "socket accept unknown ss_family type, %d", sa.ss_family);	
	}

	return RET_OK;
}

int csocket_get_peername(char *err, int sockid, char *ip, size_t iplen, int *port) {
	struct sockaddr_storage sa;
	socklen_t salen = sizeof(sa);

	if (getpeername(sockid, (struct sockaddr *)&sa, &salen) == -1) {
		if (ip) ip[0] = 0;
		if (port) *port = 0;
		return RET_ERR;
	}
	if (sa.ss_family == AF_INET) {
		struct sockaddr_in *s = (struct sockaddr_in *)&sa;
		if (ip) inet_ntop(AF_INET, (void *)&(s->sin_addr), ip, iplen);
		if (port) *port = ntohs(s->sin_port);
	} else if (sa.ss_family == AF_INET6) {
		struct sockaddr_in6 *s = (struct sockaddr_in6 *)&sa;
		if (ip) inet_ntop(AF_INET6, (void *)&(s->sin6_addr), ip, iplen);
		if (port) *port = ntohs(s->sin6_port);
	} else {
		cperror(err, "socket accept unknown ss_family type , %d", sa.ss_family);	
	}

	return RET_OK;
}

