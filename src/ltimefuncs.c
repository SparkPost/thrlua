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

#include "ltimefuncs.h"

int compare_times(struct timeval a, struct timeval b)
{
  if (a.tv_sec < b.tv_sec)
    return -1;
  if (a.tv_sec > b.tv_sec)
    return 1;
  if (a.tv_usec < b.tv_usec)
    return -1;
  if (a.tv_usec > b.tv_usec)
    return 1;
  return 0;
}

void add_times(struct timeval a, struct timeval b, struct timeval *result)
{
  result->tv_usec = a.tv_usec + b.tv_usec;
  result->tv_sec = a.tv_sec + b.tv_sec;
  if (result->tv_usec > 1000000L) {
    result->tv_sec += 1L;
    result->tv_usec -= 1000000L;
  }
}

void sub_times(struct timeval a, struct timeval b, struct timeval *result)
{
  result->tv_usec = a.tv_usec - b.tv_usec;
  if (result->tv_usec < 0L) {
    a.tv_sec--;
    result->tv_usec += 1000000L;
  }
  result->tv_sec = a.tv_sec - b.tv_sec;
  if (result->tv_sec < 0L) {
    result->tv_sec++;
    result->tv_usec -= 1000000L;
  }
}

/* vim:ts=2:sw=2:et:
 */
