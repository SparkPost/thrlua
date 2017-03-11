/*
 * Copyright (c) 2001-2017 Message Systems, Inc. All rights reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MESSAGE SYSTEMS
 * The copyright notice above does not evidence any
 * actual or intended publication of such source code.
 *
 * Redistribution of this material is strictly prohibited.
 *
 */

/* Based on timefuncs.h from the ecelerity source code */

#ifndef lua_timefuncs_h
#define lua_timefuncs_h

#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

int compare_times(struct timeval a, struct timeval b);
void add_times(struct timeval a, struct timeval b, struct timeval *result);
void sub_times(struct timeval a, struct timeval b, struct timeval *result);

#ifdef __cplusplus
}  /* Close scope of 'extern "C"' declaration which encloses file. */
#endif

#endif /* lua_timefuncs_h */

/* vim:ts=2:sw=2:et:
 */
