/*
 * Author: jiaofx
 * Description: linux epoll
 */

#include <sys/epoll.h>

typedef struct st_el_api_state {
	int epfd;
	struct epoll_event *events;
} st_el_api_state;

static int el_api_create(st_event_loop *el) {
	st_el_api_state *state = malloc(sizeof(st_el_api_state));

	if (!state) goto err;
	memset(state, 0, sizeof(st_el_api_state));
	state->events = malloc(sizeof(struct epoll_event)*el->setsize);
	if (!state->events) goto err;
	state->epfd = epoll_create(1024);
	if (state->epfd == -1) goto err;
	el->apidata = state;
	return 0;

err:
	if (state) {
		if (state->events) free(state->events);
		free(state);
	}
	return -1;
}

static void el_api_free(st_event_loop *el) {
	st_el_api_state *state = el->apidata;

	close(state->epfd);
	free(state->events);
	free(state);
}

static int el_api_add_event(st_event_loop *el, int fd, int mask) {
	int op;
	struct epoll_event ee;
	st_el_api_state *state = el->apidata;

	op = (el->events[fd].mask == EL_NONE) ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
	ee.events = 0;
	mask |= el->events[fd].mask;
	if (mask & EL_READABLE) ee.events |= EPOLLIN;
	if (mask & EL_WRITABLE) ee.events |= EPOLLOUT;
	ee.data.u64 = 0;
	ee.data.fd = fd;
	if (epoll_ctl(state->epfd, op, fd, &ee) == -1) return -1;

	return 0;
}


static int el_api_del_event(st_event_loop *el, int fd, int delmask) {
	struct epoll_event ee;	
	st_el_api_state *state = el->apidata;
	int mask = el->events[fd].mask & (~delmask);
	int ret;

	ee.events = 0;
	if (mask & EL_READABLE) ee.events |= EPOLLIN;
	if (mask & EL_WRITABLE) ee.events |= EPOLLOUT;
	ee.data.u64 = 0;
	ee.data.fd = fd;
	if (mask != EL_NONE) {
		ret = epoll_ctl(state->epfd, EPOLL_CTL_MOD, fd, &ee);	
	} else {
		ret = epoll_ctl(state->epfd, EPOLL_CTL_DEL, fd, &ee);
	}

	return ret;
}

static int el_api_poll(st_event_loop *el, int timeout) {
	int retval, numevents = 0;
	st_el_api_state *state = el->apidata;
	
	retval = epoll_wait(state->epfd, state->events, el->setsize, timeout);
	if (retval > 0) {
		int i, mask;
		struct epoll_event *e;
		numevents = retval;
		for (i = 0; i < numevents; i++) {
			mask = 0;
			e = state->events+i;
			if (e->events & EPOLLIN) mask |= EL_READABLE;
			if (e->events & EPOLLOUT) mask |= EL_WRITABLE;
			if (e->events & EPOLLERR) mask |= EL_WRITABLE;
			if (e->events & EPOLLHUP) mask |= EL_WRITABLE;
			el->event_data[i].fd = e->data.fd;
			el->event_data[i].mask = mask;
		}
	}
	return numevents;
}



