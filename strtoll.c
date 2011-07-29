/* implementation adapted from google android bionic libc's
 * strntoumax and strntoimax (removing the dependency on 'n')
 */

/*-
 * Copyright (c) 1992 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

static inline int digitval(int ch)
{
  unsigned  d;

  d = (unsigned)(ch - '0');
  if (d < 10) return (int)d;

  d = (unsigned)(ch - 'a');
  if (d < 6) return (int)(d+10);

  d = (unsigned)(ch - 'A');
  if (d < 6) return (int)(d+10);

  return -1;
}

/*
 * Convert a string to an unsigned long long.
 *
 * Ignores `locale' stuff.  Assumes that the upper and lower case
 * alphabets and digits are each contiguous.
 */
unsigned long long
strtoull(const char *nptr, char **endptr, int base)
{
  const unsigned char*  p   = nptr;
  const unsigned char*  end;
  int                   minus = 0;
  unsigned long long    v = 0;
  int                   d;
  end = p + strlen(nptr);

  /* skip leading space */
  while (p < end && isspace(*p))
    p++;

  /* Single optional + or - */
  if (p < end)
    {
      char c = p[0];
      if ( c == '-' || c == '+' )
        {
          minus = (c == '-');
          p++;
        }
    }

  if ( base == 0 )
    {
      if ( p+2 < end && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') )
        {
          p += 2;
          base = 16;
        }
      else if ( p+1 < end && p[0] == '0' )
        {
          p   += 1;
          base = 8;
        }
      else
        {
          base = 10;
        }
    }
  else if ( base == 16 )
    {
      if ( p+2 < end && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') )
        {
          p += 2;
        }
    }

  while ( p < end && (d = digitval(*p)) >= 0 && d < base )
    {
      v = v*base + d;
      p += 1;
    }

  if ( endptr )
    *endptr = (char *)p;

  return minus ? -v : v;
}

long long
strtoll(const char *nptr, char **endptr, int base)
{
  return (long long) strtoull(nptr, endptr, base);
}

