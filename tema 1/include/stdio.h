/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef __STDIO_H__
#define __STDIO_H__	1

#ifdef __cplusplus
extern "C" {
#endif

#define stdin   0
#define stdout  1
#define stderr  2

#define EOF -1

int puts(const char *str);

#ifdef __cplusplus
}
#endif

#endif
