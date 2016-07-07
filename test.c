
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libredis.h"

void redis_read_message(redis_async_context *ac);
void redis_reconnect(redis_async_context *ac);

static void redis_free_object_and_exit(redis_context *c, redis_reader *reader) {
    redis_free_reader(reader);
    redis_free(c);
    exit(1); 
}

int main(int argc, char **argv) {

    ((void)argc);
    ((void)argv);

    /* case 1 */
    redis_context *c;
    redis_reader *reader = NULL;
    redis_reply *reply;
    int ret;

    c = redis_connect("127.0.0.1", 6379);
    if (c == NULL) {
        fprintf(stderr, "redis connect failed\n");
        return -1;
    }

    reader = redis_create_reader();
    redis_append_command(c, "auth pass");
    ret = redis_exec_command(c, reader);
    if (ret == -1) {
        fprintf(stderr, "redis auth failed, err=%s", c->errstr);
        redis_free_object_and_exit(c, reader);
    }
    reply = redis_get_reply(reader);
    if (reply == NULL) {
        fprintf(stderr, "redis get reply failed, err=%s", reader->errstr);
        redis_free_object_and_exit(c, reader);
    }
    if (reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "OK") == 0) {
        printf("auth ok\n");
    } else {
        printf("auth failed, exit\n");
        redis_free_object_and_exit(c, reader);
    }

    redis_append_command(c, "hget k1 f1");
    ret = redis_exec_command(c, reader);
    if (ret == -1) {
        fprintf(stderr, "redis exec command failed, err=%s", c->errstr);
        redis_free_object_and_exit(c, reader);
    }
    reply = redis_get_reply(reader);
    if (reply == NULL) {
        fprintf(stderr, "redis get reply failed, err=%s", reader->errstr);
        redis_free_object_and_exit(c, reader);
    }
    switch (reply->type) {
        case REDIS_REPLY_ERROR:
            break;
        case REDIS_REPLY_NIL:
            printf("return NULL\n");
            break;
        case REDIS_REPLY_INTEGER:
            printf("return data [%lld]\n", reply->integer);
            break;
        case REDIS_REPLY_STRING:
            printf("return data [%s]\n", reply->str);
            break;
        default:
            printf("return type error\n");
    }

    redis_free_reader(reader);
    redis_free(c);

    /* case 2 */
    char err[REDIS_ERRBUF_SIZE];
    redis_async_context *ac;
    ac = redis_async_connect(err, "127.0.0.1", 6379, "pass");
    if (ac == NULL) {
        printf("redis connect error, errstr=%s\n", err);
        exit(1);
    }
    redis_async_append_command(ac, "subscribe c1 c2");
    if (redis_async_exec_command(ac) == -1) {
        printf("redis command exec failed, %s\n", ac->errstr);
        exit(1);
    }
    redis_async_set_read_callback(ac, redis_read_message);
    redis_async_set_reconnect_callback(ac, redis_reconnect);
    
    printf("subscribe c1 c2 success, please publish data\n");
    redis_async_run(ac);
    
    return 0;
}

void redis_reconnect(redis_async_context *ac) {
    redis_async_append_command(ac, "subscribe c1 c2");
    if (redis_async_exec_command(ac) == -1) {
        printf("redis commanc exec failed, %s\n", ac->errstr);
    }
}

void redis_read_message(redis_async_context *ac) {
    redis_reply *reply;
    
    while ((reply = redis_async_get_reply(ac)) != NULL) {
        if (reply->type == REDIS_REPLY_ARRAY && reply->elements) {
            printf("out1=%s\n", reply->element[0].str);
            printf("out2=%s\n", reply->element[1].str);
            printf("out3=%s\n", reply->element[2].str);
        }
    }
}
