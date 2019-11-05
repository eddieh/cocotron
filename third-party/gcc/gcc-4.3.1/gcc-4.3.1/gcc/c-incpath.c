/* Set up combined include path chain for the preprocessor.
   Copyright (C) 1986, 1987, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

   Broken out of cppinit.c and cppfiles.c and rewritten Mar 2003.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3, or (at your option) any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "machmode.h"
#include "target.h"
#include "tm.h"
#include "cpplib.h"
#include "prefix.h"
#include "intl.h"
#include "c-incpath.h"
#include "cppdefault.h"

/* Windows does not natively support inodes, and neither does MSDOS.
   Cygwin's emulation can generate non-unique inodes, so don't use it.
   VMS has non-numeric inodes.  */
#ifdef VMS
# define INO_T_EQ(A, B) (!memcmp (&(A), &(B), sizeof (A)))
# define INO_T_COPY(DEST, SRC) memcpy(&(DEST), &(SRC), sizeof (SRC))
#else
# if (defined _WIN32 && !defined (_UWIN)) || defined __MSDOS__
#  define INO_T_EQ(A, B) 0
# else
#  define INO_T_EQ(A, B) ((A) == (B))
# endif
# define INO_T_COPY(DEST, SRC) (DEST) = (SRC)
#endif

static const char dir_separator_str[] = { DIR_SEPARATOR, 0 };

static void add_env_var_paths (const char *, int);
static void add_standard_paths (const char *, const char *, const char *, int);
static void free_path (struct cpp_dir *, int);
static void merge_include_chains (const char *, cpp_reader *, int);
static void add_sysroot_to_chain (const char *, int);
static struct cpp_dir *remove_duplicates (cpp_reader *, struct cpp_dir *,
					   struct cpp_dir *,
					   struct cpp_dir *, int);

/* Include chains heads and tails.  */
static struct cpp_dir *heads[4];
static struct cpp_dir *tails[4];
static bool quote_ignores_source_dir;
enum { REASON_QUIET = 0, REASON_NOENT, REASON_DUP, REASON_DUP_SYS };

/* Free an element of the include chain, possibly giving a reason.  */
static void
free_path (struct cpp_dir *path, int reason)
{
  switch (reason)
    {
    case REASON_DUP:
    case REASON_DUP_SYS:
      fprintf (stderr, _("ignoring duplicate directory \"%s\"\n"), path->name);
      if (reason == REASON_DUP_SYS)
	fprintf (stderr,
 _("  as it is a non-system directory that duplicates a system directory\n"));
      break;

    case REASON_NOENT:
      fprintf (stderr, _("ignoring nonexistent directory \"%s\"\n"),
	       path->name);
      break;

    case REASON_QUIET:
    default:
      break;
    }

  free (path->name);
  free (path);
}

/* Read ENV_VAR for a PATH_SEPARATOR-separated list of file names; and
   append all the names to the search path CHAIN.  */
static void
add_env_var_paths (const char *env_var, int chain)
{
  char *p, *q, *path;

  GET_ENVIRONMENT (q, env_var);

  if (!q)
    return;

  for (p = q; *q; p = q + 1)
    {
      q = p;
      while (*q != 0 && *q != PATH_SEPARATOR)
	q++;

      if (p == q)
	path = xstrdup (".");
      else
	{
	  path = XNEWVEC (char, q - p + 1);
	  memcpy (path, p, q - p);
	  path[q - p] = '\0';
	}

      add_path (path, chain, chain == SYSTEM, false);
    }
}

/* Append the standard include chain defined in cppdefault.c.  */
static void
add_standard_paths (const char *sysroot, const char *iprefix,
		    const char *imultilib, int cxx_stdinc)
{
  const struct default_include *p;
  int relocated = cpp_relocated();
  size_t len;

  if (iprefix && (len = cpp_GCC_INCLUDE_DIR_len) != 0)
    {
      /* Look for directories that start with the standard prefix.
	 "Translate" them, i.e. replace /usr/local/lib/gcc... with
	 IPREFIX and search them first.  */
      for (p = cpp_include_defaults; p->fname; p++)
	{
	  if (!p->cplusplus || cxx_stdinc)
	    {
	      /* Should we be translating sysrooted dirs too?  Assume
		 that iprefix and sysroot are mutually exclusive, for
		 now.  */
	      if (sysroot && p->add_sysroot)
		continue;
	      if (!strncmp (p->fname, cpp_GCC_INCLUDE_DIR, len))
		{
		  char *str = concat (iprefix, p->fname + len, NULL);
		  if (p->multilib && imultilib)
		    str = concat (str, dir_separator_str, imultilib, NULL);
		  add_path (str, SYSTEM, p->cxx_aware, false);
		}
	    }
	}
    }

  for (p = cpp_include_defaults; p->fname; p++)
    {
      if (!p->cplusplus || cxx_stdinc)
	{
	  char *str;

	  /* Should this directory start with the sysroot?  */
	  if (sysroot && p->add_sysroot)
	    str = concat (sysroot, p->fname, NULL);
	  else if (!p->add_sysroot && relocated
		   && strncmp (p->fname, cpp_PREFIX, cpp_PREFIX_len) == 0)
	    {
 	      static const char *relocated_prefix;
	      /* If this path starts with the configure-time prefix, 
		 but the compiler has been relocated, replace it 
		 with the run-time prefix.  The run-time exec prefix
		 is GCC_EXEC_PREFIX.  Compute the path from there back
		 to the toplevel prefix.  */
	      if (!relocated_prefix)
		{
		  char *dummy;
		  /* Make relative prefix expects the first argument
		     to be a program, not a directory.  */
		  dummy = concat (gcc_exec_prefix, "dummy", NULL);
		  relocated_prefix 
		    = make_relative_prefix (dummy,
					    cpp_EXEC_PREFIX,
					    cpp_PREFIX);
		}
	      str = concat (relocated_prefix,
			    p->fname + cpp_PREFIX_len, 
			    NULL);
	      str = update_path (str, p->component);
	    }
	  else
	    str = update_path (p->fname, p->component);

	  if (p->multilib && imultilib)
	    str = concat (str, dir_separator_str, imultilib, NULL);

	  add_path (str, SYSTEM, p->cxx_aware, false);
	}
    }
}

/* For each duplicate path in chain HEAD, keep just the first one.
   Remove each path in chain HEAD that also exists in chain SYSTEM.
   Set the NEXT pointer of the last path in the resulting chain to
   JOIN, unless it duplicates JOIN in which case the last path is
   removed.  Return the head of the resulting chain.  Any of HEAD,
   JOIN and SYSTEM can be NULL.  */

static struct cpp_dir *
remove_duplicates (cpp_reader *pfile, struct cpp_dir *head,
		   struct cpp_dir *system, struct cpp_dir *join,
		   int verbose)
{
  struct cpp_dir **pcur, *tmp, *cur;
  struct stat st;

  for (pcur = &head; *pcur; )
    {
      int reason = REASON_QUIET;

      cur = *pcur;

      if (stat (cur->name, &st))
	{
	  /* Dirs that don't exist are silently ignored, unless verbose.  */
	  if (errno != ENOENT)
	    cpp_errno (pfile, CPP_DL_ERROR, cur->name);
	  else
	    {
	      /* If -Wmissing-include-dirs is given, warn.  */
	      cpp_options *opts = cpp_get_options (pfile);
	      if (opts->warn_missing_include_dirs && cur->user_supplied_p)
		cpp_errno (pfile, CPP_DL_WARNING, cur->name);
	      reason = REASON_NOENT;
	    }
	}
      else if (!S_ISDIR (st.st_mode))
	cpp_error_with_line (pfile, CPP_DL_ERROR, 0, 0,
			     "%s: not a directory", cur->name);
      else
	{
	  INO_T_COPY (cur->ino, st.st_ino);
	  cur->dev  = st.st_dev;

	  /* Remove this one if it is in the system chain.  */
	  reason = REASON_DUP_SYS;
	  for (tmp = system; tmp; tmp = tmp->next)
	   if (INO_T_EQ (tmp->ino, cur->ino) && tmp->dev == cur->dev
	       && cur->construct == tmp->construct)
	      break;

	  if (!tmp)
	    {
	      /* Duplicate of something earlier in the same chain?  */
	      reason = REASON_DUP;
	      for (tmp = head; tmp != cur; tmp = tmp->next)
	       if (INO_T_EQ (cur->ino, tmp->ino) && cur->dev == tmp->dev
		   && cur->construct == tmp->construct)
		  break;

	      if (tmp == cur
		  /* Last in the chain and duplicate of JOIN?  */
		  && !(cur->next == NULL && join
		       && INO_T_EQ (cur->ino, join->ino)
		      && cur->dev == join->dev
		      && cur->construct == join->construct))
		{
		  /* Unique, so keep this directory.  */
		  pcur = &cur->next;
		  continue;
		}
	    }
	}

      /* Remove this entry from the chain.  */
      *pcur = cur->next;
      free_path (cur, verbose ? reason: REASON_QUIET);
    }

  *pcur = join;
  return head;
}

/* Add SYSROOT to any user-supplied paths in CHAIN starting with
   "=".  */

static void
add_sysroot_to_chain (const char *sysroot, int chain)
{
  struct cpp_dir *p;

  for (p = heads[chain]; p != NULL; p = p->next)
    if (p->name[0] == '=' && p->user_supplied_p)
      p->name = concat (sysroot, p->name + 1, NULL);
}

/* Merge the four include chains together in the order quote, bracket,
   system, after.  Remove duplicate dirs (as determined by
   INO_T_EQ()).

   We can't just merge the lists and then uniquify them because then
   we may lose directories from the <> search path that should be
   there; consider -iquote foo -iquote bar -Ifoo -Iquux.  It is
   however safe to treat -iquote bar -iquote foo -Ifoo -Iquux as if
   written -iquote bar -Ifoo -Iquux.  */

static void
merge_include_chains (const char *sysroot, cpp_reader *pfile, int verbose)
{
  /* Add the sysroot to user-supplied paths starting with "=".  */
  if (sysroot)
    {
      add_sysroot_to_chain (sysroot, QUOTE);
      add_sysroot_to_chain (sysroot, BRACKET);
      add_sysroot_to_chain (sysroot, SYSTEM);
      add_sysroot_to_chain (sysroot, AFTER);
    }

  /* Join the SYSTEM and AFTER chains.  Remove duplicates in the
     resulting SYSTEM chain.  */
  if (heads[SYSTEM])
    tails[SYSTEM]->next = heads[AFTER];
  else
    heads[SYSTEM] = heads[AFTER];
  heads[SYSTEM] = remove_duplicates (pfile, heads[SYSTEM], 0, 0, verbose);

  /* Remove duplicates from BRACKET that are in itself or SYSTEM, and
     join it to SYSTEM.  */
  heads[BRACKET] = remove_duplicates (pfile, heads[BRACKET], heads[SYSTEM],
				      heads[SYSTEM], verbose);

  /* Remove duplicates from QUOTE that are in itself or SYSTEM, and
     join it to BRACKET.  */
  heads[QUOTE] = remove_duplicates (pfile, heads[QUOTE], heads[SYSTEM],
				    heads[BRACKET], verbose);

  /* If verbose, print the list of dirs to search.  */
  if (verbose)
    {
      struct cpp_dir *p;

      fprintf (stderr, _("#include \"...\" search starts here:\n"));
      for (p = heads[QUOTE];; p = p->next)
	{
	  if (p == heads[BRACKET])
	    fprintf (stderr, _("#include <...> search starts here:\n"));
	  if (!p)
	    break;
	  fprintf (stderr, " %s\n", p->name);
	}
      fprintf (stderr, _("End of search list.\n"));
    }
}

/* Use given -I paths for #include "..." but not #include <...>, and
   don't search the directory of the present file for #include "...".
   (Note that -I. -I- is not the same as the default setup; -I. uses
   the compiler's working dir.)  */
void
split_quote_chain (void)
{
  heads[QUOTE] = heads[BRACKET];
  tails[QUOTE] = tails[BRACKET];
  heads[BRACKET] = NULL;
  tails[BRACKET] = NULL;
  /* This is NOT redundant.  */
  quote_ignores_source_dir = true;
}

/* Add P to the chain specified by CHAIN.  */

void
add_cpp_dir_path (cpp_dir *p, int chain)
{
  if (tails[chain])
    tails[chain]->next = p;
  else
    heads[chain] = p;
  tails[chain] = p;
}

/* Add PATH to the include chain CHAIN. PATH must be malloc-ed and
   NUL-terminated.  */
void
add_path (char *path, int chain, int cxx_aware, bool user_supplied_p)
{
  cpp_dir *p;

#if defined (HAVE_DOS_BASED_FILE_SYSTEM)
  /* Remove unnecessary trailing slashes.  On some versions of MS
     Windows, trailing  _forward_ slashes cause no problems for stat().
     On newer versions, stat() does not recognize a directory that ends
     in a '\\' or '/', unless it is a drive root dir, such as "c:/",
     where it is obligatory.  */
  int pathlen = strlen (path);
  char* end = path + pathlen - 1;
  /* Preserve the lead '/' or lead "c:/".  */
  char* start = path + (pathlen > 2 && path[1] == ':' ? 3 : 1);
  
  for (; end > start && IS_DIR_SEPARATOR (*end); end--)
    *end = 0;
#endif

  p = XNEW (cpp_dir);
  p->next = NULL;
  p->name = path;
  if (chain == SYSTEM || chain == AFTER)
    p->sysp = 1 + !cxx_aware;
  else
    p->sysp = 0;
  p->construct = 0;
  p->user_supplied_p = user_supplied_p;

  add_cpp_dir_path (p, chain);
}

/* Exported function to handle include chain merging, duplicate
   removal, and registration with cpplib.  */
void
register_include_chains (cpp_reader *pfile, const char *sysroot,
			 const char *iprefix, const char *imultilib,
			 int stdinc, int cxx_stdinc, int verbose)
{
  static const char *const lang_env_vars[] =
    { "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH",
      "OBJC_INCLUDE_PATH", "OBJCPLUS_INCLUDE_PATH" };
  cpp_options *cpp_opts = cpp_get_options (pfile);
  size_t idx = (cpp_opts->objc ? 2: 0);

  if (cpp_opts->cplusplus)
    idx++;
  else
    cxx_stdinc = false;

  /* CPATH and language-dependent environment variables may add to the
     include chain.  */
  add_env_var_paths ("CPATH", BRACKET);
  add_env_var_paths (lang_env_vars[idx], SYSTEM);

  target_c_incpath.extra_pre_includes (sysroot, iprefix, stdinc);

  /* Finally chain on the standard directories.  */
  if (stdinc)
    add_standard_paths (sysroot, iprefix, imultilib, cxx_stdinc);

  target_c_incpath.extra_includes (sysroot, iprefix, stdinc);

  merge_include_chains (sysroot, pfile, verbose);

  cpp_set_include_chains (pfile, heads[QUOTE], heads[BRACKET],
			  quote_ignores_source_dir);
}
#if !(defined TARGET_EXTRA_INCLUDES) || !(defined TARGET_EXTRA_PRE_INCLUDES)
static void hook_void_charptr_charptr_int (const char *sysroot ATTRIBUTE_UNUSED,
					   const char *iprefix ATTRIBUTE_UNUSED,
					   int stdinc ATTRIBUTE_UNUSED)
{
}
#endif

#ifndef TARGET_EXTRA_INCLUDES
#define TARGET_EXTRA_INCLUDES hook_void_charptr_charptr_int
#endif
#ifndef TARGET_EXTRA_PRE_INCLUDES
#define TARGET_EXTRA_PRE_INCLUDES hook_void_charptr_charptr_int
#endif

struct target_c_incpath_s target_c_incpath = { TARGET_EXTRA_PRE_INCLUDES, TARGET_EXTRA_INCLUDES };

static bool using_frameworks = false;

static const char *find_subframework_file (const char *, const char *);
static void add_system_framework_path (char *);
static const char *find_subframework_header (cpp_reader *pfile, const char *header,
					     cpp_dir **dirp);

static struct {
  size_t len;
  const char *name;
  cpp_dir* dir;
} *frameworks_in_use;
static int num_frameworks = 0;
static int max_frameworks = 0;


/* Remember which frameworks have been seen, so that we can ensure
   that all uses of that framework come from the same framework.  DIR
   is the place where the named framework NAME, which is of length
   LEN, was found.  We copy the directory name from NAME, as it will be
   freed by others.  */

static void
add_framework (const char *name, size_t len, cpp_dir *dir)
{
  char *dir_name;
  int i;
  for (i = 0; i < num_frameworks; ++i)
    {
      if (len == frameworks_in_use[i].len
	  && strncmp (name, frameworks_in_use[i].name, len) == 0)
	{
	  return;
	}
    }
  if (i >= max_frameworks)
    {
      max_frameworks = i*2;
      max_frameworks += i == 0;
      frameworks_in_use = xrealloc (frameworks_in_use,
				    max_frameworks*sizeof(*frameworks_in_use));
    }
  dir_name = xmalloc (len + 1);
  memcpy (dir_name, name, len);
  dir_name[len] = '\0';
  frameworks_in_use[num_frameworks].name = dir_name;
  frameworks_in_use[num_frameworks].len = len;
  frameworks_in_use[num_frameworks].dir = dir;
  ++num_frameworks;
}

/* Recall if we have seen the named framework NAME, before, and where
   we saw it.  NAME is LEN bytes long.  The return value is the place
   where it was seen before.  */

static struct cpp_dir*
find_framework (const char *name, size_t len)
{
  int i;
  for (i = 0; i < num_frameworks; ++i)
    {
      if (len == frameworks_in_use[i].len
	  && strncmp (name, frameworks_in_use[i].name, len) == 0)
	{
	  return frameworks_in_use[i].dir;
	}
    }
  return 0;
}

/* There are two directories in a framework that contain header files,
   Headers and PrivateHeaders.  We search Headers first as it is more
   common to upgrade a header from PrivateHeaders to Headers and when
   that is done, the old one might hang around and be out of data,
   causing grief.  */

struct framework_header {const char * dirName; int dirNameLen; };
static struct framework_header framework_header_dirs[] = {
  { "Headers", 7 },
  { "PrivateHeaders", 14 },
  { NULL, 0 }
};

/* Returns a pointer to a malloced string that contains the real pathname
   to the file, given the base name and the name.  */

static char *
framework_construct_pathname (const char *fname, cpp_dir *dir)
{
  char *buf;
  size_t fname_len, frname_len;
  cpp_dir *fast_dir;
  char *frname;
  struct stat st;
  int i;

  /* Framework names must have a / in them.  */
  buf = strchr (fname, '/');
  if (buf)
    fname_len = buf - fname;
  else
    return 0;

  fast_dir = find_framework (fname, fname_len);

  /* Framework includes must all come from one framework.  */
  if (fast_dir && dir != fast_dir)
    return 0;

  frname = xmalloc (strlen (fname) + dir->len + 2
		    + strlen(".framework/") + strlen("PrivateHeaders"));
  strncpy (&frname[0], dir->name, dir->len);
  frname_len = dir->len;
  if (frname_len && frname[frname_len-1] != '/')
    frname[frname_len++] = '/';
  strncpy (&frname[frname_len], fname, fname_len);
  frname_len += fname_len;
  strncpy (&frname[frname_len], ".framework/", strlen (".framework/"));
  frname_len += strlen (".framework/");

  /* Append framework_header_dirs and header file name */
  for (i = 0; framework_header_dirs[i].dirName; i++)
    {
      strncpy (&frname[frname_len], 
	       framework_header_dirs[i].dirName,
	       framework_header_dirs[i].dirNameLen);
      strcpy (&frname[frname_len + framework_header_dirs[i].dirNameLen],
	      &fname[fname_len]);

      if (stat (frname, &st) == 0)
	{
	  if (fast_dir == 0)
	    add_framework (fname, fname_len, dir);
	  return frname;
	}
    }

  free (frname);
  return 0;
}

/* Search for FNAME in sub-frameworks.  pname is the context that we
   wish to search in.  Return the path the file was found at,
   otherwise return 0.  */

static const char*
find_subframework_file (const char *fname, const char *pname)
{
  char *sfrname;
  const char *dot_framework = ".framework/";
  char *bufptr; 
  int sfrname_len, i, fname_len; 
  struct cpp_dir *fast_dir;
  static struct cpp_dir subframe_dir;
  struct stat st;

  bufptr = strchr (fname, '/');

  /* Subframework files must have / in the name.  */
  if (bufptr == 0)
    return 0;
    
  fname_len = bufptr - fname;
  fast_dir = find_framework (fname, fname_len);

  /* Sub framework header filename includes parent framework name and
     header name in the "CarbonCore/OSUtils.h" form. If it does not
     include slash it is not a sub framework include.  */
  bufptr = strstr (pname, dot_framework);

  /* If the parent header is not of any framework, then this header
     cannot be part of any subframework.  */
  if (!bufptr)
    return 0;

  /* Now translate. For example,                  +- bufptr
     fname = CarbonCore/OSUtils.h                 | 
     pname = /System/Library/Frameworks/Foundation.framework/Headers/Foundation.h
     into
     sfrname = /System/Library/Frameworks/Foundation.framework/Frameworks/CarbonCore.framework/Headers/OSUtils.h */

  sfrname = (char *) xmalloc (strlen (pname) + strlen (fname) + 2 +
			      strlen ("Frameworks/") + strlen (".framework/")
			      + strlen ("PrivateHeaders"));
 
  bufptr += strlen (dot_framework);

  sfrname_len = bufptr - pname; 

  strncpy (&sfrname[0], pname, sfrname_len);

  strncpy (&sfrname[sfrname_len], "Frameworks/", strlen ("Frameworks/"));
  sfrname_len += strlen("Frameworks/");

  strncpy (&sfrname[sfrname_len], fname, fname_len);
  sfrname_len += fname_len;

  strncpy (&sfrname[sfrname_len], ".framework/", strlen (".framework/"));
  sfrname_len += strlen (".framework/");

  /* Append framework_header_dirs and header file name */
  for (i = 0; framework_header_dirs[i].dirName; i++)
    {
      strncpy (&sfrname[sfrname_len], 
	       framework_header_dirs[i].dirName,
	       framework_header_dirs[i].dirNameLen);
      strcpy (&sfrname[sfrname_len + framework_header_dirs[i].dirNameLen],
	      &fname[fname_len]);
    
      if (stat (sfrname, &st) == 0)
	{
	  if (fast_dir != &subframe_dir)
	    {
	      if (fast_dir)
		warning ("subframework include %s conflicts with framework include",
			 fname);
	      else
		add_framework (fname, fname_len, &subframe_dir);
	    }

	  return sfrname;
	}
    }
  free (sfrname);

  return 0;
}

/* Add PATH to the system includes. PATH must be malloc-ed and
   NUL-terminated.  System framework paths are C++ aware.  */

static void
add_system_framework_path (char *path)
{
  int cxx_aware = 1;
  cpp_dir *p;

  p = xmalloc (sizeof (cpp_dir));
  p->next = NULL;
  p->name = path;
  p->sysp = 1 + !cxx_aware;
  p->construct = framework_construct_pathname;
  using_frameworks = 1;

  add_cpp_dir_path (p, SYSTEM);
}

/* Add PATH to the bracket includes. PATH must be malloc-ed and
   NUL-terminated.  */

void
add_framework_path (char *path)
{
  cpp_dir *p;

  p = xmalloc (sizeof (cpp_dir));
  p->next = NULL;
  p->name = path;
  p->sysp = 0;
  p->construct = framework_construct_pathname;
  using_frameworks = 1;

  add_cpp_dir_path (p, BRACKET);
}

/* Search for HEADER in context dependent way.  The return value is
   the malloced name of a header to try and open, if any, or NULL
   otherwise.  This is called after normal header lookup processing
   fails to find a header.  We search each file in the include stack,
   using FUNC, starting from the most deeply nested include and
   finishing with the main input file.  We stop searching when FUNC
   returns nonzero.  */

static const char*
find_subframework_header (cpp_reader *pfile, const char *header, cpp_dir **dirp)
{
  const char *fname = header;
  struct cpp_buffer *b;
  const char *n;

  for (b = cpp_get_buffer (pfile);
       b && cpp_get_file (b) && cpp_get_path (cpp_get_file (b));
       b = cpp_get_prev (b))
    {
      n = find_subframework_file (fname, cpp_get_path (cpp_get_file (b)));
      if (n)
	{
	  /* Logically, the place where we found the subframework is
	     the place where we found the Framework that contains the
	     subframework.  This is useful for tracking wether or not
	     we are in a system header.  */
	  *dirp = cpp_get_dir (cpp_get_file (b));
	  return n;
	}
    }

  return 0;
}

