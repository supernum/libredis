/*
 * Author: jiaofx
 * Description: The source file of ceventloop
 */

#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "ccel.h"
#ifdef LINUX
 #include "ccepoll.c"
#else
 #include "ccselect.c"
#endif


st_event_loop *cel_create_event_loop(int setsize) {
	st_event_loop *el;

	if ((el = malloc(sizeof(st_event_loop))) == NULL) goto err;	
	memset(el, 0, sizeof(st_event_loop));
	if ((el->events = malloc(sizeof(st_el_file_event)*setsize)) == NULL) goto err;
	if ((el->event_data = malloc(sizeof(st_el_event_data)*setsize)) == NULL) goto err;
	memset(el->events, 0, sizeof(st_el_file_event)*setsize);
	memset(el->event_data, 0, sizeof(st_el_event_data)*setsize);
	el->setsize = setsize;
	el->maxfd = -1;
	el->stop = 0;
	el->lasttime = time(NULL);
	if (el_api_create(el) == -1) goto err;
	return el;	

err:
	if (el) {
		if (el->events) free(el->events);
		if (el->event_data) free(el->event_data);
		free(el);
	}
	return NULL;
}

void cel_delete_event_loop(st_event_loop *el) {
	if (!el) return;
	el_api_free(el);
	free(el->events);
	free(el->event_data);
	free(el);
}

void cel_stop(st_event_loop *el) {
	el->stop = 1;
}

int cel_add_file_event(st_event_loop *el, int fd, int mask, el_file_proc *proc, void *clientdata) {
	if (fd >= el->setsize) {
		return EL_ERR;
	}

	st_el_file_event *fe = &el->events[fd];
	if (el_api_add_event(el, fd, mask) == -1) 
		return EL_ERR;
	fe->mask |= mask;
	if (mask & EL_READABLE) fe->rfileproc = proc;
	if (mask & EL_WRITABLE) fe->wfileproc = proc;
	fe->clientdata = clientdata;
	if (fd > el->maxfd) el->maxfd = fd;

	return EL_OK;
}

int cel_del_file_event(st_event_loop *el, int fd, int mask) {
	if (fd >= el->setsize) return EL_ERR;
	st_el_file_event *fe = &el->events[fd];
	if (fe->mask == EL_NONE) return EL_ERR;

	if (el_api_del_event(el, fd, mask) == -1) 
		return EL_ERR;
	fe->mask = fe->mask & (~mask);
	if (fd == el->maxfd) {
		int i;
		for (i = el->maxfd-1; i >= 0; i--) {
			if (el->events[i].mask != EL_NONE)
				break;
		}
		el->maxfd = i;
	}

	return EL_OK;
}

int cel_get_file_event(st_event_loop *el, int fd) {
	if (fd >= el->setsize) 
		return EL_NONE;
	return el->events[fd].mask;
}

static void cel_get_time(long *seconds, long *milliseconds) {
	struct timeval tv;

	gettimeofday(&tv, NULL);
	*seconds = tv.tv_sec;
	*milliseconds = tv.tv_usec/1000;	
}

static void cel_add_milliseconds_to_now(long milliseconds, long *sec, long *ms) {
	long cur_sec, cur_ms, when_sec, when_ms;

	cel_get_time(&cur_sec, &cur_ms);	
	when_sec = cur_sec + milliseconds/1000;
	when_ms = cur_ms + milliseconds%1000;
	if (when_ms >= 1000) {
		when_sec++;
		when_ms -= 1000;
	}
	*sec = when_sec;
	*ms = when_ms;
}

int cel_add_timer_event(st_event_loop *el, int milliseconds, el_timer_proc *proc, void *clientdata) {
	int id = el->next_timer_id++;
	st_el_timer_event *te;

	te = malloc(sizeof(st_el_timer_event));
	if (te == NULL) return EL_ERR;
	te->id = id;
	cel_add_milliseconds_to_now(milliseconds, &te->when_sec, &te->when_ms);
	te->timerproc = proc;
	te->clientdata = clientdata;
	te->next = el->timer_event_head;
	el->timer_event_head = te;
	return id;
}

int cel_del_timer_event(st_event_loop *el, int id) {
	st_el_timer_event *te, *p = NULL;

	te = el->timer_event_head;
	while (te) {
		if (te->id == id) {
			if (p == NULL) 
				el->timer_event_head = te->next;
			else
				p->next = te->next;
			free(te);
			return EL_OK;
		}
		p = te;
		te = te->next;
	}
	return EL_ERR;
}

static st_el_timer_event *cel_search_nearest_timer(st_event_loop *el) {
	st_el_timer_event *te = el->timer_event_head;
    st_el_timer_event *nearest = NULL;

    while(te) {
        if (!nearest || te->when_sec < nearest->when_sec ||
                (te->when_sec == nearest->when_sec &&
                 te->when_ms < nearest->when_ms))
            nearest = te; 
        te = te->next;
    }
    return nearest;
}

/* timer event */
static int cel_process_timer_event(st_event_loop *el) {
	int processed = 0;
	st_el_timer_event *te;
	time_t now = time(NULL);

	if (now < el->lasttime) {
		te = el->timer_event_head;
		while (te) {
			te->when_sec = 0;
			te = te->next;
		}
	}
	el->lasttime = now;
	te = el->timer_event_head;

	while (te) {
		long now_sec, now_ms;

		cel_get_time(&now_sec, &now_ms);
		if (now_sec > te->when_sec || 
				(now_sec == te->when_sec && now_ms >= te->when_ms)) {
			int retval;

			retval = te->timerproc(el, te->id, te->clientdata);
			processed++;
			if (retval != EL_NOMORE) {
				cel_add_milliseconds_to_now(retval, &te->when_sec, &te->when_ms);			
			} else {
				cel_del_timer_event(el, te->id);	
				te = el->timer_event_head;
			}
		}
		te = te->next;
	}

	return processed;	
}

/* process event */
int cel_process_event(st_event_loop *el, int flags) {
	int processed = 0; 
	long ms = -1;

	if (!(flags & EL_FILE_EVENTS) && !(flags & EL_TIMER_EVENTS)) return 0;

	if (el->maxfd != -1 || (flags & EL_TIMER_EVENTS)) {
		st_el_timer_event *te = NULL;

		if (flags & EL_TIMER_EVENTS) 
			te = cel_search_nearest_timer(el);
		if (te) {
			long now_sec, now_ms;
			cel_get_time(&now_sec, &now_ms);
			ms = (te->when_sec - now_sec) * 1000;
			ms += te->when_ms - now_ms;
			if (ms < 0) ms = 0;
		}
	}

	int i, numevents;
	numevents = el_api_poll(el, ms);
	for (i = 0; i < numevents; i++) {
		st_el_file_event *fe = &el->events[el->event_data[i].fd];			
		int mask = el->event_data[i].mask;
		int fd = el->event_data[i].fd;
		int read = 0;

		if (fe->mask & mask & EL_READABLE) {
			fe->rfileproc(el, fd, fe->clientdata, mask);
			read = 1;
		}
		if (fe->mask & mask & EL_WRITABLE) {
			if (!read || fe->rfileproc != fe->wfileproc) 
				fe->wfileproc(el, fd, fe->clientdata, mask);
		}
		processed++;
	}

	if (flags & EL_TIMER_EVENTS) {
		processed += cel_process_timer_event(el);
	}

	return processed;
}

void cel_main(st_event_loop *el) {
	while (!el->stop) {
		if (el->before_sleep)
			el->before_sleep(el);
		cel_process_event(el, EL_ALL_EVENTS);
	}
}

void cel_set_before_sleep_proc(st_event_loop *el, el_before_sleep_proc *proc) {
	el->before_sleep = proc;
}
