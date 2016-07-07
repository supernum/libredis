
#ifndef __LIBREDIS_H__
#define __LIBREDIS_H__
#include <unistd.h>

#define REDIS_ERRBUF_SIZE 128
#define REDIS_READER_MAX_BUF (1024*64)

/* redis error code */
#define REDIS_ERR_IO 1
#define REDIS_ERR_EOF 2
#define REDIS_ERR_PROTOCOL 3
#define REDIS_ERR_OMM 4
#define REDIS_ERR_OTHER 5

#define REDIS_REPLY_STRING 1
#define REDIS_REPLY_ARRAY 2
#define REDIS_REPLY_INTEGER 3
#define REDIS_REPLY_NIL 4
#define REDIS_REPLY_STATUS 5
#define REDIS_REPLY_ERROR 6

#define REDIS_BLOCK 0x1

typedef struct redis_context {
    int err;
    char errstr[REDIS_ERRBUF_SIZE];
    int fd;
    int flags;	
    int pipe;
    char *obuf;
} redis_context;

typedef struct redis_reply {
    int type;
    long long integer;
    int len;
    char *str;
    size_t elements;
    size_t total;           /* total elements */
    struct redis_reply *element;	
} redis_reply;

typedef struct redis_reader {
    int err;
    char errstr[REDIS_ERRBUF_SIZE];
    char *buf;
    size_t pos;
    size_t len;
    size_t maxbuf;    
    size_t readcount;
    redis_reply *reply;
    redis_context *c;
} redis_reader;

struct redis_async_context;
typedef void (redis_callback_function)(struct redis_async_context *ac);
typedef struct redis_async_context {
    int err;
    char *errstr; 
    redis_context *c;
    redis_reader *r;
    char ip[64];
    int port;
    char passwd[512];
    redis_callback_function *fn_read;
    redis_callback_function *fn_reconnect;
    void *el;
    int status;
} redis_async_context;

redis_context *redis_connect(char *ip, int port);
redis_context *redis_connect_with_timeout(char *ip, int port, size_t timeout);
void redis_free(redis_context *c);
redis_reader *redis_create_reader(void);
void redis_free_reader(redis_reader *r);
int redis_set_timeout(redis_context *c, size_t timeout);
int redis_set_nonblock(redis_context *c);

int redis_append_command(redis_context *c, const char *cmd, ...);
int redis_exec_command(redis_context *c, redis_reader *r);

int redis_get_return_number(redis_reader *r);
redis_reply *redis_get_reply(redis_reader *r);

/* redis async */
#define redis_async_append_command(ac, cmd) redis_append_command(ac->c, cmd)
#define redis_async_exec_command(ac) redis_exec_command(ac->c, ac->r)
#define redis_async_get_reply(ac) redis_get_reply(ac->r)
redis_async_context* redis_async_connect(char *err, char *ip, int port, char *pass);
void redis_async_free(redis_async_context *ac);
void redis_async_set_reconnect_callback(redis_async_context *ac, redis_callback_function *fn);
void redis_async_set_read_callback(redis_async_context *ac, redis_callback_function *fn);
void redis_async_run(redis_async_context *ac);
    

#endif /*__LIBREDIS_H__*/
