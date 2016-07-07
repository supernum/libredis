/*
 * Author: jiaofx
 * Description: The header file of type
 */

#ifndef __CC_TYPE_H__
#define __CC_TYPE_H__

#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>

#ifndef FALSE
 #define FALSE        (0)
#endif
#ifndef TRUE
 #define TRUE         (1)
#endif

#define RET_ERR      (-1)
#define RET_OK       (0)
#define RET_CONTINUE (254)

/*
 * int*_t
 * INT*_MIN
 * INT*_MAX
 * INTMAX_MIN
 * INTMAX_MAX
 * intptr_t
 * uintptr_t
 * intmax_t
 * INT64_C
 * */

#define _1B 1
#define _1K 1024
#define _1M 1048576
#define _1G 1073741824
#define _1T 1099511627776

#define NOMORE(v) ((void) v)
#define __errno__  errno
#define __ERRNO__  errno
#define __errmsg__ strerror(errno)
#define __ERRMSG__ strerror(errno)
#define cprintf(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)

#endif



