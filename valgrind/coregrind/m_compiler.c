/* -*- mode: C; c-basic-offset: 3; -*- */

/*--------------------------------------------------------------------*/
/*--- Compiler specific stuff.                        m_compiler.c ---*/
/*--------------------------------------------------------------------*/
 
/*
   This file is part of Valgrind, a dynamic binary instrumentation
   framework.

   Copyright (C) 2014-2014 Florian Krohm
      florian@eich-krohm.de

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

/* Currently, this file provides definitions for builtins that not all
   compilers or compiler versions provide.

   Missing builtins are rare. Therefore, no attempt has been made to
   provide efficient implementations.
 */

#include "config.h"
#include "pub_core_basics.h"
#include "pub_core_libcbase.h"

#ifndef HAVE_BUILTIN_POPCOUT

/* From the GCC documentation:
   Returns the number of 1-bits in x. */

UInt
__builtin_popcount(UInt x)
{
   UInt i, count = 0;

   for (i = 0; i < 32; ++i) {
      count += x & 1;
      x >>= 1;
   }
   return count;
}

UInt
__builtin_popcountll(ULong x)
{
   UInt i, count = 0;

   for (i = 0; i < 64; ++i) {
      count += x & 1;
      x >>= 1;
   }
   return count;
}
#endif

#ifndef HAVE_BUILTIN_CLZ

/* From the GCC documentation:
   Returns the number of leading 0-bits in x, starting at the most
   significant position. If x is 0, the result is undefined. */

UInt
__builtin_clz(UInt x)
{
   UInt count = 32;
   UInt y;

   y = x >> 16; if (y != 0) { count -= 16; x = y; }
   y = x >> 8;  if (y != 0) { count -= 8;  x = y; }
   y = x >> 4;  if (y != 0) { count -= 4;  x = y; }
   y = x >> 2;  if (y != 0) { count -= 2;  x = y; }
   y = x >> 1;  if (y != 0) return count - 2;
   return count - x;
}

UInt
__builtin_clzll(ULong x)
{
   UInt count = 64;
   ULong y;

   y = x >> 32; if (y != 0) { count -= 32; x = y; }
   y = x >> 16; if (y != 0) { count -= 16; x = y; }
   y = x >> 8;  if (y != 0) { count -= 8;  x = y; }
   y = x >> 4;  if (y != 0) { count -= 4;  x = y; }
   y = x >> 2;  if (y != 0) { count -= 2;  x = y; }
   y = x >> 1;  if (y != 0) return count - 2;
   return count - x;
}
#endif

#ifndef HAVE_BUILTIN_CTZ

/* From the GCC documentation:
   Returns the number of trailing 0-bits in x, starting at the least
   significant bit position. If x is 0, the result is undefined. */

UInt
__builtin_ctz(UInt x)
{
   UInt i, count = 0;

   for (i = 0; i < 32; ++i) {
      if (x & 1) break;
      ++count;
      x >>= 1;
   }
   return count;
}

UInt
__builtin_ctzll(ULong x)
{
   UInt i, count = 0;

   for (i = 0; i < 64; ++i) {
      if (x & 1) break;
      ++count;
      x >>= 1;
   }
   return count;
}
#endif


#ifdef __INTEL_COMPILER

/* Provide certain functions Intel's ICC compiler expects to be defined. */

void *
_intel_fast_memcpy(void *dest, const void *src, SizeT sz)
{
   return VG_(memcpy)( dest, src, sz );
}

void *
_intel_fast_memset(void *dest, int value, SizeT num)
{
   return VG_(memset)( dest, value, num );    
}
#endif

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/

