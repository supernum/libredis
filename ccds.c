/*
 * Author: jiaofx
 * Description: The source file of cds
 */
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <stdio.h>
#include "ccds.h"


cds cdsnewlen(const void *init, size_t initlen) {
	cds_t *ds;

    ds = malloc(sizeof(cds_t)+initlen+1);
	if (ds == NULL) return NULL;
    if (!init && initlen) {
        ds->len = 0;
        ds->free = initlen;
        ds->buf[0] = 0;
    } else {
        ds->len = initlen;
        ds->free = 0;
        ds->buf[initlen] = 0;
    }
	if (initlen && init)
		memcpy(ds->buf, init, initlen);
	return ds->buf;

}

cds cdsnew(const char *s) {
	size_t initlen = (s == NULL) ? 0 : strlen(s);
	return cdsnewlen(s, initlen);
}

cds cdsdup(const cds s) {
	return cdsnewlen(s, cdslen(s));
}

void cdsfree(cds s) {
	if (s == NULL) return;
	free(s-sizeof(cds_t));
}

void cdsclear(cds s) {
	cds_t *ds = (void *)(s-(sizeof(cds_t)));
	ds->free += ds->len;
	ds->len = 0;
	ds->buf[0] = 0;
}

cds cdsmakeroom(cds s, size_t addlen) {
	cds_t *ds;
	size_t len, newlen;
	size_t free = cdsavail(s);		
	
	if (free >= addlen) return s;
	len = cdslen(s);
	newlen = (len + addlen) * 2;
	ds = (void *)(s - sizeof(cds_t));
	ds = realloc(ds, (newlen + sizeof(cds_t) + 1));
	if (ds == NULL) return NULL;
	ds->free = newlen - len;
	return ds->buf;
}

cds cdscatlen(cds s, const void *t, size_t len) {
	cds_t *ds;
	size_t curlen = cdslen(s);	

	s = cdsmakeroom(s, len);
	if (s == NULL) return NULL;
	ds = (void *) (s - sizeof(cds_t));
	memcpy(s+curlen, t, len);
	ds->len += len;
	ds->free -= len;
	s[ds->len] = 0;
	return s;
}

cds cdscat(cds s, const char *t) {
	return cdscatlen(s, t, strlen(t));
}

cds cdscopylen(cds s, char *t, size_t len) {
	cds_t *ds;
	size_t total;

    total = cdslen(s) + cdsavail(s);	
    if (total < len) {
        s = cdsmakeroom(s, len);
        if (s == NULL) return NULL;
        total = cdslen(s) + cdsavail(s);	
    }
	ds = (void *) (s - sizeof(cds_t));
	memcpy(s, t, len);
    ds->len = len;
    ds->free = total-len;
    s[len] = 0;
    return s;
}

cds cdscopy(cds s, char *t) {
    return cdscopylen(s, t, strlen(t)); 
}

int cdscmp(const cds s1, const cds s2) {
	size_t l1, l2, l3;

	l1 = cdslen(s1);
	l2 = cdslen(s2);
	l3 = l1 - l2;
	if (l3 != 0) return l3;
	return memcmp(s1, s2, l1);
}

/* Like sdscatpritf() but gets va_list instead of being variadic. */
cds cdscatvprintf(cds s, const char *fmt, va_list ap) {
    va_list cpy;
    char staticbuf[1024], *buf = staticbuf, *t;
    size_t buflen = strlen(fmt)*2;

    /* We try to start using a static buffer for speed.
     * If not possible we revert to heap allocation. */
    if (buflen > sizeof(staticbuf)) {
        buf = malloc(buflen);
        if (buf == NULL) return NULL;
    } else {
        buflen = sizeof(staticbuf);
    }

    /* Try with buffers two times bigger every time we fail to
     * fit the string in the current buffer size. */
    while(1) {
        buf[buflen-2] = '\0';
        va_copy(cpy,ap);
        vsnprintf(buf, buflen, fmt, cpy);
        va_end(ap);
        if (buf[buflen-2] != '\0') {
            if (buf != staticbuf) free(buf);
            buflen *= 2;
            buf = malloc(buflen);
            if (buf == NULL) return NULL;
            continue;
        }
        break;
    }

    /* Finally concat the obtained string to the SDS string and return it. */
    t = cdscat(s, buf);
    if (buf != staticbuf) free(buf);
    return t;
}


