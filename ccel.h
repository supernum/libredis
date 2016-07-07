/*
 * Author: jiaofx
 * Description: eventloop
 */

#ifndef __CC_EVENTLOOP_H__
#define __CC_EVENTLOOP_H__

#define EL_OK 0
#define EL_ERR -1

#define EL_NONE 0
#define EL_READABLE 1
#define EL_WRITABLE 2

#define EL_FILE_EVENTS 1
#define EL_TIMER_EVENTS 2
#define EL_ALL_EVENTS (EL_FILE_EVENTS|EL_TIMER_EVENTS)

#define EL_NOMORE -1


struct st_event_loop;

typedef void el_file_proc(struct st_event_loop *el, int fd, void *clentdata, int mask);
typedef int el_timer_proc(struct st_event_loop *el, int id, void *clentdata);
typedef void el_before_sleep_proc(struct st_event_loop *el);

typedef struct st_el_file_event {
	int mask;           /* read|write */
	el_file_proc *rfileproc;  /* read */
	el_file_proc *wfileproc;  /* write */
	void *clientdata;
} st_el_file_event;

typedef struct st_el_timer_event {
	int id;
	long when_sec;
	long when_ms;
	el_timer_proc *timerproc;
	void *clientdata;
	struct st_el_timer_event *next;
} st_el_timer_event;

typedef struct st_el_event_data {
	int fd;
	int mask;
} st_el_event_data;

typedef struct st_event_loop {
	int stop;
	int maxfd;
	int setsize;
	int next_timer_id;
	time_t lasttime;
	st_el_file_event *events;		
	st_el_event_data *event_data;
	st_el_timer_event *timer_event_head;
	void *apidata;
	el_before_sleep_proc *before_sleep;
} st_event_loop;

/* Function prototypes */
st_event_loop *cel_create_event_loop(int setsize);
void cel_delete_event_loop(st_event_loop *el);
void cel_stop(st_event_loop *el);

int cel_add_file_event(st_event_loop *el, int fd, int mask, el_file_proc *proc, void *clientdata);
int cel_del_file_event(st_event_loop *el, int fd, int mask);
int cel_get_file_event(st_event_loop *el, int fd);

int cel_add_timer_event(st_event_loop *el, int milliseconds, el_timer_proc *proc, void *clientdata);
int cel_del_timer_event(st_event_loop *el, int id);

void cel_set_before_sleep_proc(st_event_loop *el, el_before_sleep_proc *proc);

void cel_main(st_event_loop *el);

#endif /* __C_EVENTLOOP_H__ */



