/* Test file for mpfr_log1p.

Copyright 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008 Free Software Foundation, Inc.
Contributed by the Arenaire and Cacao projects, INRIA.

This file is part of the MPFR Library.

The MPFR Library is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version.

The MPFR Library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
License for more details.

You should have received a copy of the GNU Lesser General Public License
along with the MPFR Library; see the file COPYING.LIB.  If not, write to
the Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
MA 02110-1301, USA. */

#include <stdio.h>
#include <stdlib.h>

#include "mpfr-test.h"

#ifdef CHECK_EXTERNAL
static int
test_log1p (mpfr_ptr a, mpfr_srcptr b, mp_rnd_t rnd_mode)
{
  int res;
  int ok = rnd_mode == GMP_RNDN && mpfr_number_p (b) && mpfr_get_prec (a)>=53;
  if (ok)
    {
      mpfr_print_raw (b);
    }
  res = mpfr_log1p (a, b, rnd_mode);
  if (ok)
    {
      printf (" ");
      mpfr_print_raw (a);
      printf ("\n");
    }
  return res;
}
#else
#define test_log1p mpfr_log1p
#endif

#define TEST_FUNCTION test_log1p
#include "tgeneric.c"

static void
special (void)
{
  mpfr_t x;
  int inex;

  mpfr_init (x);

  mpfr_set_nan (x);
  inex = test_log1p (x, x, GMP_RNDN);
  MPFR_ASSERTN (mpfr_nan_p (x) && inex == 0);

  mpfr_set_inf (x, -1);
  inex = test_log1p (x, x, GMP_RNDN);
  MPFR_ASSERTN (mpfr_nan_p (x) && inex == 0);

  mpfr_set_inf (x, 1);
  inex = test_log1p (x, x, GMP_RNDN);
  MPFR_ASSERTN (mpfr_inf_p (x) && mpfr_sgn (x) > 0 && inex == 0);

  mpfr_set_ui (x, 0, GMP_RNDN);
  inex = test_log1p (x, x, GMP_RNDN);
  MPFR_ASSERTN (mpfr_cmp_ui (x, 0) == 0 && MPFR_IS_POS (x) && inex == 0);
  mpfr_neg (x, x, GMP_RNDN);
  inex = test_log1p (x, x, GMP_RNDN);
  MPFR_ASSERTN (mpfr_cmp_ui (x, 0) == 0 && MPFR_IS_NEG (x) && inex == 0);

  mpfr_set_si (x, -1, GMP_RNDN);
  inex = test_log1p (x, x, GMP_RNDN);
  MPFR_ASSERTN (mpfr_inf_p (x) && mpfr_sgn (x) < 0 && inex == 0);

  mpfr_set_si (x, -2, GMP_RNDN);
  inex = test_log1p (x, x, GMP_RNDN);
  MPFR_ASSERTN (mpfr_nan_p (x) && inex == 0);

  mpfr_clear (x);
}

static void
other (void)
{
  mpfr_t x, y;

  /* Bug reported by Guillaume Melquiond on 2006-08-14. */
  mpfr_init2 (x, 53);
  mpfr_set_str (x, "-1.5e4f72873ed9a@-100", 16, GMP_RNDN);
  mpfr_init2 (y, 57);
  mpfr_log1p (y, x, GMP_RNDU);
  if (mpfr_cmp (x, y) != 0)
    {
      printf ("Error in tlog1p for x = ");
      mpfr_out_str (stdout, 16, 0, x, GMP_RNDN);
      printf (", rnd = GMP_RNDU\nExpected ");
      mpfr_out_str (stdout, 16, 15, x, GMP_RNDN);
      printf ("\nGot      ");
      mpfr_out_str (stdout, 16, 15, y, GMP_RNDN);
      printf ("\n");
      exit (1);
    }

  mpfr_clear (y);
  mpfr_clear (x);
  return;
}

int
main (int argc, char *argv[])
{
  tests_start_mpfr ();

  special ();
  other ();

  test_generic (2, 100, 50);

  data_check ("data/log1p", mpfr_log1p, "mpfr_log1p");

  tests_end_mpfr ();
  return 0;
}
