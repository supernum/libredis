/*
 * Author: jiaofx
 * Description: The header file of CDS(C language dynamic string)
 */

#ifndef __CC_CDS_H__
#define __CC_CDS_H__

#include <sys/types.h>
#include <stdarg.h>

typedef char *cds;

typedef struct st_cds {
	unsigned int len;
	unsigned int free;
	char buf[];
} cds_t;

static inline size_t cdslen(const cds s) {
	cds_t *ds = (void *)(s-sizeof(cds_t));
	return ds->len;
}

static inline size_t cdsavail(const cds s) {
	cds_t *ds = (void *)(s-sizeof(cds_t));
	return ds->free;
}

cds cdsnewlen(const void *init, size_t initlen);
cds cdsnew(const char *s);
cds cdsdup(const cds s);
void cdsfree(cds s);
void cdsclear(cds s);
cds cdsmakeroom(cds s, size_t addlen);
cds cdscatlen(cds s, const void *t, size_t len);
cds cdscat(cds s, const char *t);
cds cdscopylen(cds s, char *t, size_t len);
cds cdscopy(cds s, char *t);
int cdscmp(const cds s1, const cds s2);
cds cdscatvprintf(cds s, const char *fmt, va_list ap);

#endif



