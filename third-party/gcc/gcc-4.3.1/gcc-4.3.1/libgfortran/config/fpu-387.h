/* FPU-related code for x86 and x86_64 processors.
   Copyright 2005, 2007 Free Software Foundation, Inc.
   Contributed by Francois-Xavier Coudert <coudert@clipper.ens.fr>

This file is part of the GNU Fortran 95 runtime library (libgfortran).

Libgfortran is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

Libgfortran is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public
License along with libgfortran; see the file COPYING.  If not,
write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#ifndef __x86_64__
#include "cpuid.h"
#endif

static int
has_sse (void)
{
#ifndef __x86_64__
  unsigned int eax, ebx, ecx, edx;

  if (!__get_cpuid (1, &eax, &ebx, &ecx, &edx))
    return 0;

  return edx & bit_SSE;
#else
  return 1;
#endif
}

/* i387 -- see linux <fpu_control.h> header file for details.  */
#define _FPU_MASK_IM  0x01
#define _FPU_MASK_DM  0x02
#define _FPU_MASK_ZM  0x04
#define _FPU_MASK_OM  0x08
#define _FPU_MASK_UM  0x10
#define _FPU_MASK_PM  0x20

void set_fpu (void)
{
  unsigned short cw;

  asm volatile ("fnstcw %0" : "=m" (cw));

  cw |= (_FPU_MASK_IM | _FPU_MASK_DM | _FPU_MASK_ZM | _FPU_MASK_OM
	 | _FPU_MASK_UM | _FPU_MASK_PM);

  if (options.fpe & GFC_FPE_INVALID) cw &= ~_FPU_MASK_IM;
  if (options.fpe & GFC_FPE_DENORMAL) cw &= ~_FPU_MASK_DM;
  if (options.fpe & GFC_FPE_ZERO) cw &= ~_FPU_MASK_ZM;
  if (options.fpe & GFC_FPE_OVERFLOW) cw &= ~_FPU_MASK_OM;
  if (options.fpe & GFC_FPE_UNDERFLOW) cw &= ~_FPU_MASK_UM;
  if (options.fpe & GFC_FPE_PRECISION) cw &= ~_FPU_MASK_PM;

  asm volatile ("fldcw %0" : : "m" (cw));

  if (has_sse())
    {
      unsigned int cw_sse;

      asm volatile ("stmxcsr %0" : "=m" (cw_sse));

      cw_sse &= 0xffff0000;
      cw_sse |= (_FPU_MASK_IM | _FPU_MASK_DM | _FPU_MASK_ZM | _FPU_MASK_OM
		 | _FPU_MASK_UM | _FPU_MASK_PM ) << 7;

      if (options.fpe & GFC_FPE_INVALID) cw_sse &= ~(_FPU_MASK_IM << 7);
      if (options.fpe & GFC_FPE_DENORMAL) cw_sse &= ~(_FPU_MASK_DM << 7);
      if (options.fpe & GFC_FPE_ZERO) cw_sse &= ~(_FPU_MASK_ZM << 7);
      if (options.fpe & GFC_FPE_OVERFLOW) cw_sse &= ~(_FPU_MASK_OM << 7);
      if (options.fpe & GFC_FPE_UNDERFLOW) cw_sse &= ~(_FPU_MASK_UM << 7);
      if (options.fpe & GFC_FPE_PRECISION) cw_sse &= ~(_FPU_MASK_PM << 7);

      asm volatile ("ldmxcsr %0" : : "m" (cw_sse));
    }
}
