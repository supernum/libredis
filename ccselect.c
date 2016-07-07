/*
 * Author: jiaofx
 * Description: unix select
 */

#include <sys/select.h>
#include <string.h>

typedef struct st_el_api_state {
    fd_set rfds, wfds;
    fd_set _rfds, _wfds;
} st_el_api_state;

static int el_api_create(st_event_loop *el) {
	st_el_api_state *state;
   
    if ((state = malloc(sizeof(st_el_api_state))) == NULL)
	    return -1;
	memset(state, 0, sizeof(st_el_api_state));
    FD_ZERO(&state->rfds);
    FD_ZERO(&state->wfds);
    el->apidata = state;
	return 0;
}

static void el_api_free(st_event_loop *el) {
    if (!el->apidata)
        return;
    free(el->apidata);
}

static int el_api_add_event(st_event_loop *el, int fd, int mask) {
    st_el_api_state *state = el->apidata;

    if (mask & EL_READABLE) FD_SET(fd, &state->rfds);
    if (mask & EL_WRITABLE) FD_SET(fd, &state->wfds);
	return 0;
}


static int el_api_del_event(st_event_loop *el, int fd, int mask) {
    st_el_api_state *state = el->apidata;

    if (mask & EL_READABLE) FD_CLR(fd, &state->rfds);
    if (mask & EL_WRITABLE) FD_CLR(fd, &state->wfds);
	return 0;
}

static int el_api_poll(st_event_loop *el, int timeout) {
    st_el_api_state *state = el->apidata;
    int retval, j, numevents = 0;
    struct timeval tval;

    tval.tv_sec = timeout/1000;
    tval.tv_usec = (timeout%1000)*1000;
    memcpy(&state->_rfds, &state->rfds, sizeof(fd_set));
    memcpy(&state->_wfds, &state->wfds, sizeof(fd_set));

    retval = select(el->maxfd+1, &state->_rfds, &state->_wfds, NULL, &tval);
    if (retval > 0) {
        for (j = 0; j <= el->maxfd; j++) {
            int mask = 0;
            st_el_file_event *fe = &el->events[j];

            if (fe->mask == EL_NONE) continue;
            if (fe->mask & EL_READABLE && FD_ISSET(j, &state->_rfds))
                mask |= EL_READABLE;
            if (fe->mask & EL_WRITABLE && FD_ISSET(j, &state->_wfds))
                mask |= EL_WRITABLE;
            el->event_data[numevents].fd = j;
            el->event_data[numevents].mask = mask;
            numevents++;
        }
    }   
    return numevents;
}



