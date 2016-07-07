
#include <malloc.h>
#include <stdarg.h>
#include <ctype.h>
#include "cctype.h"
#include "ccsocket.h"
#include "ccds.h"
#include "ccel.h"
#include "libredis.h"

#define REDIS_ERRBUF_LENGTH (REDIS_ERRBUF_SIZE-1)

static void redis_set_error(redis_context *c, int type, const char *fmt, ...) {
    c->err = type;
    if (fmt) {
        va_list ap; 
        va_start(ap, fmt);
        vsnprintf(c->errstr, REDIS_ERRBUF_LENGTH, fmt, ap);
        va_end(ap);
    }
}

static void redis_reader_set_error(redis_reader *r, int type, const char *fmt, ...) {
    r->err = type;
    if (fmt) {
        va_list ap; 
        va_start(ap, fmt);
        vsnprintf(r->errstr, REDIS_ERRBUF_LENGTH, fmt, ap);
        va_end(ap);
    }
}

static int intlen(int i) {
    int len = 0;
    if (i < 0) {
        len++;
        i = -i;
    }
    do {
        len++;
        i /= 10;
    } while(i);
    return len;
}

static size_t bulklen(size_t len) {
    return 1+intlen(len)+2+len+2;
}

static char *read_bytes(redis_reader *r, unsigned int bytes) {
    char *p;
    if (r->len-r->pos >= bytes) {
        p = r->buf+r->pos;
        r->pos += bytes;
        return p;
    }
    return NULL;
}

/* Find pointer to \r\n. */
static char *seek_newline(char *s, size_t len) {
    int pos = 0; 
    int _len = len-1;

    /* Position should be < len-1 because the character at "pos" should be
     * followed by a \n. Note that strchr cannot be used because it doesn't
     * allow to search a limited length and the buffer that is being searched
     * might not have a trailing NULL character. */
    while (pos < _len) {
        while(pos < _len && s[pos] != '\r') pos++;
        if (s[pos] != '\r') {
            /* Not found. */
            return NULL;
        } else {
            if (s[pos+1] == '\n') {
                /* Found. */
                return s+pos;
            } else {
                /* Continue searching. */
                pos++;
            }
        }
    }    
    return NULL;
}

/* Read a long long value starting at *s, under the assumption that it will be
 * terminated by \r\n. Ambiguously returns -1 for unexpected input. */
static long long read_longlong(char *s) {
    long long v = 0;
    int dec, mult = 1;
    char c;

    if (*s == '-') {
        mult = -1;
        s++;
    } else if (*s == '+') {
        mult = 1;
        s++;
    }

    while ((c = *(s++)) != '\r') {
        dec = c - '0';
        if (dec >= 0 && dec < 10) {
            v *= 10;
            v += dec;
        } else {
            /* Should not happen... */
            return -1;
        }
    }

    return mult*v;
}

static char *read_line(redis_reader *r, long *_len) {
    char *p, *s;
    long len;

    p = r->buf+r->pos;
    s = seek_newline(p,(r->len-r->pos));
    if (s != NULL) {
        len = s-(r->buf+r->pos);
        r->pos += len+2; /* skip \r\n */
        if (_len) *_len = len;
        return p;
    }
    return NULL;
}

static redis_reply *create_reply(void) {
    redis_reply *obj;
    if ((obj = malloc(sizeof(redis_reply))) == NULL)
        return NULL;
    memset(obj, 0, sizeof(redis_reply));    
    return obj;
}

static void free_reply(redis_reply *reply) {
    size_t i;
    if (!reply) return;
    if (reply->str) cdsfree(reply->str);
    for (i = 0; i < reply->total; i++) {
        if (reply->element[i].str)
            cdsfree(reply->element[i].str);
    }
    if (reply->element) free(reply->element);
    free(reply);
}

static void clear_reply(redis_reply *reply) {
    if (!reply) return;
    reply->type = 0; 
    reply->integer = 0;
    reply->elements = 0;
    if (reply->len) {
        reply->len = 0;
        cdsclear(reply->str);
    }
}

static redis_context *redis_context_init(void) {
    redis_context *c;

    if ((c = malloc(sizeof(redis_context))) == NULL)
        return NULL;
    c->err = c->fd = c->flags = 0;
    c->pipe = -1;
    c->errstr[0] = c->errstr[127] = 0;
    c->obuf = cdsnew(NULL);
    if (!c->obuf) {
        redis_free(c);
        return NULL;
    }
    return c;
}

void redis_free(redis_context *c) {
    if (!c) return;
    if (c->fd > 0) {
        close(c->fd);
    }
    if (c->obuf) cdsfree(c->obuf);
    free(c);
}

redis_reader *redis_create_reader(void) {
    redis_reader *r; 
    if ((r = malloc(sizeof(redis_reader))) == NULL)
        return NULL;
    memset(r, 0, sizeof(redis_reader));    
    r->buf = cdsnew(NULL);
    r->reply = create_reply();
    r->maxbuf = REDIS_READER_MAX_BUF;
    if (!r->buf || !r->reply) {
        redis_free_reader(r);
        return NULL;
    }
    r->c = NULL;
    return r;
}

void redis_free_reader(redis_reader *r) {
    if (!r) return;
    if (r->buf) cdsfree(r->buf);
    if (r->reply) free_reply(r->reply);
    free(r);
}

redis_reader *_redis_copy_reader(redis_reader *r) {
    redis_reader *newread; 
    if ((newread = malloc(sizeof(redis_reader))) == NULL)
        return NULL;
    memset(newread, 0, sizeof(redis_reader));    
    newread->buf = cdsdup(r->buf);
    newread->reply = create_reply();
    newread->maxbuf = REDIS_READER_MAX_BUF;
    if (!newread->buf || !newread->reply) {
        redis_free_reader(newread);
        return NULL;
    }
    return newread; 
}

static redis_context *redis_tcp_connect(char *ip, int port, size_t timeout) {
    redis_context *c;

    if ((c = redis_context_init()) == NULL)
        return NULL;
    c->flags |= REDIS_BLOCK;
    c->fd = csocket_tcp_connect(c->errstr, ip, port, timeout);
    if (c->fd <= 0) {
        redis_set_error(c, REDIS_ERR_IO, NULL);
    }
    return c;
}

redis_context *redis_connect(char *ip, int port) {
    return redis_tcp_connect(ip, port, 0);
}

redis_context *redis_connect_with_timeout(char *ip, int port, size_t timeout) {
    return redis_tcp_connect(ip, port, timeout);
}

static int redis_reconnect(redis_context *c, char *ip, int port, size_t timeout) {
    if (c->fd > 0) close(c->fd);
    c->fd = csocket_tcp_connect(c->errstr, ip, port, timeout);
    c->flags |= REDIS_BLOCK;
    return c->fd;
}

int redis_set_nonblock(redis_context *c) {
    c->flags &= ~REDIS_BLOCK;
    if (csocket_non_block(c->errstr, c->fd) == RET_ERR) {
        redis_set_error(c, REDIS_ERR_IO, NULL);
        return RET_ERR;
    }
    return RET_OK;
}

/* timeout:millsecond */
int redis_set_timeout(redis_context *c, size_t timeout) {
    if (c->flags & REDIS_BLOCK) {
        if (csocket_set_block_timeout(c->errstr, c->fd, timeout) == RET_ERR) {
            redis_set_error(c, REDIS_ERR_IO, NULL);
            return RET_ERR;
        }
    }
    return RET_OK;
}

static void redis_clear_writer(redis_context *c) {
    cdsclear(c->obuf);    
    c->pipe = -1;
}

static void redis_clear_reader(redis_reader *r) {
    cdsclear(r->buf);    
    if (r->err) r->err = r->errstr[0] = 0;
    r->readcount = 0;
    r->len = 0;
    r->pos = 0;
}

static int redis_v_format_command(char **target, const char *format, va_list ap) {
    const char *c = format;
    char *cmd = NULL; /* final command */
    int pos; /* position in final command */
    cds curarg, newarg; /* current argument */
    int touched = 0; /* was the current argument touched? */
    int spacedata = 0;
    char **curargv = NULL, **newargv = NULL;
    int argc = 0;
    int totlen = 0;
    int j;

    /* Abort if there is not target to set */
    if (target == NULL)
        return -1;

    /* Build the command string accordingly to protocol */
    curarg = cdsnew(NULL);
    if (curarg == NULL)
        return -1;

    while (*c != '\0') {
        if (*c != '%' || spacedata || c[1] == '\0') {
            if (*c == '\'') {
                spacedata = spacedata ? 0 : 1;
            }
            if (*c == ' ' && !spacedata) {
                if (touched) {
                    newargv = realloc(curargv,sizeof(char*)*(argc+1));
                    if (newargv == NULL) goto err;
                    curargv = newargv;
                    curargv[argc++] = curarg;
                    totlen += bulklen(cdslen(curarg));

                    /* curarg is put in argv so it can be overwritten. */
                    curarg = cdsnew(NULL);
                    if (curarg == NULL) goto err;
                    touched = 0;
                }
            } else if (*c != '\''){
                newarg = cdscatlen(curarg,c,1);
                if (newarg == NULL) goto err;
                curarg = newarg;
                touched = 1;
            }
        } else {
            char *arg;
            size_t size;

            /* Set newarg so it can be checked even if it is not touched. */
            newarg = curarg;

            switch(c[1]) {
                case 's':
                    arg = va_arg(ap,char*);
                    size = strlen(arg);
                    if (size > 0)
                        newarg = cdscatlen(curarg,arg,size);
                    break;
                case 'b':
                    arg = va_arg(ap,char*);
                    size = va_arg(ap,size_t);
                    if (size > 0)
                        newarg = cdscatlen(curarg,arg,size);
                    break;
                case '%':
                    newarg = cdscat(curarg,"%");
                    break;
                default:
                    /* Try to detect printf format */
                    {
                        static const char intfmts[] = "diouxX";
                        char _format[16];
                        const char *_p = c+1;
                        size_t _l = 0;
                        va_list _cpy;

                        /* Flags */
                        if (*_p != '\0' && *_p == '#') _p++;
                        if (*_p != '\0' && *_p == '0') _p++;
                        if (*_p != '\0' && *_p == '-') _p++;
                        if (*_p != '\0' && *_p == ' ') _p++;
                        if (*_p != '\0' && *_p == '+') _p++;

                        /* Field width */
                        while (*_p != '\0' && isdigit(*_p)) _p++;

                        /* Precision */
                        if (*_p == '.') {
                            _p++;
                            while (*_p != '\0' && isdigit(*_p)) _p++;
                        }

                        /* Copy va_list before consuming with va_arg */
                        va_copy(_cpy,ap);
                        /* Integer conversion (without modifiers) */
                        if (strchr(intfmts,*_p) != NULL) {
                            va_arg(ap,int);
                            goto fmt_valid;
                        }

                        /* Double conversion (without modifiers) */
                        if (strchr("eEfFgGaA",*_p) != NULL) {
                            va_arg(ap,double);
                            goto fmt_valid;
                        }

                        /* Size: char */
                        if (_p[0] == 'h' && _p[1] == 'h') {
                            _p += 2;
                            if (*_p != '\0' && strchr(intfmts,*_p) != NULL) {
                                va_arg(ap,int); /* char gets promoted to int */
                                goto fmt_valid;
                            }
                            goto fmt_invalid;
                        }
                        /* Size: short */
                        if (_p[0] == 'h') {
                            _p += 1;
                            if (*_p != '\0' && strchr(intfmts,*_p) != NULL) {
                                va_arg(ap,int); /* short gets promoted to int */
                                goto fmt_valid;
                            }
                            goto fmt_invalid;
                        }

                        /* Size: long long */
                        if (_p[0] == 'l' && _p[1] == 'l') {
                            _p += 2;
                            if (*_p != '\0' && strchr(intfmts,*_p) != NULL) {
                                va_arg(ap,long long);
                                goto fmt_valid;
                            }
                            goto fmt_invalid;
                        }

                        /* Size: long */
                        if (_p[0] == 'l') {
                            _p += 1;
                            if (*_p != '\0' && strchr(intfmts,*_p) != NULL) {
                                va_arg(ap,long);
                                goto fmt_valid;
                            }
                            goto fmt_invalid;
                        }
fmt_invalid:
                        va_end(_cpy);
                        goto err;

fmt_valid:
                        _l = (_p+1)-c;
                        if (_l < sizeof(_format)-2) {
                            memcpy(_format,c,_l);
                            _format[_l] = '\0';
                            newarg = cdscatvprintf(curarg,_format,_cpy);

                            /* Update current position (note: outer blocks
                             * increment c twice so compensate here) */
                            c = _p-1;
                        }

                        va_end(_cpy);
                        break;
                    }
            }

            if (newarg == NULL) goto err;
            curarg = newarg;

            touched = 1;
            c++;
        }
        c++;
    }

    /* Add the last argument if needed */
    if (touched) {
        newargv = realloc(curargv,sizeof(char*)*(argc+1));
        if (newargv == NULL) goto err;
        curargv = newargv;
        curargv[argc++] = curarg;
        totlen += bulklen(cdslen(curarg));
    } else {
        cdsfree(curarg);
    }

    /* Clear curarg because it was put in curargv or was free'd. */
    curarg = NULL;

    /* Add bytes needed to hold multi bulk count */
    totlen += 1+intlen(argc)+2;

    /* Build the command at protocol level */
    cmd = malloc(totlen+1);
    if (cmd == NULL) goto err;

    pos = sprintf(cmd,"*%d\r\n",argc);
    for (j = 0; j < argc; j++) {
#ifdef LINUX
        pos += sprintf(cmd+pos,"$%zu\r\n",cdslen(curargv[j]));
#else
        pos += sprintf(cmd+pos,"$%u\r\n",cdslen(curargv[j]));
#endif
        memcpy(cmd+pos,curargv[j],cdslen(curargv[j]));
        pos += cdslen(curargv[j]);
        cdsfree(curargv[j]);
        cmd[pos++] = '\r';
        cmd[pos++] = '\n';
    }
    //assert(pos == totlen);
    cmd[pos] = '\0';

    free(curargv);
    *target = cmd;
    return totlen;

err:
    while(argc--)
        cdsfree(curargv[argc]);
    free(curargv);

    if (curarg != NULL)
        cdsfree(curarg);

    /* No need to check cmd since it is the last statement that can fail,
     * but do it anyway to be as defensive as possible. */
    if (cmd != NULL)
        free(cmd);

    return -1;
}

static int redis_v_append_command(redis_context *c, const char *format, va_list ap) {
    char *cmd;
    int len;
    cds newbuf;

    len = redis_v_format_command(&cmd, format, ap); 
    if (len == RET_ERR) {
        goto err;
    }
    newbuf = cdscatlen(c->obuf, cmd, len);
    if (newbuf == NULL) {
        free(cmd);
        goto err;
    }
    c->pipe++;
    c->obuf = newbuf;
    free(cmd);
    return RET_OK;
err:
    redis_set_error(c, REDIS_ERR_OMM, "out of memory");
    return RET_ERR;
}

int redis_append_command(redis_context *c, const char *format, ...) {
    va_list ap;
    int ret; 

    va_start(ap,format);
    ret = redis_v_append_command(c, format, ap);
    va_end(ap);
    return ret; 
}

int redis_reader_feed(redis_reader *r, const char *buf, size_t len) {
    cds newbuf;

    /* Copy the provided buffer. */
    if (buf != NULL && len >= 1) {
        /* Destroy internal buffer when it is empty and is quite large. */
        if (r->len == 0 && r->maxbuf != 0 && cdsavail(r->buf) > r->maxbuf) {
            cdsfree(r->buf);
            r->buf = cdsnew(NULL);
            r->pos = 0;
            if (r->buf == NULL) {
                return RET_ERR;
            }
        }   
        newbuf = cdscatlen(r->buf, buf, len);
        if (newbuf == NULL) {
            return RET_ERR;
        }
        r->buf = newbuf;
        r->len = cdslen(r->buf);
    }

    return RET_OK;
}

int redis_buffer_read(redis_context *c, redis_reader *r, int flag) {
    char buf[1024*16];
    int nread;

    do {
        nread = read(c->fd,buf,sizeof(buf));
        if (nread == -1) {
            if ((errno == EAGAIN && !(c->flags & REDIS_BLOCK)) || (errno == EINTR)) {
                /* Try again later */
            } else {
                if (flag)
                    redis_reader_set_error(r, REDIS_ERR_IO, "errno=%d, errmsg=%s", __errno__, __errmsg__);
                else
                    redis_set_error(c, REDIS_ERR_IO, "errno=%d, errmsg=%s", __errno__, __errmsg__);
                return RET_ERR;
            }
        } else if (nread == 0) {
            if (flag)
                redis_reader_set_error(r, REDIS_ERR_EOF, "Server closed the connection");
            else
                redis_set_error(c, REDIS_ERR_EOF, "Server closed the connection");
            return RET_ERR;
        } else {
            if (redis_reader_feed(r, buf, nread) != RET_OK) {
                if (flag)
                    redis_reader_set_error(r, REDIS_ERR_OMM, "out of memory");
                else
                    redis_set_error(c, REDIS_ERR_OMM, "out of memory");
                return RET_ERR;
            }
        }
        //printf("%d, r->len=%d\n", nread, r->len);
    } while (nread == sizeof(buf));

    return RET_OK;
}

int redis_buffer_write(redis_context *c) {
    int nwritten, len, index = 0;

    len = cdslen(c->obuf);
    do {
        nwritten = write(c->fd, c->obuf+index, len-index);
        if (nwritten == -1) {
            if ((errno == EAGAIN && !(c->flags & REDIS_BLOCK)) || (errno == EINTR)) {
                /* Try again later */
            } else {
                redis_set_error(c, REDIS_ERR_IO, "errno=%d, errmsg=%s", __errno__, __errmsg__);
                return RET_ERR;
            }
        } else if (nwritten > 0) {
            index += nwritten;
            if (index == (signed)cdslen(c->obuf)) {
                cdsclear(c->obuf);
                break;
            }
        }
    } while (1);

    return RET_OK;
}

/* exec redis command */
int _redis_exec_command(redis_context *c, redis_reader *r) {
    if (c->err) c->err = c->errstr[0] = 0;
    if (cdslen(c->obuf) <= 0)
        return RET_ERR;

    if (c->flags & REDIS_BLOCK) {
        if (redis_buffer_write(c) == RET_ERR)
            goto err;
        redis_clear_reader(r);
    }
    r->c = c;
    redis_clear_writer(c);
    return RET_OK;

err:
    r->c = NULL;
    redis_clear_writer(c);
    return RET_ERR;
}

int redis_exec_command(redis_context *c, redis_reader *r) {
    return _redis_exec_command(c, r);
}

int redis_exec_command2(redis_context *c, redis_reader *r) {
    return _redis_exec_command(c, r);
}

int _redis_get_return_number(redis_reader *r) {
    size_t i, len, rows = 0;
    long strlen, multi = -1;
    char *p = r->buf;

    len = r->len;
    for (i = 0; i < len;) {
        switch (p[i]) {
            case '$':
                strlen = read_longlong(p+i+1);
                if (strlen > 0) i += strlen+3;
            case '+':
            case ':':
            case '-': 
                if (multi > 0)
                    multi--;
                else 
                    rows++;
                i += 2;
                break;
            case '*':
                multi = read_longlong(p+i+1);
                rows++;
                i += 2;
                break;
            default:
                i++;
                break;
        }
    }
    return rows;    
}

int redis_get_return_number(redis_reader *r) {
    return _redis_get_return_number(r);
}

static int continue_read_data(redis_reader *r) {
    if (!r->c) return 0;
    size_t len = r->len;
    redis_buffer_read(r->c, r, 1);
    r->readcount++;
    if (len == r->len)
        return 0;
    else
        return 1;
}

static int process_line_item(redis_reader *r, redis_reply *reply) {
    char *p; 
    long len;

reread:
    if ((p = read_line(r, &len)) == NULL) {
        if (continue_read_data(r)) goto reread;
        redis_reader_set_error(r, REDIS_ERR_PROTOCOL, "protocol error, parse failed");
        return RET_ERR;
    }
    if (reply->type == REDIS_REPLY_INTEGER) {
        reply->integer = read_longlong(p);
    } else {
        if (reply->str == NULL) {
            reply->str = cdsnewlen(p, len);
        } else {
            reply->str = cdscopylen(reply->str, p, len);
        }
        if (reply->str == NULL) {
            redis_reader_set_error(r, REDIS_ERR_OMM, "malloc memory error, errno=%d, errmsg=%s", 
                    __errno__, __errmsg__);
            return RET_ERR;
        }
        reply->len = len;
    }
    return RET_OK;
}

static int process_bulk_item(redis_reader *r, redis_reply *reply) {
    char *p;
    long len, plen;

reread1:
    if ((p = read_line(r, &plen)) == NULL) {
        if (continue_read_data(r)) goto reread1;
        redis_reader_set_error(r, REDIS_ERR_PROTOCOL, "protocol error, parse failed");
        return RET_ERR;
    }
    len = read_longlong(p);
    if (len < 0) {
        reply->type = REDIS_REPLY_NIL;
        if (reply->str && cdslen(reply->str) > 0)
            cdsclear(reply->str);
    } else {
reread2:
        if (((long)(r->len - r->pos) < len)) {
            if (continue_read_data(r)) goto reread2;
            redis_reader_set_error(r, REDIS_ERR_PROTOCOL, 
                    "protocol error, parse failed, %d,%d,%d", r->len, r->pos, len);
            return RET_ERR;
        }
        if (reply->str == NULL) {
            reply->str = cdsnewlen(p, len);
        } else {
            reply->str = cdscopylen(reply->str, p+plen+2, len);
            r->pos += len+2;
        }
        if (reply->str == NULL) {
            redis_reader_set_error(r, REDIS_ERR_OMM, "malloc memory error, errno=%d, errmsg=%s", 
                    __errno__, __errmsg__);
            return RET_ERR;
        }
    }
    reply->len = len;
    return RET_OK;
}

static int process_multi_bulk_item(redis_reader *r, redis_reply *reply) {
    char *p;
    int i;
    long l, len, elements;
    redis_reply *re;

reread1:
    if ((p = read_line(r, NULL)) == NULL) {
        if (continue_read_data(r)) goto reread1;
        redis_reader_set_error(r, REDIS_ERR_PROTOCOL, "protocol error, parse failed");
        return RET_ERR;
    }
    elements = read_longlong(p);
    if (elements < 0) {
        reply->type = REDIS_REPLY_NIL;
    } else {
        reply->elements = elements;
        reply->type = REDIS_REPLY_ARRAY;
        if (reply->elements > reply->total) {
            if (reply->element == NULL) {
                reply->element = malloc(sizeof(redis_reply)*elements);
            } else {
                reply->element = realloc(reply->element, sizeof(redis_reply)*elements);
            }
            if (reply->element == NULL) {
                redis_reader_set_error(r, REDIS_ERR_OMM, "malloc memory error, errno=%d, errmsg=%s", 
                        __errno__, __errmsg__);
                return RET_ERR;
            }
            memset(reply->element, 0, sizeof(redis_reply)*elements);
            reply->total = elements;
        } 

        for (i = 0; i < elements; i++) {
reread2:
            if ((p = read_bytes(r,1)) == NULL || p[0] != '$' || 
                    (p = read_line(r, NULL)) == NULL) {
                if (continue_read_data(r)) goto reread2;
                redis_reader_set_error(r, REDIS_ERR_PROTOCOL, "protocol error, parse failed");
                return RET_ERR;
            }
            len = read_longlong(p);
            re = &reply->element[i];
            clear_reply(re);
            if (len < 0) {
                re->type = REDIS_REPLY_NIL;
            } else {
reread3:
                if ((p = read_line(r, &l)) == NULL || len != l) {
                if (continue_read_data(r)) goto reread3;
                    redis_reader_set_error(r, REDIS_ERR_PROTOCOL, "protocol error, parse failed");
                    return RET_ERR;
                }
                if (re->str == NULL) {
                    re->str = cdsnewlen(p, len);
                } else {
                    re->str = cdscopylen(re->str, p, len);
                }
                if (re->str == NULL) {
                    redis_reader_set_error(r, REDIS_ERR_OMM, "malloc memory error, errno=%d, errmsg=%s", 
                            __errno__, __errmsg__);
                    return RET_ERR;
                }
                re->type = REDIS_REPLY_STRING;
                re->len = len;
            }
        }
    }
    return RET_OK;
}

static redis_reply *redis_parse_message(redis_reader *r, redis_reply *reply) {
    char *p;

    if (r->pos >= r->len) {
            redis_reader_set_error(r, REDIS_ERR_EOF, "no data"); 
        return NULL;
    }
    if ((p = read_bytes(r,1)) != NULL) {
        if (reply == NULL) {
            if ((r->reply = reply = create_reply()) == NULL) {
                redis_reader_set_error(r, REDIS_ERR_OMM, "malloc memory error, errno=%d, errmsg=%s", 
                        __errno__, __errmsg__);
                return NULL;
            }
        } else {
            clear_reply(reply);
        }
        
        switch (p[0]) {
            case '-': 
                reply->type = REDIS_REPLY_ERROR;
                break;
            case '+':
                reply->type = REDIS_REPLY_STATUS;
                break;
            case ':':
                reply->type = REDIS_REPLY_INTEGER;
                break;
            case '$':
                reply->type = REDIS_REPLY_STRING;
                break;
            case '*':
                reply->type = REDIS_REPLY_ARRAY;
                break;
            default:
                redis_reader_set_error(r, REDIS_ERR_PROTOCOL, "protocol error, unkown type");
                return NULL;
        }
    } else {
        return NULL;
    }        
  
    int ret;
    switch (reply->type) {
        case REDIS_REPLY_ERROR:
        case REDIS_REPLY_STATUS:
        case REDIS_REPLY_INTEGER:
            ret = process_line_item(r, reply);
            break;
        case REDIS_REPLY_STRING:
            ret = process_bulk_item(r, reply);
            break;
        case REDIS_REPLY_ARRAY:
            ret = process_multi_bulk_item(r, reply);
            break;
        default:
            ret = RET_ERR;
    }
    
    if (ret == RET_ERR) {
        clear_reply(reply);
        return NULL;
    }
    return reply;
}

redis_reply *redis_get_reply(redis_reader *r) {
    if (r == NULL) return NULL;
    if (r->err) r->err = r->errstr[0] = 0;
    if (!r->readcount) {
        if (redis_buffer_read(r->c, r, 0) == RET_ERR)
            return NULL;
        r->readcount++;
    }
    return redis_parse_message(r, r->reply);
}

static int redis_async_auth(char *errstr, redis_async_context *ac, char *pass) {
    int ret;
    redis_reply *reply;
    redis_context *c = ac->c;
    redis_reader *r = ac->r;

    redis_append_command(c, "auth %s", pass);
    ret = redis_exec_command(c, r);
    if (ret == -1) {
        if (errstr && c->err) strcpy(errstr, c->errstr);
        return 0;
    }
    reply = redis_get_reply(r);
    if (reply == NULL) {
        if (errstr && r->err) strcpy(errstr, r->errstr);
        return 0;
    }
    if (reply->type == REDIS_REPLY_STATUS && reply->len && strcmp(reply->str, "OK") == 0) {
        //auth ok           
    } else {
        if (errstr) strcpy(errstr, "redis auth failed"); 
        return 0;
    }
    return 1;
}

redis_async_context* redis_async_connect(char *errstr, char *ip, int port, char *pass) {
    redis_async_context *ac;    

    ac = malloc(sizeof(redis_async_context));
    ac->el = cel_create_event_loop(10);
    ac->r = redis_create_reader(); 
    ac->c = redis_connect_with_timeout(ip, port, 5*1000); 
    if (!ac || !ac->r || !ac->c || !ac->el) {
        strcpy(errstr, "malloc failed");
        goto err;
    }
    if (ac->c->err) {
        strcpy(errstr, ac->c->errstr);
        goto err;
    }
    if (pass) {
        if (!redis_async_auth(errstr, ac, pass))
            goto err;
        strcpy(ac->passwd, pass);
    } else {
        ac->passwd[0] = 0;
    }
    ac->port = port;
    strcpy(ac->ip, ip);
    ac->status = 1;
    return ac;

err:
    redis_async_free(ac);
    return NULL;
}

void redis_async_free(redis_async_context *ac) {
    if (!ac) return;
    if (ac->r) redis_free_reader(ac->r);
    if (ac->c) redis_free(ac->c);    
    if (ac->el) cel_delete_event_loop(ac->el);
    free(ac);
}

void redis_async_set_read_callback(redis_async_context *ac, redis_callback_function *fn) {
    ac->fn_read = fn; 
}

void redis_async_set_reconnect_callback(redis_async_context *ac, redis_callback_function *fn) {
    ac->fn_reconnect = fn; 
}

static void redis_async_read_event(struct st_event_loop *el, int fd, void *clientdata, int mask) {
    redis_async_context *ac = (redis_async_context *)clientdata;

    NOMORE(mask);
    redis_clear_reader(ac->r);
    if (redis_buffer_read(ac->c, ac->r, 0) == RET_ERR) {
        if (ac->c->err == REDIS_ERR_EOF) {
            ac->status = 0;
            cel_del_file_event(el, fd, EL_READABLE);
            //printf("err=%s\n", ac->c->errstr);
        }
    } else {
        if (ac->fn_read) ac->fn_read(ac);
    }
}

static int redis_async_reconnect(struct st_event_loop *el, int id, void *clientdata) {
    redis_async_context *ac = (redis_async_context *)clientdata;
    int ret;

    NOMORE(id);
    if (!ac->status) {
        //printf("try connect redis\n");
        if ((ret = redis_reconnect(ac->c, ac->ip, ac->port, 5000)) > 0) {
            if (ac->passwd[0] && !redis_async_auth(NULL, ac, ac->passwd))
                goto end;
            if (ac->fn_reconnect) ac->fn_reconnect(ac);
            redis_set_nonblock(ac->c);
            ret = cel_add_file_event(el, ac->c->fd, EL_READABLE, redis_async_read_event, ac);
            if (ret == EL_ERR) goto end;
            ac->status = 1; 
            //printf("redis reconnect success\n");
        }
    }

end:
    return 3*1000;
}

void redis_async_run(redis_async_context *ac) {
    int ret;

    redis_set_nonblock(ac->c);
    ret = cel_add_file_event(ac->el, ac->c->fd, EL_READABLE, redis_async_read_event, ac);
    if (ret == EL_ERR) return;
    ret = cel_add_timer_event(ac->el, 3*1000, redis_async_reconnect, ac);
    if (ret == EL_ERR) return;
    cel_main(ac->el);    
}


