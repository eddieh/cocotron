/* Alias analysis for trees.
   Copyright (C) 2004, 2005, 2006, 2007 Free Software Foundation, Inc.
   Contributed by Diego Novillo <dnovillo@redhat.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "timevar.h"
#include "expr.h"
#include "ggc.h"
#include "langhooks.h"
#include "flags.h"
#include "function.h"
#include "diagnostic.h"
#include "tree-dump.h"
#include "tree-gimple.h"
#include "tree-flow.h"
#include "tree-inline.h"
#include "tree-pass.h"
#include "tree-ssa-structalias.h"
#include "convert.h"
#include "params.h"
#include "ipa-type-escape.h"
#include "vec.h"
#include "bitmap.h"
#include "vecprim.h"
#include "pointer-set.h"
#include "alloc-pool.h"

/* Broad overview of how aliasing works:
   
   First we compute points-to sets, which is done in
   tree-ssa-structalias.c
      
   During points-to set constraint finding, a bunch of little bits of
   information is collected.
   This is not done because it is necessary for points-to, but because
   points-to has to walk every statement anyway.  The function performing
   this collecting is update_alias_info.

   Bits update_alias_info collects include:
   1. Directly escaping variables and variables whose value escapes
   (using is_escape_site).  This is the set of variables and values that
   escape prior to transitive closure of the clobbers.
   2.  The set of variables dereferenced on the LHS (into
   dereferenced_ptr_stores) 
   3. The set of variables dereferenced on the RHS (into
   dereferenced_ptr_loads) 
   4. The set of all pointers we saw.
   5. The number of loads and stores for each variable
   6. The number of statements touching memory
   7. The set of address taken variables.
   
   
   #1 is computed by a combination of is_escape_site, and counting the
   number of uses/deref operators.  This function properly accounts for
   situations like &ptr->field, which is *not* a dereference.
   
   After points-to sets are computed, the sets themselves still
   contain points-to specific variables, such as a variable that says
   the pointer points to anything, a variable that says the pointer
   points to readonly memory, etc.

   These are eliminated in a later phase, as we will see.

   The rest of the phases are located in tree-ssa-alias.c

   The next phase after points-to set computation is called
   "setup_pointers_and_addressables"

   This pass does 3 main things:
   
   1. All variables that can have TREE_ADDRESSABLE removed safely (IE
   non-globals whose address is not taken), have TREE_ADDRESSABLE
   removed.
   2. All variables that may be aliased (which is the set of addressable
   variables and globals) at all, are marked for renaming, and have
   symbol memory tags created for them.
   3. All variables which are stored into have their SMT's added to
   written vars. 


   After this function is run, all variables that will ever have an
   SMT, have one, though its aliases are not filled in.

   The next phase is to compute flow-insensitive aliasing, which in
   our case, is a misnomer.  it is really computing aliasing that
   requires no transitive closure to be correct.  In particular, it
   uses stack vs non-stack, TBAA, etc, to determine whether two
   symbols could *ever* alias .  This phase works by going through all
   the pointers we collected during update_alias_info, and for every
   addressable variable in the program, seeing if they alias.  If so,
   the addressable variable is added to the symbol memory tag for the
   pointer.

   As part of this, we handle symbol memory tags that conflict but
   have no aliases in common, by forcing them to have a symbol in
   common (through unioning alias sets or adding one as an alias of
   the other), or by adding one as an alias of another.  The case of
   conflicts with no aliases in common occurs mainly due to aliasing
   we cannot see.  In particular, it generally means we have a load
   through a pointer whose value came from outside the function.
   Without an addressable symbol to point to, they would get the wrong
   answer.

   After flow insensitive aliasing is computed, we compute name tags
   (called compute_flow_sensitive_info).  We walk each pointer we
   collected and see if it has a usable points-to set.  If so, we
   generate a name tag using that pointer, and make an alias bitmap for
   it.  Name tags are shared between all things with the same alias
   bitmap.  The alias bitmap will be translated from what points-to
   computed.  In particular, the "anything" variable in points-to will be
   transformed into a pruned set of SMT's and their aliases that
   compute_flow_insensitive_aliasing computed.
   Note that since 4.3, every pointer that points-to computed a solution for
   will get a name tag (whereas before 4.3, only those whose set did
   *not* include the anything variable would).  At the point where name
   tags are all assigned, symbol memory tags are dead, and could be
   deleted, *except* on global variables.  Global variables still use
   symbol memory tags as of right now.

   After name tags are computed, the set of clobbered variables is
   transitively closed.  In particular, we compute the set of clobbered
   variables based on the initial set of clobbers, plus the aliases of
   pointers which either escape, or have their value escape.

   After this, maybe_create_global_var is run, which handles a corner
   case where we have no call clobbered variables, but have pure and
   non-pure functions.
   
   Staring at this function, I now remember it is a hack for the fact
   that we do not mark all globals in the program as call clobbered for a
   function unless they are actually used in that function.  Instead,  we
   only mark the set that is actually clobbered.  As a result, you can
   end up with situations where you have no call clobbered vars set.
   
   After maybe_create_global_var, we set pointers with the REF_ALL flag
   to have alias sets that include all clobbered
   memory tags and variables.
   
   After this, memory partitioning is computed (by the function
   compute_memory_partitions) and alias sets are reworked accordingly.

   Lastly, we delete partitions with no symbols, and clean up after
   ourselves.  */

/* Structure to map a variable to its alias set.  */
struct alias_map_d
{
  /* Variable and its alias set.  */
  tree var;
  alias_set_type set;
};


/* Counters used to display statistics on alias analysis.  */
struct alias_stats_d
{
  unsigned int alias_queries;
  unsigned int alias_mayalias;
  unsigned int alias_noalias;
  unsigned int simple_queries;
  unsigned int simple_resolved;
  unsigned int tbaa_queries;
  unsigned int tbaa_resolved;
  unsigned int structnoaddress_queries;
  unsigned int structnoaddress_resolved;
};


/* Local variables.  */
static struct alias_stats_d alias_stats;
static bitmap_obstack alias_bitmap_obstack;

/* Local functions.  */
static void compute_flow_insensitive_aliasing (struct alias_info *);
static void finalize_ref_all_pointers (struct alias_info *);
static void dump_alias_stats (FILE *);
static bool may_alias_p (tree, alias_set_type, tree, alias_set_type, bool);
static tree create_memory_tag (tree type, bool is_type_tag);
static tree get_smt_for (tree, struct alias_info *);
static tree get_nmt_for (tree);
static void add_may_alias (tree, tree);
static struct alias_info *init_alias_info (void);
static void delete_alias_info (struct alias_info *);
static void compute_flow_sensitive_aliasing (struct alias_info *);
static void setup_pointers_and_addressables (struct alias_info *);
static void create_global_var (void);
static void maybe_create_global_var (void);
static void set_pt_anything (tree);

void debug_mp_info (VEC(mem_sym_stats_t,heap) *);

static alloc_pool mem_sym_stats_pool;

/* Return memory reference stats for symbol VAR.  Create a new slot in
   cfun->gimple_df->mem_sym_stats if needed.  */

static struct mem_sym_stats_d *
get_mem_sym_stats_for (tree var)
{
  void **slot;
  struct mem_sym_stats_d *stats;
  struct pointer_map_t *map = gimple_mem_ref_stats (cfun)->mem_sym_stats;
  
  gcc_assert (map);

  slot = pointer_map_insert (map, var);
  if (*slot == NULL)
    {
      stats = pool_alloc (mem_sym_stats_pool);
      memset (stats, 0, sizeof (*stats));
      stats->var = var;
      *slot = (void *) stats;
    }
  else
    stats = (struct mem_sym_stats_d *) *slot;

  return stats;
}


/* Return memory reference statistics for variable VAR in function FN.
   This is computed by alias analysis, but it is not kept
   incrementally up-to-date.  So, these stats are only accurate if
   pass_may_alias has been run recently.  If no alias information
   exists, this function returns NULL.  */

static mem_sym_stats_t
mem_sym_stats (struct function *fn, tree var)
{
  void **slot;
  struct pointer_map_t *stats_map = gimple_mem_ref_stats (fn)->mem_sym_stats;

  if (stats_map == NULL)
    return NULL;

  slot = pointer_map_contains (stats_map, var);
  if (slot == NULL)
    return NULL;

  return (mem_sym_stats_t) *slot;
}


/* Set MPT to be the memory partition associated with symbol SYM.  */

static inline void
set_memory_partition (tree sym, tree mpt)
{
#if defined ENABLE_CHECKING
  if (mpt)
    gcc_assert (TREE_CODE (mpt) == MEMORY_PARTITION_TAG
	        && !is_gimple_reg (sym));
#endif

  var_ann (sym)->mpt = mpt;
  if (mpt)
    {
      if (MPT_SYMBOLS (mpt) == NULL)
	MPT_SYMBOLS (mpt) = BITMAP_ALLOC (&alias_bitmap_obstack);

      bitmap_set_bit (MPT_SYMBOLS (mpt), DECL_UID (sym));

      /* MPT inherits the call-clobbering attributes from SYM.  */
      if (is_call_clobbered (sym))
	{
	  MTAG_GLOBAL (mpt) = 1;
	  mark_call_clobbered (mpt, ESCAPE_IS_GLOBAL);
	}
    }
}


/* Mark variable VAR as being non-addressable.  */

static void
mark_non_addressable (tree var)
{
  tree mpt;

  if (!TREE_ADDRESSABLE (var))
    return;

  mpt = memory_partition (var);

  if (!MTAG_P (var))
    var_ann (var)->call_clobbered = false;

  bitmap_clear_bit (gimple_call_clobbered_vars (cfun), DECL_UID (var));
  TREE_ADDRESSABLE (var) = 0;

  if (mpt)
    {
      /* Note that it's possible for a symbol to have an associated
	 MPT and the MPT have a NULL empty set.  During
	 init_alias_info, all MPTs get their sets cleared out, but the
	 symbols still point to the old MPTs that used to hold them.
	 This is done so that compute_memory_partitions can now which
	 symbols are losing or changing partitions and mark them for
	 renaming.  */
      if (MPT_SYMBOLS (mpt))
	bitmap_clear_bit (MPT_SYMBOLS (mpt), DECL_UID (var));
      set_memory_partition (var, NULL_TREE);
    }
}


/* qsort comparison function to sort type/name tags by DECL_UID.  */

static int
sort_tags_by_id (const void *pa, const void *pb)
{
  const_tree const a = *(const_tree const *)pa;
  const_tree const b = *(const_tree const *)pb;
 
  return DECL_UID (a) - DECL_UID (b);
}

/* Initialize WORKLIST to contain those memory tags that are marked call
   clobbered.  Initialized WORKLIST2 to contain the reasons these
   memory tags escaped.  */

static void
init_transitive_clobber_worklist (VEC (tree, heap) **worklist,
				  VEC (int, heap) **worklist2,
				  bitmap on_worklist)
{
  referenced_var_iterator rvi;
  tree curr;

  FOR_EACH_REFERENCED_VAR (curr, rvi)
    {
      if (MTAG_P (curr) && is_call_clobbered (curr))
	{
	  VEC_safe_push (tree, heap, *worklist, curr);
	  VEC_safe_push (int, heap, *worklist2,
			 var_ann (curr)->escape_mask);
	  bitmap_set_bit (on_worklist, DECL_UID (curr));
	}
    }
}

/* Add ALIAS to WORKLIST (and the reason for escaping REASON to WORKLIST2) if
   ALIAS is not already marked call clobbered, and is a memory
   tag.  */

static void
add_to_worklist (tree alias, VEC (tree, heap) **worklist,
		 VEC (int, heap) **worklist2, int reason,
		 bitmap on_worklist)
{
  if (MTAG_P (alias) && !is_call_clobbered (alias)
      && !bitmap_bit_p (on_worklist, DECL_UID (alias)))
    {
      VEC_safe_push (tree, heap, *worklist, alias);
      VEC_safe_push (int, heap, *worklist2, reason);
      bitmap_set_bit (on_worklist, DECL_UID (alias));
    }
}

/* Mark aliases of TAG as call clobbered, and place any tags on the
   alias list that were not already call clobbered on WORKLIST.  */

static void
mark_aliases_call_clobbered (tree tag, VEC (tree, heap) **worklist,
			     VEC (int, heap) **worklist2,
			     bitmap on_worklist, bitmap queued)
{
  bitmap aliases;
  bitmap_iterator bi;
  unsigned int i;
  tree entry;
  var_ann_t ta = var_ann (tag);

  if (!MTAG_P (tag))
    return;
  aliases = may_aliases (tag);
  if (!aliases)
    return;

  EXECUTE_IF_SET_IN_BITMAP (aliases, 0, i, bi)
    {
      entry = referenced_var (i);
      /* If you clobber one part of a structure, you
	 clobber the entire thing.  While this does not make
	 the world a particularly nice place, it is necessary
	 in order to allow C/C++ tricks that involve
	 pointer arithmetic to work.  */
      if (TREE_CODE (entry) == STRUCT_FIELD_TAG)
	bitmap_set_bit (queued, DECL_UID (SFT_PARENT_VAR (entry)));
      else if (!unmodifiable_var_p (entry))
	{
	  add_to_worklist (entry, worklist, worklist2, ta->escape_mask,
			   on_worklist);
	  mark_call_clobbered (entry, ta->escape_mask);
	}
    }
  if (!bitmap_empty_p (queued))
    {
      EXECUTE_IF_SET_IN_BITMAP (queued, 0, i, bi)
	{
	  subvar_t svars = get_subvars_for_var (referenced_var (i));
	  unsigned int i;
	  tree subvar;

	  for (i = 0; VEC_iterate (tree, svars, i, subvar); ++i)
	    if (!unmodifiable_var_p (subvar))
	       mark_call_clobbered (subvar, ta->escape_mask);
	}
      bitmap_clear (queued);
    }
}

/* Tags containing global vars need to be marked as global.
   Tags containing call clobbered vars need to be marked as call
   clobbered. */

static void
compute_tag_properties (void)
{
  referenced_var_iterator rvi;
  tree tag;
  bool changed = true;
  VEC (tree, heap) *taglist = NULL;

  FOR_EACH_REFERENCED_VAR (tag, rvi)
    {
      if (!MTAG_P (tag) || TREE_CODE (tag) == STRUCT_FIELD_TAG)
	continue;
      VEC_safe_push (tree, heap, taglist, tag);
    }

  /* We sort the taglist by DECL_UID, for two reasons.
     1. To get a sequential ordering to make the bitmap accesses
     faster.
     2. Because of the way we compute aliases, it's more likely that
     an earlier tag is included in a later tag, and this will reduce
     the number of iterations.

     If we had a real tag graph, we would just topo-order it and be
     done with it.  */
  qsort (VEC_address (tree, taglist),
	 VEC_length (tree, taglist),
	 sizeof (tree),
	 sort_tags_by_id);

  /* Go through each tag not marked as global, and if it aliases
     global vars, mark it global. 
     
     If the tag contains call clobbered vars, mark it call
     clobbered.  

     This loop iterates because tags may appear in the may-aliases
     list of other tags when we group.  */

  while (changed)
    {
      unsigned int k;

      changed = false;      
      for (k = 0; VEC_iterate (tree, taglist, k, tag); k++)
	{
	  bitmap ma;
	  bitmap_iterator bi;
	  unsigned int i;
	  tree entry;
	  bool tagcc = is_call_clobbered (tag);
	  bool tagglobal = MTAG_GLOBAL (tag);
	  
	  if (tagcc && tagglobal)
	    continue;
	  
	  ma = may_aliases (tag);
	  if (!ma)
	    continue;

	  EXECUTE_IF_SET_IN_BITMAP (ma, 0, i, bi)
	    {
	      entry = referenced_var (i);
	      /* Call clobbered entries cause the tag to be marked
		 call clobbered.  */
	      if (!tagcc && is_call_clobbered (entry))
		{
		  mark_call_clobbered (tag, var_ann (entry)->escape_mask);
		  tagcc = true;
		  changed = true;
		}

	      /* Global vars cause the tag to be marked global.  */
	      if (!tagglobal && is_global_var (entry))
		{
		  MTAG_GLOBAL (tag) = true;
		  changed = true;
		  tagglobal = true;
		}

	      /* Early exit once both global and cc are set, since the
		 loop can't do any more than that.  */
	      if (tagcc && tagglobal)
		break;
	    }
	}
    }
  VEC_free (tree, heap, taglist);
}

/* Set up the initial variable clobbers and globalness.
   When this function completes, only tags whose aliases need to be
   clobbered will be set clobbered.  Tags clobbered because they   
   contain call clobbered vars are handled in compute_tag_properties.  */

static void
set_initial_properties (struct alias_info *ai)
{
  unsigned int i;
  referenced_var_iterator rvi;
  tree var;
  tree ptr;
  bitmap queued;
  bool any_pt_anything = false;
  enum escape_type pt_anything_mask = 0;
  bitmap_iterator bi;
  unsigned int j;

  /* Temporary bitmap to avoid quadratic behavior in marking
     call clobbers.  */
  queued = BITMAP_ALLOC (&alias_bitmap_obstack);

  FOR_EACH_REFERENCED_VAR (var, rvi)
    {
      if (is_global_var (var) 
	  && (!var_can_have_subvars (var)
	      || get_subvars_for_var (var) == NULL))
	{
	  if (!unmodifiable_var_p (var))
	    mark_call_clobbered (var, ESCAPE_IS_GLOBAL);
	}
      else if (TREE_CODE (var) == PARM_DECL
	       && gimple_default_def (cfun, var)
	       && POINTER_TYPE_P (TREE_TYPE (var)))
	{
	  tree def = gimple_default_def (cfun, var);
	  get_ptr_info (def)->value_escapes_p = 1;
	  get_ptr_info (def)->escape_mask |= ESCAPE_IS_PARM;	  
	}
    }

  for (i = 0; VEC_iterate (tree, ai->processed_ptrs, i, ptr); i++)
    {
      struct ptr_info_def *pi = SSA_NAME_PTR_INFO (ptr);
      tree tag = symbol_mem_tag (SSA_NAME_VAR (ptr));

      if (pi->value_escapes_p)
	{
	  /* If PTR escapes then its associated memory tags and
	     pointed-to variables are call-clobbered.  */
	  if (pi->name_mem_tag)
	    mark_call_clobbered (pi->name_mem_tag, pi->escape_mask);

	  if (tag)
	    mark_call_clobbered (tag, pi->escape_mask);

	  if (pi->pt_vars)
	    {
	      EXECUTE_IF_SET_IN_BITMAP (pi->pt_vars, 0, j, bi)
		{
		  tree alias = referenced_var (j);

		  /* If you clobber one part of a structure, you
		     clobber the entire thing.  While this does not make
		     the world a particularly nice place, it is necessary
		     in order to allow C/C++ tricks that involve
		     pointer arithmetic to work.  */
		  if (TREE_CODE (alias) == STRUCT_FIELD_TAG)
		    {
		      alias = SFT_PARENT_VAR (alias);
		      if (!unmodifiable_var_p (alias))
			{
			  bitmap_set_bit (queued, DECL_UID (alias));
			  var_ann (alias)->escape_mask |= pi->escape_mask;
			}
		    }
		  else if (!unmodifiable_var_p (alias))
		    mark_call_clobbered (alias, pi->escape_mask);
		}
	    }
	  else if (pi->pt_anything)
	    {
	      any_pt_anything = true;
	      pt_anything_mask |= pi->escape_mask;
	    }
	}

      /* If the name tag is call clobbered, so is the symbol tag
	 associated with the base VAR_DECL.  */
      if (pi->name_mem_tag
	  && tag
	  && is_call_clobbered (pi->name_mem_tag))
	mark_call_clobbered (tag, pi->escape_mask);

      /* Name tags and symbol tags that we don't know where they point
	 to, might point to global memory, and thus, are clobbered.

         FIXME:  This is not quite right.  They should only be
         clobbered if value_escapes_p is true, regardless of whether
         they point to global memory or not.
         So removing this code and fixing all the bugs would be nice.
         It is the cause of a bunch of clobbering.  */
      if ((pi->pt_global_mem || pi->pt_anything) 
	  && pi->is_dereferenced && pi->name_mem_tag)
	{
	  mark_call_clobbered (pi->name_mem_tag, ESCAPE_IS_GLOBAL);
	  MTAG_GLOBAL (pi->name_mem_tag) = true;
	}
      
      if ((pi->pt_global_mem || pi->pt_anything) 
	  && pi->is_dereferenced
	  && tag)
	{
	  mark_call_clobbered (tag, ESCAPE_IS_GLOBAL);
	  MTAG_GLOBAL (tag) = true;
	}
    }

  /* If a pointer to anything escaped we need to mark all addressable
     variables call clobbered.  */
  if (any_pt_anything)
    {
      EXECUTE_IF_SET_IN_BITMAP (gimple_addressable_vars (cfun),
				0, j, bi)
	{
	  tree var = referenced_var (j);
	  if (TREE_CODE (var) == STRUCT_FIELD_TAG)
	    {
	      var = SFT_PARENT_VAR (var);
	      if (!unmodifiable_var_p (var))
		{
		  bitmap_set_bit (queued, DECL_UID (var));
		  var_ann (var)->escape_mask |= pt_anything_mask;
		}
	    }
	  else if (!unmodifiable_var_p (var))
	    mark_call_clobbered (var, pt_anything_mask);
	}
    }

  /* Process variables we need to clobber all parts of.  */
  EXECUTE_IF_SET_IN_BITMAP (queued, 0, j, bi)
    {
      tree var = referenced_var (j);
      subvar_t svars = get_subvars_for_var (var);
      unsigned int i;
      tree subvar;
      enum escape_type mask = var_ann (var)->escape_mask;

      for (i = 0; VEC_iterate (tree, svars, i, subvar); ++i)
	mark_call_clobbered (subvar, mask);
    }

  BITMAP_FREE (queued);
}

/* Compute which variables need to be marked call clobbered because
   their tag is call clobbered, and which tags need to be marked
   global because they contain global variables.  */

static void
compute_call_clobbered (struct alias_info *ai)
{
  VEC (tree, heap) *worklist = NULL;
  VEC (int,heap) *worklist2 = NULL;
  bitmap on_worklist, queued;

  timevar_push (TV_CALL_CLOBBER);
  on_worklist = BITMAP_ALLOC (NULL);
  queued = BITMAP_ALLOC (NULL);
    
  set_initial_properties (ai);
  init_transitive_clobber_worklist (&worklist, &worklist2, on_worklist);
  while (VEC_length (tree, worklist) != 0)
    {
      tree curr = VEC_pop (tree, worklist);
      int reason = VEC_pop (int, worklist2);

      bitmap_clear_bit (on_worklist, DECL_UID (curr));
      mark_call_clobbered (curr, reason);
      mark_aliases_call_clobbered (curr, &worklist, &worklist2,
				   on_worklist, queued);
    }
  VEC_free (tree, heap, worklist);
  VEC_free (int, heap, worklist2);
  BITMAP_FREE (on_worklist);
  BITMAP_FREE (queued);
  compute_tag_properties ();
  timevar_pop (TV_CALL_CLOBBER);
}


/* Dump memory partition information to FILE.  */

static void
dump_memory_partitions (FILE *file)
{
  unsigned i, npart;
  unsigned long nsyms;
  tree mpt;

  fprintf (file, "\nMemory partitions\n\n");
  for (i = 0, npart = 0, nsyms = 0;
       VEC_iterate (tree, gimple_ssa_operands (cfun)->mpt_table, i, mpt);
       i++)
    {
      if (mpt)
	{
	  bitmap syms = MPT_SYMBOLS (mpt);
	  unsigned long n = (syms) ? bitmap_count_bits (syms) : 0;

	  fprintf (file, "#%u: ", i);
	  print_generic_expr (file, mpt, 0);
	  fprintf (file, ": %lu elements: ", n);
	  dump_decl_set (file, syms);
	  npart++;
	  nsyms += n;
	}
    }

  fprintf (file, "\n%u memory partitions holding %lu symbols\n", npart, nsyms);
}


/* Dump memory partition information to stderr.  */

void
debug_memory_partitions (void)
{
  dump_memory_partitions (stderr);
}


/* Return true if memory partitioning is required given the memory
   reference estimates in STATS.  */

static inline bool
need_to_partition_p (struct mem_ref_stats_d *stats)
{
  long num_vops = stats->num_vuses + stats->num_vdefs;
  long avg_vops = CEIL (num_vops, stats->num_mem_stmts);
  return (num_vops > (long) MAX_ALIASED_VOPS
          && avg_vops > (long) AVG_ALIASED_VOPS);
}


/* Count the actual number of virtual operators in CFUN.  Note that
   this is only meaningful after virtual operands have been populated,
   so it should be invoked at the end of compute_may_aliases.

   The number of virtual operators are stored in *NUM_VDEFS_P and
   *NUM_VUSES_P, the number of partitioned symbols in
   *NUM_PARTITIONED_P and the number of unpartitioned symbols in
   *NUM_UNPARTITIONED_P.

   If any of these pointers is NULL the corresponding count is not
   computed.  */

static void
count_mem_refs (long *num_vuses_p, long *num_vdefs_p,
		long *num_partitioned_p, long *num_unpartitioned_p)
{
  block_stmt_iterator bsi;
  basic_block bb;
  long num_vdefs, num_vuses, num_partitioned, num_unpartitioned;
  referenced_var_iterator rvi;
  tree sym;

  num_vuses = num_vdefs = num_partitioned = num_unpartitioned = 0;

  if (num_vuses_p || num_vdefs_p)
    FOR_EACH_BB (bb)
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  tree stmt = bsi_stmt (bsi);
	  if (stmt_references_memory_p (stmt))
	    {
	      num_vuses += NUM_SSA_OPERANDS (stmt, SSA_OP_VUSE);
	      num_vdefs += NUM_SSA_OPERANDS (stmt, SSA_OP_VDEF);
	    }
	}

  if (num_partitioned_p || num_unpartitioned_p)
    FOR_EACH_REFERENCED_VAR (sym, rvi)
      {
	if (is_gimple_reg (sym))
	  continue;

	if (memory_partition (sym))
	  num_partitioned++;
	else
	  num_unpartitioned++;
      }

  if (num_vdefs_p)
    *num_vdefs_p = num_vdefs;

  if (num_vuses_p)
    *num_vuses_p = num_vuses;

  if (num_partitioned_p)
    *num_partitioned_p = num_partitioned;

  if (num_unpartitioned_p)
    *num_unpartitioned_p = num_unpartitioned;
}


/* The list is sorted by increasing partitioning score (PSCORE).
   This score is computed such that symbols with high scores are
   those that are least likely to be partitioned.  Given a symbol
   MP->VAR, PSCORE(S) is the result of the following weighted sum

   PSCORE(S) =   FW * 64 + FR * 32
   	       + DW * 16 + DR *  8 
   	       + IW *  4 + IR *  2
               + NO_ALIAS

   where

   FW		Execution frequency of writes to S
   FR		Execution frequency of reads from S
   DW		Number of direct writes to S
   DR		Number of direct reads from S
   IW		Number of indirect writes to S
   IR		Number of indirect reads from S
   NO_ALIAS	State of the NO_ALIAS* flags

   The basic idea here is that symbols that are frequently
   written-to in hot paths of the code are the last to be considered
   for partitioning.  */

static inline long
mem_sym_score (mem_sym_stats_t mp)
{
  /* Unpartitionable SFTs are automatically thrown to the bottom of
     the list.  They are not stored in partitions, but they are used
     for computing overall statistics.  */
  if (TREE_CODE (mp->var) == STRUCT_FIELD_TAG
      && SFT_UNPARTITIONABLE_P (mp->var))
    return LONG_MAX;

  return mp->frequency_writes * 64 + mp->frequency_reads * 32
         + mp->num_direct_writes * 16 + mp->num_direct_reads * 8
	 + mp->num_indirect_writes * 4 + mp->num_indirect_reads * 2
	 + var_ann (mp->var)->noalias_state;
}


/* Dump memory reference stats for function CFUN to FILE.  */

void
dump_mem_ref_stats (FILE *file)
{
  long actual_num_vuses, actual_num_vdefs;
  long num_partitioned, num_unpartitioned;
  struct mem_ref_stats_d *stats;
  
  stats = gimple_mem_ref_stats (cfun);

  count_mem_refs (&actual_num_vuses, &actual_num_vdefs, &num_partitioned,
                  &num_unpartitioned);

  fprintf (file, "\nMemory reference statistics for %s\n\n", 
	   lang_hooks.decl_printable_name (current_function_decl, 2));

  fprintf (file, "Number of memory statements:     %ld\n",
           stats->num_mem_stmts);
  fprintf (file, "Number of call sites:            %ld\n",
	   stats->num_call_sites);
  fprintf (file, "Number of pure/const call sites: %ld\n",
	   stats->num_pure_const_call_sites);
  fprintf (file, "Number of asm sites:             %ld\n",
	   stats->num_asm_sites);
  fprintf (file, "Estimated number of loads:       %ld (%ld/stmt)\n",
	   stats->num_vuses,
	   (stats->num_mem_stmts)
	   ? CEIL (stats->num_vuses, stats->num_mem_stmts)
	   : 0);
  fprintf (file, "Actual number of loads:          %ld (%ld/stmt)\n",
	   actual_num_vuses, 
	   (stats->num_mem_stmts)
	   ? CEIL (actual_num_vuses, stats->num_mem_stmts)
	   : 0);

  if (actual_num_vuses > stats->num_vuses + (stats->num_vuses / 25))
    fprintf (file, "\t(warning: estimation is lower by more than 25%%)\n");

  fprintf (file, "Estimated number of stores:      %ld (%ld/stmt)\n",
	   stats->num_vdefs,
	   (stats->num_mem_stmts)
	   ? CEIL (stats->num_vdefs, stats->num_mem_stmts)
	   : 0);
  fprintf (file, "Actual number of stores:         %ld (%ld/stmt)\n",
	   actual_num_vdefs, 
	   (stats->num_mem_stmts)
	   ? CEIL (actual_num_vdefs, stats->num_mem_stmts)
	   : 0);

  if (actual_num_vdefs > stats->num_vdefs + (stats->num_vdefs / 25))
    fprintf (file, "\t(warning: estimation is lower by more than 25%%)\n");

  fprintf (file, "Partitioning thresholds:         MAX = %d   AVG = %d "
           "(%sNEED TO PARTITION)\n", MAX_ALIASED_VOPS, AVG_ALIASED_VOPS,
	   stats->num_mem_stmts && need_to_partition_p (stats) ? "" : "NO ");
  fprintf (file, "Number of partitioned symbols:   %ld\n", num_partitioned);
  fprintf (file, "Number of unpartitioned symbols: %ld\n", num_unpartitioned);
}


/* Dump memory reference stats for function FN to stderr.  */

void
debug_mem_ref_stats (void)
{
  dump_mem_ref_stats (stderr);
}


/* Dump memory reference stats for variable VAR to FILE.  */

static void
dump_mem_sym_stats (FILE *file, tree var)
{
  mem_sym_stats_t stats = mem_sym_stats (cfun, var);

  if (stats == NULL)
    return;

  fprintf (file, "read frequency: %6ld, write frequency: %6ld, "
           "direct reads: %3ld, direct writes: %3ld, "
	   "indirect reads: %4ld, indirect writes: %4ld, symbol: ",
	   stats->frequency_reads, stats->frequency_writes,
	   stats->num_direct_reads, stats->num_direct_writes,
	   stats->num_indirect_reads, stats->num_indirect_writes);
  print_generic_expr (file, stats->var, 0);
  fprintf (file, ", tags: ");
  dump_decl_set (file, stats->parent_tags);
}


/* Dump memory reference stats for variable VAR to stderr.  */

void
debug_mem_sym_stats (tree var)
{
  dump_mem_sym_stats (stderr, var);
}

/* Dump memory reference stats for variable VAR to FILE.  For use
   of tree-dfa.c:dump_variable.  */

void
dump_mem_sym_stats_for_var (FILE *file, tree var)
{
  mem_sym_stats_t stats = mem_sym_stats (cfun, var);

  if (stats == NULL)
    return;

  fprintf (file, ", score: %ld", mem_sym_score (stats));
  fprintf (file, ", direct reads: %ld", stats->num_direct_reads);
  fprintf (file, ", direct writes: %ld", stats->num_direct_writes);
  fprintf (file, ", indirect reads: %ld", stats->num_indirect_reads);
  fprintf (file, ", indirect writes: %ld", stats->num_indirect_writes);
}

/* Dump memory reference stats for all memory symbols to FILE.  */

static void
dump_all_mem_sym_stats (FILE *file)
{
  referenced_var_iterator rvi;
  tree sym;

  FOR_EACH_REFERENCED_VAR (sym, rvi)
    {
      if (is_gimple_reg (sym))
	continue;

      dump_mem_sym_stats (file, sym);
    }
}


/* Dump memory reference stats for all memory symbols to stderr.  */

void
debug_all_mem_sym_stats (void)
{
  dump_all_mem_sym_stats (stderr);
}


/* Dump the MP_INFO array to FILE.  */

static void
dump_mp_info (FILE *file, VEC(mem_sym_stats_t,heap) *mp_info)
{
  unsigned i;
  mem_sym_stats_t mp_p;

  for (i = 0; VEC_iterate (mem_sym_stats_t, mp_info, i, mp_p); i++)
    if (!mp_p->partitioned_p)
      dump_mem_sym_stats (file, mp_p->var);
}


/* Dump the MP_INFO array to stderr.  */

void
debug_mp_info (VEC(mem_sym_stats_t,heap) *mp_info)
{
  dump_mp_info (stderr, mp_info);
}


/* Update memory reference stats for symbol VAR in statement STMT.
   NUM_DIRECT_READS and NUM_DIRECT_WRITES specify the number of times
   that VAR is read/written in STMT (indirect reads/writes are not
   recorded by this function, see compute_memory_partitions).  */

void
update_mem_sym_stats_from_stmt (tree var, tree stmt, long num_direct_reads,
                                long num_direct_writes)
{
  mem_sym_stats_t stats;

  gcc_assert (num_direct_reads >= 0 && num_direct_writes >= 0);

  stats = get_mem_sym_stats_for (var);

  stats->num_direct_reads += num_direct_reads;
  stats->frequency_reads += ((long) bb_for_stmt (stmt)->frequency
                             * num_direct_reads);

  stats->num_direct_writes += num_direct_writes;
  stats->frequency_writes += ((long) bb_for_stmt (stmt)->frequency
                              * num_direct_writes);
}


/* Given two MP_INFO entries MP1 and MP2, return -1 if MP1->VAR should
   be partitioned before MP2->VAR, 0 if they are the same or 1 if
   MP1->VAR should be partitioned after MP2->VAR.  */

static inline int
compare_mp_info_entries (mem_sym_stats_t mp1, mem_sym_stats_t mp2)
{
  long pscore1 = mem_sym_score (mp1);
  long pscore2 = mem_sym_score (mp2);

  if (pscore1 < pscore2)
    return -1;
  else if (pscore1 > pscore2)
    return 1;
  else
    return DECL_UID (mp1->var) - DECL_UID (mp2->var);
}


/* Comparison routine for qsort.  The list is sorted by increasing
   partitioning score (PSCORE).  This score is computed such that
   symbols with high scores are those that are least likely to be
   partitioned.  */

static int
mp_info_cmp (const void *p, const void *q)
{
  mem_sym_stats_t e1 = *((const mem_sym_stats_t *) p);
  mem_sym_stats_t e2 = *((const mem_sym_stats_t *) q);
  return compare_mp_info_entries (e1, e2);
}


/* Sort the array of reference counts used to compute memory partitions.
   Elements are sorted in ascending order of execution frequency and 
   descending order of virtual operators needed.  */

static inline void
sort_mp_info (VEC(mem_sym_stats_t,heap) *list)
{
  unsigned num = VEC_length (mem_sym_stats_t, list);

  if (num < 2)
    return;

  if (num == 2)
    {
      if (compare_mp_info_entries (VEC_index (mem_sym_stats_t, list, 0),
	                           VEC_index (mem_sym_stats_t, list, 1)) > 0)
	{  
	  /* Swap elements if they are in the wrong order.  */
	  mem_sym_stats_t tmp = VEC_index (mem_sym_stats_t, list, 0);
	  VEC_replace (mem_sym_stats_t, list, 0,
	               VEC_index (mem_sym_stats_t, list, 1));
	  VEC_replace (mem_sym_stats_t, list, 1, tmp);
	}

      return;
    }

  /* There are 3 or more elements, call qsort.  */
  qsort (VEC_address (mem_sym_stats_t, list),
         VEC_length (mem_sym_stats_t, list), 
	 sizeof (mem_sym_stats_t),
	 mp_info_cmp);
}


/* Return the memory partition tag (MPT) associated with memory
   symbol SYM.  */

static tree
get_mpt_for (tree sym)
{
  tree mpt;

  /* Don't create a new tag unnecessarily.  */
  mpt = memory_partition (sym);
  if (mpt == NULL_TREE)
    {
      mpt = create_tag_raw (MEMORY_PARTITION_TAG, TREE_TYPE (sym), "MPT");
      TREE_ADDRESSABLE (mpt) = 0;
      add_referenced_var (mpt);
      VEC_safe_push (tree, heap, gimple_ssa_operands (cfun)->mpt_table, mpt);
      gcc_assert (MPT_SYMBOLS (mpt) == NULL);
      set_memory_partition (sym, mpt);
    }

  return mpt;
}


/* Add MP_P->VAR to a memory partition and return the partition.  */

static tree
find_partition_for (mem_sym_stats_t mp_p)
{
  unsigned i;
  VEC(tree,heap) *mpt_table;
  tree mpt;

  mpt_table = gimple_ssa_operands (cfun)->mpt_table;
  mpt = NULL_TREE;

  /* Find an existing partition for MP_P->VAR.  */
  for (i = 0; VEC_iterate (tree, mpt_table, i, mpt); i++)
    {
      mem_sym_stats_t mpt_stats;

      /* If MPT does not have any symbols yet, use it.  */
      if (MPT_SYMBOLS (mpt) == NULL)
	break;

      /* Otherwise, see if MPT has common parent tags with MP_P->VAR,
	 but avoid grouping clobbered variables with non-clobbered
	 variables (otherwise, this tends to creates a single memory
	 partition because other call-clobbered variables may have
	 common parent tags with non-clobbered ones).  */
      mpt_stats = get_mem_sym_stats_for (mpt);
      if (mp_p->parent_tags
	  && mpt_stats->parent_tags
	  && is_call_clobbered (mpt) == is_call_clobbered (mp_p->var)
	  && bitmap_intersect_p (mpt_stats->parent_tags, mp_p->parent_tags))
	break;

      /* If no common parent tags are found, see if both MPT and
	 MP_P->VAR are call-clobbered.  */
      if (is_call_clobbered (mpt) && is_call_clobbered (mp_p->var))
	break;
    }

  if (mpt == NULL_TREE)
    mpt = get_mpt_for (mp_p->var);
  else
    set_memory_partition (mp_p->var, mpt);

  mp_p->partitioned_p = true;

  mark_sym_for_renaming (mp_p->var);
  mark_sym_for_renaming (mpt);

  return mpt;
}


/* Rewrite the alias set for TAG to use the newly created partitions.
   If TAG is NULL, rewrite the set of call-clobbered variables.
   NEW_ALIASES is a scratch bitmap to build the new set of aliases for
   TAG.  */

static void
rewrite_alias_set_for (tree tag, bitmap new_aliases)
{
  bitmap_iterator bi;
  unsigned i;
  tree mpt, sym;

  EXECUTE_IF_SET_IN_BITMAP (MTAG_ALIASES (tag), 0, i, bi)
    {
      sym = referenced_var (i);
      mpt = memory_partition (sym);
      if (mpt)
	bitmap_set_bit (new_aliases, DECL_UID (mpt));
      else
	bitmap_set_bit (new_aliases, DECL_UID (sym));
    }

  /* Rebuild the may-alias array for TAG.  */
  bitmap_copy (MTAG_ALIASES (tag), new_aliases);
}


/* Determine how many virtual operands can be saved by partitioning
   MP_P->VAR into MPT.  When a symbol S is thrown inside a partition
   P, every virtual operand that used to reference S will now
   reference P.  Whether it reduces the number of virtual operands
   depends on:

   1- Direct references to S are never saved.  Instead of the virtual
      operand to S, we will now have a virtual operand to P.

   2- Indirect references to S are reduced only for those memory tags
      holding S that already had other symbols partitioned into P.
      For instance, if a memory tag T has the alias set { a b S c },
      the first time we partition S into P, the alias set will become
      { a b P c }, so no virtual operands will be saved. However, if
      we now partition symbol 'c' into P, then the alias set for T
      will become { a b P }, so we will be saving one virtual operand
      for every indirect reference to 'c'.

   3- Is S is call-clobbered, we save as many virtual operands as
      call/asm sites exist in the code, but only if other
      call-clobbered symbols have been grouped into P.  The first
      call-clobbered symbol that we group does not produce any
      savings.

   MEM_REF_STATS points to CFUN's memory reference information.  */

static void
estimate_vop_reduction (struct mem_ref_stats_d *mem_ref_stats,
                        mem_sym_stats_t mp_p, tree mpt)
{
  unsigned i;
  bitmap_iterator bi;
  mem_sym_stats_t mpt_stats;

  /* We should only get symbols with indirect references here.  */
  gcc_assert (mp_p->num_indirect_reads > 0 || mp_p->num_indirect_writes > 0);

  /* Note that the only statistics we keep for MPT is the set of
     parent tags to know which memory tags have had alias members
     partitioned, and the indicator has_call_clobbered_vars.
     Reference counts are not important for MPT.  */
  mpt_stats = get_mem_sym_stats_for (mpt);

  /* Traverse all the parent tags for MP_P->VAR.  For every tag T, if
     partition P is already grouping aliases of T, then reduce the
     number of virtual operands by the number of direct references
     to T.  */
  if (mp_p->parent_tags)
    {
      if (mpt_stats->parent_tags == NULL)
	mpt_stats->parent_tags = BITMAP_ALLOC (&alias_bitmap_obstack);

      EXECUTE_IF_SET_IN_BITMAP (mp_p->parent_tags, 0, i, bi)
	{
	  if (bitmap_bit_p (mpt_stats->parent_tags, i))
	    {
	      /* Partition MPT is already partitioning symbols in the
		 alias set for TAG.  This means that we are now saving
		 1 virtual operand for every direct reference to TAG.  */
	      tree tag = referenced_var (i);
	      mem_sym_stats_t tag_stats = mem_sym_stats (cfun, tag);
	      mem_ref_stats->num_vuses -= tag_stats->num_direct_reads;
	      mem_ref_stats->num_vdefs -= tag_stats->num_direct_writes;
	    }
	  else
	    {
	      /* This is the first symbol in tag I's alias set that is
		 being grouped under MPT.  We will not save any
		 virtual operands this time, but record that MPT is
		 grouping a symbol from TAG's alias set so that the
		 next time we get the savings.  */
	      bitmap_set_bit (mpt_stats->parent_tags, i);
	    }
	}
    }

  /* If MP_P->VAR is call-clobbered, and MPT is already grouping
     call-clobbered symbols, then we will save as many virtual
     operands as asm/call sites there are.  */
  if (is_call_clobbered (mp_p->var))
    {
      if (mpt_stats->has_call_clobbered_vars)
	mem_ref_stats->num_vdefs -= mem_ref_stats->num_call_sites
	                            + mem_ref_stats->num_asm_sites;
      else
	mpt_stats->has_call_clobbered_vars = true;
    }
}


/* Helper for compute_memory_partitions.  Transfer reference counts
   from pointers to their pointed-to sets.  Counters for pointers were
   computed by update_alias_info.  MEM_REF_STATS points to CFUN's
   memory reference information.  */

static void
update_reference_counts (struct mem_ref_stats_d *mem_ref_stats)
{
  unsigned i;
  bitmap_iterator bi;
  mem_sym_stats_t sym_stats;

  for (i = 1; i < num_ssa_names; i++)
    {
      tree ptr;
      struct ptr_info_def *pi;

      ptr = ssa_name (i);
      if (ptr
	  && POINTER_TYPE_P (TREE_TYPE (ptr))
	  && (pi = SSA_NAME_PTR_INFO (ptr)) != NULL
	  && pi->is_dereferenced)
	{
	  unsigned j;
	  bitmap_iterator bj;
	  tree tag;
	  mem_sym_stats_t ptr_stats, tag_stats;

	  /* If PTR has flow-sensitive points-to information, use
	     PTR's name tag, otherwise use the symbol tag associated
	     with PTR's symbol.  */
	  if (pi->name_mem_tag)
	    tag = pi->name_mem_tag;
	  else
	    tag = symbol_mem_tag (SSA_NAME_VAR (ptr));

	  ptr_stats = get_mem_sym_stats_for (ptr);
	  tag_stats = get_mem_sym_stats_for (tag);

	  /* TAG has as many direct references as dereferences we
	     found for its parent pointer.  */
	  tag_stats->num_direct_reads += ptr_stats->num_direct_reads;
	  tag_stats->num_direct_writes += ptr_stats->num_direct_writes;

	  /* All the dereferences of pointer PTR are considered direct
	     references to PTR's memory tag (TAG).  In turn,
	     references to TAG will become virtual operands for every
	     symbol in TAG's alias set.  So, for every symbol ALIAS in
	     TAG's alias set, add as many indirect references to ALIAS
	     as direct references there are for TAG.  */
	  if (MTAG_ALIASES (tag))
	    EXECUTE_IF_SET_IN_BITMAP (MTAG_ALIASES (tag), 0, j, bj)
	      {
		tree alias = referenced_var (j);
		sym_stats = get_mem_sym_stats_for (alias);

		/* All the direct references to TAG are indirect references
		   to ALIAS.  */
		sym_stats->num_indirect_reads += ptr_stats->num_direct_reads;
		sym_stats->num_indirect_writes += ptr_stats->num_direct_writes;
		sym_stats->frequency_reads += ptr_stats->frequency_reads;
		sym_stats->frequency_writes += ptr_stats->frequency_writes;

		/* Indicate that TAG is one of ALIAS's parent tags.  */
		if (sym_stats->parent_tags == NULL)
		  sym_stats->parent_tags = BITMAP_ALLOC (&alias_bitmap_obstack);
		bitmap_set_bit (sym_stats->parent_tags, DECL_UID (tag));
	      }
	}
    }

  /* Call-clobbered symbols are indirectly written at every
     call/asm site.  */
  EXECUTE_IF_SET_IN_BITMAP (gimple_call_clobbered_vars (cfun), 0, i, bi)
    {
      tree sym = referenced_var (i);
      sym_stats = get_mem_sym_stats_for (sym);
      sym_stats->num_indirect_writes += mem_ref_stats->num_call_sites
	                                + mem_ref_stats->num_asm_sites;
    }

  /* Addressable symbols are indirectly written at some ASM sites.
     Since only ASM sites that clobber memory actually affect
     addressable symbols, this is an over-estimation.  */
  EXECUTE_IF_SET_IN_BITMAP (gimple_addressable_vars (cfun), 0, i, bi)
    {
      tree sym = referenced_var (i);
      sym_stats = get_mem_sym_stats_for (sym);
      sym_stats->num_indirect_writes += mem_ref_stats->num_asm_sites;
    }
}


/* Helper for compute_memory_partitions.  Add all memory symbols to
   *MP_INFO_P and compute the initial estimate for the total number of
   virtual operands needed.  MEM_REF_STATS points to CFUN's memory
   reference information.  On exit, *TAGS_P will contain the list of
   memory tags whose alias set need to be rewritten after
   partitioning.  */

static void
build_mp_info (struct mem_ref_stats_d *mem_ref_stats,
               VEC(mem_sym_stats_t,heap) **mp_info_p,
	       VEC(tree,heap) **tags_p)
{
  tree var;
  referenced_var_iterator rvi;

  FOR_EACH_REFERENCED_VAR (var, rvi)
    {
      mem_sym_stats_t sym_stats;
      tree old_mpt;

      /* We are only interested in memory symbols other than MPTs.  */
      if (is_gimple_reg (var) || TREE_CODE (var) == MEMORY_PARTITION_TAG)
	continue;

      /* Collect memory tags into the TAGS array so that we can
	 rewrite their alias sets after partitioning.  */
      if (MTAG_P (var) && MTAG_ALIASES (var))
	VEC_safe_push (tree, heap, *tags_p, var);

      /* Since we are going to re-compute partitions, any symbols that
	 used to belong to a partition must be detached from it and
	 marked for renaming.  */
      if ((old_mpt = memory_partition (var)) != NULL)
	{
	  mark_sym_for_renaming (old_mpt);
	  set_memory_partition (var, NULL_TREE);
	  mark_sym_for_renaming (var);
	}

      sym_stats = get_mem_sym_stats_for (var);

      /* Add VAR's reference info to MP_INFO.  Note that the only
	 symbols that make sense to partition are those that have
	 indirect references.  If a symbol S is always directly
	 referenced, partitioning it will not reduce the number of
	 virtual operators.  The only symbols that are profitable to
	 partition are those that belong to alias sets and/or are
	 call-clobbered.  */
      if (sym_stats->num_indirect_reads > 0
	  || sym_stats->num_indirect_writes > 0)
	VEC_safe_push (mem_sym_stats_t, heap, *mp_info_p, sym_stats);

      /* Update the number of estimated VOPS.  Note that direct
	 references to memory tags are always counted as indirect
	 references to their alias set members, so if a memory tag has
	 aliases, do not count its direct references to avoid double
	 accounting.  */
      if (!MTAG_P (var) || !MTAG_ALIASES (var))
	{
	  mem_ref_stats->num_vuses += sym_stats->num_direct_reads;
	  mem_ref_stats->num_vdefs += sym_stats->num_direct_writes;
	}

      mem_ref_stats->num_vuses += sym_stats->num_indirect_reads;
      mem_ref_stats->num_vdefs += sym_stats->num_indirect_writes;
    }
}


/* Compute memory partitions.  A memory partition (MPT) is an
   arbitrary grouping of memory symbols, such that references to one
   member of the group is considered a reference to all the members of
   the group.
   
   As opposed to alias sets in memory tags, the grouping into
   partitions is completely arbitrary and only done to reduce the
   number of virtual operands.  The only rule that needs to be
   observed when creating memory partitions is that given two memory
   partitions MPT.i and MPT.j, they must not contain symbols in
   common.

   Memory partitions are used when putting the program into Memory-SSA
   form.  In particular, in Memory-SSA PHI nodes are not computed for
   individual memory symbols.  They are computed for memory
   partitions.  This reduces the amount of PHI nodes in the SSA graph
   at the expense of precision (i.e., it makes unrelated stores affect
   each other).
   
   However, it is possible to increase precision by changing this
   partitioning scheme.  For instance, if the partitioning scheme is
   such that get_mpt_for is the identity function (that is,
   get_mpt_for (s) = s), this will result in ultimate precision at the
   expense of huge SSA webs.

   At the other extreme, a partitioning scheme that groups all the
   symbols in the same set results in minimal SSA webs and almost
   total loss of precision.

   There partitioning heuristic uses three parameters to decide the
   order in which symbols are processed.  The list of symbols is
   sorted so that symbols that are more likely to be partitioned are
   near the top of the list:

   - Execution frequency.  If a memory references is in a frequently
     executed code path, grouping it into a partition may block useful
     transformations and cause sub-optimal code generation.  So, the
     partition heuristic tries to avoid grouping symbols with high
     execution frequency scores.  Execution frequency is taken
     directly from the basic blocks where every reference is made (see
     update_mem_sym_stats_from_stmt), which in turn uses the
     profile guided machinery, so if the program is compiled with PGO
     enabled, more accurate partitioning decisions will be made.

   - Number of references.  Symbols with few references in the code,
     are partitioned before symbols with many references.

   - NO_ALIAS attributes.  Symbols with any of the NO_ALIAS*
     attributes are partitioned after symbols marked MAY_ALIAS.

   Once the list is sorted, the partitioning proceeds as follows:

   1- For every symbol S in MP_INFO, create a new memory partition MP,
      if necessary.  To avoid memory partitions that contain symbols
      from non-conflicting alias sets, memory partitions are
      associated to the memory tag that holds S in its alias set.  So,
      when looking for a memory partition for S, the memory partition
      associated with one of the memory tags holding S is chosen.  If
      none exists, a new one is created.

   2- Add S to memory partition MP.

   3- Reduce by 1 the number of VOPS for every memory tag holding S.

   4- If the total number of VOPS is less than MAX_ALIASED_VOPS or the
      average number of VOPS per statement is less than
      AVG_ALIASED_VOPS, stop.  Otherwise, go to the next symbol in the
      list.  */

static void
compute_memory_partitions (void)
{
  tree tag;
  unsigned i;
  mem_sym_stats_t mp_p;
  VEC(mem_sym_stats_t,heap) *mp_info;
  bitmap new_aliases;
  VEC(tree,heap) *tags;
  struct mem_ref_stats_d *mem_ref_stats;
  int prev_max_aliased_vops;

  mem_ref_stats = gimple_mem_ref_stats (cfun);
  gcc_assert (mem_ref_stats->num_vuses == 0 && mem_ref_stats->num_vdefs == 0);

  if (mem_ref_stats->num_mem_stmts == 0)
    return;

  timevar_push (TV_MEMORY_PARTITIONING);

  mp_info = NULL;
  tags = NULL;
  prev_max_aliased_vops = MAX_ALIASED_VOPS;

  /* Since we clearly cannot lower the number of virtual operators
     below the total number of memory statements in the function, we
     may need to adjust MAX_ALIASED_VOPS beforehand.  */
  if (MAX_ALIASED_VOPS < mem_ref_stats->num_mem_stmts)
    MAX_ALIASED_VOPS = mem_ref_stats->num_mem_stmts;

  /* Update reference stats for all the pointed-to variables and
     memory tags.  */
  update_reference_counts (mem_ref_stats);

  /* Add all the memory symbols to MP_INFO.  */
  build_mp_info (mem_ref_stats, &mp_info, &tags);

  /* No partitions required if we are below the threshold.  */
  if (!need_to_partition_p (mem_ref_stats))
    {
      if (dump_file)
	fprintf (dump_file, "\nMemory partitioning NOT NEEDED for %s\n",
	         get_name (current_function_decl));
      goto done;
    }

  /* Sort the MP_INFO array so that symbols that should be partitioned
     first are near the top of the list.  */
  sort_mp_info (mp_info);

  if (dump_file)
    {
      fprintf (dump_file, "\nMemory partitioning NEEDED for %s\n\n",
	       get_name (current_function_decl));
      fprintf (dump_file, "Memory symbol references before partitioning:\n");
      dump_mp_info (dump_file, mp_info);
    }

  /* Create partitions for variables in MP_INFO until we have enough
     to lower the total number of VOPS below MAX_ALIASED_VOPS or if
     the average number of VOPS per statement is below
     AVG_ALIASED_VOPS.  */
  for (i = 0; VEC_iterate (mem_sym_stats_t, mp_info, i, mp_p); i++)
    {
      tree mpt;

      /* If we are below the threshold, stop.  */
      if (!need_to_partition_p (mem_ref_stats))
	break;

      /* SFTs that are marked unpartitionable should not be added to
	 partitions.  These SFTs are special because they mark the
	 first SFT into a structure where a pointer is pointing to.
	 This is needed by the operand scanner to find adjacent
	 fields.  See add_vars_for_offset for details.  */
      if (TREE_CODE (mp_p->var) == STRUCT_FIELD_TAG
	  && SFT_UNPARTITIONABLE_P (mp_p->var))
	continue;

      mpt = find_partition_for (mp_p);
      estimate_vop_reduction (mem_ref_stats, mp_p, mpt);
    }

  /* After partitions have been created, rewrite alias sets to use
     them instead of the original symbols.  This way, if the alias set
     was computed as { a b c d e f }, and the subset { b e f } was
     grouped into partition MPT.3, then the new alias set for the tag
     will be  { a c d MPT.3 }.
     
     Note that this is not strictly necessary.  The operand scanner
     will always check if a symbol belongs to a partition when adding
     virtual operands.  However, by reducing the size of the alias
     sets to be scanned, the work needed inside the operand scanner is
     significantly reduced.  */
  new_aliases = BITMAP_ALLOC (&alias_bitmap_obstack);

  for (i = 0; VEC_iterate (tree, tags, i, tag); i++)
    {
      rewrite_alias_set_for (tag, new_aliases);
      bitmap_clear (new_aliases);
    }

  BITMAP_FREE (new_aliases);

  if (dump_file)
    {
      fprintf (dump_file, "\nMemory symbol references after partitioning:\n");
      dump_mp_info (dump_file, mp_info);
    }

done:
  /* Free allocated memory.  */
  VEC_free (mem_sym_stats_t, heap, mp_info);
  VEC_free (tree, heap, tags);

  MAX_ALIASED_VOPS = prev_max_aliased_vops;

  timevar_pop (TV_MEMORY_PARTITIONING);
}


/* Compute may-alias information for every variable referenced in function
   FNDECL.

   Alias analysis proceeds in 3 main phases:

   1- Points-to and escape analysis.

   This phase walks the use-def chains in the SSA web looking for three
   things:

	* Assignments of the form P_i = &VAR
	* Assignments of the form P_i = malloc()
	* Pointers and ADDR_EXPR that escape the current function.

   The concept of 'escaping' is the same one used in the Java world.  When
   a pointer or an ADDR_EXPR escapes, it means that it has been exposed
   outside of the current function.  So, assignment to global variables,
   function arguments and returning a pointer are all escape sites, as are
   conversions between pointers and integers.

   This is where we are currently limited.  Since not everything is renamed
   into SSA, we lose track of escape properties when a pointer is stashed
   inside a field in a structure, for instance.  In those cases, we are
   assuming that the pointer does escape.

   We use escape analysis to determine whether a variable is
   call-clobbered.  Simply put, if an ADDR_EXPR escapes, then the variable
   is call-clobbered.  If a pointer P_i escapes, then all the variables
   pointed-to by P_i (and its memory tag) also escape.

   2- Compute flow-sensitive aliases

   We have two classes of memory tags.  Memory tags associated with the
   pointed-to data type of the pointers in the program.  These tags are
   called "symbol memory tag" (SMT).  The other class are those associated
   with SSA_NAMEs, called "name memory tag" (NMT). The basic idea is that
   when adding operands for an INDIRECT_REF *P_i, we will first check
   whether P_i has a name tag, if it does we use it, because that will have
   more precise aliasing information.  Otherwise, we use the standard symbol
   tag.

   In this phase, we go through all the pointers we found in points-to
   analysis and create alias sets for the name memory tags associated with
   each pointer P_i.  If P_i escapes, we mark call-clobbered the variables
   it points to and its tag.


   3- Compute flow-insensitive aliases

   This pass will compare the alias set of every symbol memory tag and
   every addressable variable found in the program.  Given a symbol
   memory tag SMT and an addressable variable V.  If the alias sets of
   SMT and V conflict (as computed by may_alias_p), then V is marked
   as an alias tag and added to the alias set of SMT.

   For instance, consider the following function:

	    foo (int i)
	    {
	      int *p, a, b;
	    
	      if (i > 10)
	        p = &a;
	      else
	        p = &b;
	    
	      *p = 3;
	      a = b + 2;
	      return *p;
	    }

   After aliasing analysis has finished, the symbol memory tag for pointer
   'p' will have two aliases, namely variables 'a' and 'b'.  Every time
   pointer 'p' is dereferenced, we want to mark the operation as a
   potential reference to 'a' and 'b'.

	    foo (int i)
	    {
	      int *p, a, b;

	      if (i_2 > 10)
		p_4 = &a;
	      else
		p_6 = &b;
	      # p_1 = PHI <p_4(1), p_6(2)>;

	      # a_7 = VDEF <a_3>;
	      # b_8 = VDEF <b_5>;
	      *p_1 = 3;

	      # a_9 = VDEF <a_7>
	      # VUSE <b_8>
	      a_9 = b_8 + 2;

	      # VUSE <a_9>;
	      # VUSE <b_8>;
	      return *p_1;
	    }

   In certain cases, the list of may aliases for a pointer may grow too
   large.  This may cause an explosion in the number of virtual operands
   inserted in the code.  Resulting in increased memory consumption and
   compilation time.

   When the number of virtual operands needed to represent aliased
   loads and stores grows too large (configurable with option --param
   max-aliased-vops and --param avg-aliased-vops), alias sets are
   grouped to avoid severe compile-time slow downs and memory
   consumption. See compute_memory_partitions.  */

unsigned int
compute_may_aliases (void)
{
  struct alias_info *ai;

  timevar_push (TV_TREE_MAY_ALIAS);
  
  memset (&alias_stats, 0, sizeof (alias_stats));

  /* Initialize aliasing information.  */
  ai = init_alias_info ();

  /* For each pointer P_i, determine the sets of variables that P_i may
     point-to.  For every addressable variable V, determine whether the
     address of V escapes the current function, making V call-clobbered
     (i.e., whether &V is stored in a global variable or if its passed as a
     function call argument).  */
  compute_points_to_sets (ai);

  /* Collect all pointers and addressable variables, compute alias sets,
     create memory tags for pointers and promote variables whose address is
     not needed anymore.  */
  setup_pointers_and_addressables (ai);

  /* Compute type-based flow-insensitive aliasing for all the type
     memory tags.  */
  compute_flow_insensitive_aliasing (ai);

  /* Compute flow-sensitive, points-to based aliasing for all the name
     memory tags.  */
  compute_flow_sensitive_aliasing (ai);
  
  /* Compute call clobbering information.  */
  compute_call_clobbered (ai);

  /* If the program makes no reference to global variables, but it
     contains a mixture of pure and non-pure functions, then we need
     to create use-def and def-def links between these functions to
     avoid invalid transformations on them.  */
  maybe_create_global_var ();

  /* If the program contains ref-all pointers, finalize may-alias information
     for them.  This pass needs to be run after call-clobbering information
     has been computed.  */
  if (ai->ref_all_symbol_mem_tag)
    finalize_ref_all_pointers (ai);

  /* Compute memory partitions for every memory variable.  */
  compute_memory_partitions ();

  /* Remove partitions with no symbols.  Partitions may end up with an
     empty MPT_SYMBOLS set if a previous round of alias analysis
     needed to partition more symbols.  Since we don't need those
     partitions anymore, remove them to free up the space.  */
  {
    tree mpt;
    unsigned i;
    VEC(tree,heap) *mpt_table;

    mpt_table = gimple_ssa_operands (cfun)->mpt_table;
    i = 0;
    while (i < VEC_length (tree, mpt_table))
      {
	mpt = VEC_index (tree, mpt_table, i);
	if (MPT_SYMBOLS (mpt) == NULL)
	  VEC_unordered_remove (tree, mpt_table, i);
	else
	  i++;
      }
  }

  /* Populate all virtual operands and newly promoted register operands.  */
  {
    block_stmt_iterator bsi;
    basic_block bb;
    FOR_EACH_BB (bb)
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	update_stmt_if_modified (bsi_stmt (bsi));
  }

  /* Debugging dumps.  */
  if (dump_file)
    {
      dump_mem_ref_stats (dump_file);
      dump_alias_info (dump_file);
      dump_points_to_info (dump_file);

      if (dump_flags & TDF_STATS)
	dump_alias_stats (dump_file);

      if (dump_flags & TDF_DETAILS)
	dump_referenced_vars (dump_file);
    }

  /* Report strict aliasing violations.  */
  strict_aliasing_warning_backend ();

  /* Deallocate memory used by aliasing data structures.  */
  delete_alias_info (ai);

  if (need_ssa_update_p ())
    update_ssa (TODO_update_ssa);

  timevar_pop (TV_TREE_MAY_ALIAS);
  
  return 0;
}

/* Data structure used to count the number of dereferences to PTR
   inside an expression.  */
struct count_ptr_d
{
  tree ptr;
  unsigned count;
};


/* Helper for count_uses_and_derefs.  Called by walk_tree to look for
   (ALIGN/MISALIGNED_)INDIRECT_REF nodes for the pointer passed in DATA.  */

static tree
count_ptr_derefs (tree *tp, int *walk_subtrees, void *data)
{
  struct count_ptr_d *count_p = (struct count_ptr_d *) data;

  /* Do not walk inside ADDR_EXPR nodes.  In the expression &ptr->fld,
     pointer 'ptr' is *not* dereferenced, it is simply used to compute
     the address of 'fld' as 'ptr + offsetof(fld)'.  */
  if (TREE_CODE (*tp) == ADDR_EXPR)
    {
      *walk_subtrees = 0;
      return NULL_TREE;
    }

  if (INDIRECT_REF_P (*tp) && TREE_OPERAND (*tp, 0) == count_p->ptr)
    count_p->count++;

  return NULL_TREE;
}


/* Count the number of direct and indirect uses for pointer PTR in
   statement STMT.  The number of direct uses is stored in
   *NUM_USES_P.  Indirect references are counted separately depending
   on whether they are store or load operations.  The counts are
   stored in *NUM_STORES_P and *NUM_LOADS_P.  */

void
count_uses_and_derefs (tree ptr, tree stmt, unsigned *num_uses_p,
		       unsigned *num_loads_p, unsigned *num_stores_p)
{
  ssa_op_iter i;
  tree use;

  *num_uses_p = 0;
  *num_loads_p = 0;
  *num_stores_p = 0;

  /* Find out the total number of uses of PTR in STMT.  */
  FOR_EACH_SSA_TREE_OPERAND (use, stmt, i, SSA_OP_USE)
    if (use == ptr)
      (*num_uses_p)++;

  /* Now count the number of indirect references to PTR.  This is
     truly awful, but we don't have much choice.  There are no parent
     pointers inside INDIRECT_REFs, so an expression like
     '*x_1 = foo (x_1, *x_1)' needs to be traversed piece by piece to
     find all the indirect and direct uses of x_1 inside.  The only
     shortcut we can take is the fact that GIMPLE only allows
     INDIRECT_REFs inside the expressions below.  */
  if (TREE_CODE (stmt) == GIMPLE_MODIFY_STMT
      || (TREE_CODE (stmt) == RETURN_EXPR
	  && TREE_CODE (TREE_OPERAND (stmt, 0)) == GIMPLE_MODIFY_STMT)
      || TREE_CODE (stmt) == ASM_EXPR
      || TREE_CODE (stmt) == CALL_EXPR)
    {
      tree lhs, rhs;

      if (TREE_CODE (stmt) == GIMPLE_MODIFY_STMT)
	{
	  lhs = GIMPLE_STMT_OPERAND (stmt, 0);
	  rhs = GIMPLE_STMT_OPERAND (stmt, 1);
	}
      else if (TREE_CODE (stmt) == RETURN_EXPR)
	{
	  tree e = TREE_OPERAND (stmt, 0);
	  lhs = GIMPLE_STMT_OPERAND (e, 0);
	  rhs = GIMPLE_STMT_OPERAND (e, 1);
	}
      else if (TREE_CODE (stmt) == ASM_EXPR)
	{
	  lhs = ASM_OUTPUTS (stmt);
	  rhs = ASM_INPUTS (stmt);
	}
      else
	{
	  lhs = NULL_TREE;
	  rhs = stmt;
	}

      if (lhs
	  && (TREE_CODE (lhs) == TREE_LIST
	      || EXPR_P (lhs)
	      || GIMPLE_STMT_P (lhs)))
	{
	  struct count_ptr_d count;
	  count.ptr = ptr;
	  count.count = 0;
	  walk_tree (&lhs, count_ptr_derefs, &count, NULL);
	  *num_stores_p = count.count;
	}

      if (rhs
	  && (TREE_CODE (rhs) == TREE_LIST
	      || EXPR_P (rhs)
	      || GIMPLE_STMT_P (rhs)))
	{
	  struct count_ptr_d count;
	  count.ptr = ptr;
	  count.count = 0;
	  walk_tree (&rhs, count_ptr_derefs, &count, NULL);
	  *num_loads_p = count.count;
	}
    }

  gcc_assert (*num_uses_p >= *num_loads_p + *num_stores_p);
}

/* Remove memory references stats for function FN.  */

void
delete_mem_ref_stats (struct function *fn)
{
  if (gimple_mem_ref_stats (fn)->mem_sym_stats)
    {
      free_alloc_pool (mem_sym_stats_pool);
      pointer_map_destroy (gimple_mem_ref_stats (fn)->mem_sym_stats);
    }
  gimple_mem_ref_stats (fn)->mem_sym_stats = NULL;
}


/* Initialize memory reference stats.  */

static void
init_mem_ref_stats (void)
{
  struct mem_ref_stats_d *mem_ref_stats = gimple_mem_ref_stats (cfun);

  mem_sym_stats_pool = create_alloc_pool ("Mem sym stats",
					  sizeof (struct mem_sym_stats_d),
					  100);
  memset (mem_ref_stats, 0, sizeof (struct mem_ref_stats_d));
  mem_ref_stats->mem_sym_stats = pointer_map_create ();
}


/* Helper for init_alias_info.  Reset existing aliasing information.  */

static void
reset_alias_info (void)
{
  referenced_var_iterator rvi;
  tree var;
  unsigned i;
  bitmap active_nmts, all_nmts;

  /* Clear the set of addressable variables.  We do not need to clear
     the TREE_ADDRESSABLE bit on every symbol because we are going to
     re-compute addressability here.  */
  bitmap_clear (gimple_addressable_vars (cfun));

  active_nmts = BITMAP_ALLOC (&alias_bitmap_obstack);
  all_nmts = BITMAP_ALLOC (&alias_bitmap_obstack);

  /* Clear flow-insensitive alias information from each symbol.  */
  FOR_EACH_REFERENCED_VAR (var, rvi)
    {
      if (is_gimple_reg (var))
	continue;

      if (MTAG_P (var))
	MTAG_ALIASES (var) = NULL;

      /* Memory partition information will be computed from scratch.  */
      if (TREE_CODE (var) == MEMORY_PARTITION_TAG)
	MPT_SYMBOLS (var) = NULL;

      /* Collect all the name tags to determine if we have any
	 orphaned that need to be removed from the IL.  A name tag
	 will be orphaned if it is not associated with any active SSA
	 name.  */
      if (TREE_CODE (var) == NAME_MEMORY_TAG)
	bitmap_set_bit (all_nmts, DECL_UID (var));

      /* Since we are about to re-discover call-clobbered
	 variables, clear the call-clobbered flag.  Variables that
	 are intrinsically call-clobbered (globals, local statics,
	 etc) will not be marked by the aliasing code, so we can't
	 remove them from CALL_CLOBBERED_VARS.  

	 NB: STRUCT_FIELDS are still call clobbered if they are for a
	 global variable, so we *don't* clear their call clobberedness
	 just because they are tags, though we will clear it if they
	 aren't for global variables.  */
      if (TREE_CODE (var) == NAME_MEMORY_TAG
	  || TREE_CODE (var) == SYMBOL_MEMORY_TAG
	  || TREE_CODE (var) == MEMORY_PARTITION_TAG
	  || !is_global_var (var))
	clear_call_clobbered (var);
    }

  /* Clear flow-sensitive points-to information from each SSA name.  */
  for (i = 1; i < num_ssa_names; i++)
    {
      tree name = ssa_name (i);

      if (!name || !POINTER_TYPE_P (TREE_TYPE (name)))
	continue;

      if (SSA_NAME_PTR_INFO (name))
	{
	  struct ptr_info_def *pi = SSA_NAME_PTR_INFO (name);

	  /* Clear all the flags but keep the name tag to
	     avoid creating new temporaries unnecessarily.  If
	     this pointer is found to point to a subset or
	     superset of its former points-to set, then a new
	     tag will need to be created in create_name_tags.  */
	  pi->pt_anything = 0;
	  pi->pt_null = 0;
	  pi->value_escapes_p = 0;
	  pi->is_dereferenced = 0;
	  if (pi->pt_vars)
	    bitmap_clear (pi->pt_vars);

	  /* Add NAME's name tag to the set of active tags.  */
	  if (pi->name_mem_tag)
	    bitmap_set_bit (active_nmts, DECL_UID (pi->name_mem_tag));
	}
    }

  /* Name memory tags that are no longer associated with an SSA name
     are considered stale and should be removed from the IL.  All the
     name tags that are in the set ALL_NMTS but not in ACTIVE_NMTS are
     considered stale and marked for renaming.  */
  bitmap_and_compl_into (all_nmts, active_nmts);
  mark_set_for_renaming (all_nmts);

  BITMAP_FREE (all_nmts);
  BITMAP_FREE (active_nmts);
}


/* Initialize the data structures used for alias analysis.  */

static struct alias_info *
init_alias_info (void)
{
  struct alias_info *ai;
  referenced_var_iterator rvi;
  tree var;

  ai = XCNEW (struct alias_info);
  ai->ssa_names_visited = sbitmap_alloc (num_ssa_names);
  sbitmap_zero (ai->ssa_names_visited);
  ai->processed_ptrs = VEC_alloc (tree, heap, 50);
  ai->written_vars = pointer_set_create ();
  ai->dereferenced_ptrs_store = pointer_set_create ();
  ai->dereferenced_ptrs_load = pointer_set_create ();

  /* Clear out all memory reference stats.  */
  init_mem_ref_stats ();

  /* If aliases have been computed before, clear existing information.  */
  if (gimple_aliases_computed_p (cfun))
    reset_alias_info ();
  else
    {
      /* If this is the first time we compute aliasing information,
	 every non-register symbol will need to be put into SSA form
	 (the initial SSA form only operates on GIMPLE registers).  */
      FOR_EACH_REFERENCED_VAR (var, rvi)
	if (!is_gimple_reg (var))
	  mark_sym_for_renaming (var);
    }

  /* Next time, we will need to reset alias information.  */
  cfun->gimple_df->aliases_computed_p = true;
  if (alias_bitmap_obstack.elements != NULL)
    bitmap_obstack_release (&alias_bitmap_obstack);    
  bitmap_obstack_initialize (&alias_bitmap_obstack);

  return ai;
}


/* Deallocate memory used by alias analysis.  */

static void
delete_alias_info (struct alias_info *ai)
{
  size_t i;

  sbitmap_free (ai->ssa_names_visited);

  VEC_free (tree, heap, ai->processed_ptrs);

  for (i = 0; i < ai->num_addressable_vars; i++)
    free (ai->addressable_vars[i]);
  free (ai->addressable_vars);
  
  for (i = 0; i < ai->num_pointers; i++)
    free (ai->pointers[i]);
  free (ai->pointers);

  pointer_set_destroy (ai->written_vars);
  pointer_set_destroy (ai->dereferenced_ptrs_store);
  pointer_set_destroy (ai->dereferenced_ptrs_load);
  free (ai);

  delete_mem_ref_stats (cfun);
  delete_points_to_sets ();
}


/* Used for hashing to identify pointer infos with identical
   pt_vars bitmaps.  */

static int
eq_ptr_info (const void *p1, const void *p2)
{
  const struct ptr_info_def *n1 = (const struct ptr_info_def *) p1;
  const struct ptr_info_def *n2 = (const struct ptr_info_def *) p2;
  return bitmap_equal_p (n1->pt_vars, n2->pt_vars);
}

static hashval_t
ptr_info_hash (const void *p)
{
  const struct ptr_info_def *n = (const struct ptr_info_def *) p;
  return bitmap_hash (n->pt_vars);
}


/* Create name tags for all the pointers that have been dereferenced.
   We only create a name tag for a pointer P if P is found to point to
   a set of variables (so that we can alias them to *P) or if it is
   the result of a call to malloc (which means that P cannot point to
   anything else nor alias any other variable).

   If two pointers P and Q point to the same set of variables, they
   are assigned the same name tag.  */

static void
create_name_tags (void)
{
  size_t i;
  VEC (tree, heap) *with_ptvars = NULL;
  tree ptr;
  htab_t ptr_hash;

  /* Collect the list of pointers with a non-empty points to set.  */
  for (i = 1; i < num_ssa_names; i++)
    {
      tree ptr = ssa_name (i);
      struct ptr_info_def *pi;

      if (!ptr
	  || !POINTER_TYPE_P (TREE_TYPE (ptr))
	  || !SSA_NAME_PTR_INFO (ptr))
	continue;

      pi = SSA_NAME_PTR_INFO (ptr);

      if (pi->pt_anything || !pi->is_dereferenced)
	{
	  /* No name tags for pointers that have not been
	     dereferenced or point to an arbitrary location.  */
	  pi->name_mem_tag = NULL_TREE;
	  continue;
	}

      /* Set pt_anything on the pointers without pt_vars filled in so
	 that they are assigned a symbol tag.  */
      if (pi->pt_vars && !bitmap_empty_p (pi->pt_vars))	
	VEC_safe_push (tree, heap, with_ptvars, ptr);
      else
	set_pt_anything (ptr);
    }
  
  /* If we didn't find any pointers with pt_vars set, we're done.  */
  if (!with_ptvars)
    return;

  ptr_hash = htab_create (10, ptr_info_hash, eq_ptr_info, NULL);

  /* Now go through the pointers with pt_vars, and find a name tag
     with the same pt_vars as this pointer, or create one if one
     doesn't exist.  */
  for (i = 0; VEC_iterate (tree, with_ptvars, i, ptr); i++)
    {
      struct ptr_info_def *pi = SSA_NAME_PTR_INFO (ptr);
      tree old_name_tag = pi->name_mem_tag;
      struct ptr_info_def **slot;
      
      /* If PTR points to a set of variables, check if we don't
	 have another pointer Q with the same points-to set before
	 creating a tag.  If so, use Q's tag instead of creating a
	 new one.
	 
	 This is important for not creating unnecessary symbols
	 and also for copy propagation.  If we ever need to
	 propagate PTR into Q or vice-versa, we would run into
	 problems if they both had different name tags because
	 they would have different SSA version numbers (which
	 would force us to take the name tags in and out of SSA).  */
      slot = (struct ptr_info_def **) htab_find_slot (ptr_hash, pi, INSERT);
      if (*slot)
        pi->name_mem_tag = (*slot)->name_mem_tag;
      else
	{
	  *slot = pi;

	  /* If we didn't find a pointer with the same points-to set
	     as PTR, create a new name tag if needed.  */
	  if (pi->name_mem_tag == NULL_TREE)
	    pi->name_mem_tag = get_nmt_for (ptr);
	}
      
      /* If the new name tag computed for PTR is different than
	 the old name tag that it used to have, then the old tag
	 needs to be removed from the IL, so we mark it for
	 renaming.  */
      if (old_name_tag && old_name_tag != pi->name_mem_tag)
	mark_sym_for_renaming (old_name_tag);

      /* Inherit volatility from the pointed-to type.  */
      TREE_THIS_VOLATILE (pi->name_mem_tag)
	|= TYPE_VOLATILE (TREE_TYPE (TREE_TYPE (ptr)));
      
      /* Mark the new name tag for renaming.  */
      mark_sym_for_renaming (pi->name_mem_tag);
    }

  htab_delete (ptr_hash);

  VEC_free (tree, heap, with_ptvars);
}


/* Union the alias set SET into the may-aliases for TAG.  */

static void
union_alias_set_into (tree tag, bitmap set)
{
  bitmap ma = MTAG_ALIASES (tag);
  
  if (bitmap_empty_p (set))
    return;
  
  if (!ma)
    ma = MTAG_ALIASES (tag) = BITMAP_ALLOC (&alias_bitmap_obstack);
  bitmap_ior_into (ma, set);
}


/* For every pointer P_i in AI->PROCESSED_PTRS, create may-alias sets for
   the name memory tag (NMT) associated with P_i.  If P_i escapes, then its
   name tag and the variables it points-to are call-clobbered.  Finally, if
   P_i escapes and we could not determine where it points to, then all the
   variables in the same alias set as *P_i are marked call-clobbered.  This
   is necessary because we must assume that P_i may take the address of any
   variable in the same alias set.  */

static void
compute_flow_sensitive_aliasing (struct alias_info *ai)
{
  size_t i;
  tree ptr;
  
  timevar_push (TV_FLOW_SENSITIVE);
  set_used_smts ();
  
  for (i = 0; VEC_iterate (tree, ai->processed_ptrs, i, ptr); i++)
    {
      if (!find_what_p_points_to (ptr))
	set_pt_anything (ptr);
    }

  create_name_tags ();

  for (i = 0; VEC_iterate (tree, ai->processed_ptrs, i, ptr); i++)
    {
      struct ptr_info_def *pi = SSA_NAME_PTR_INFO (ptr);
      tree tag = symbol_mem_tag (SSA_NAME_VAR (ptr));

      /* Set up aliasing information for PTR's name memory tag (if it has
	 one).  Note that only pointers that have been dereferenced will
	 have a name memory tag.  */
      if (pi->name_mem_tag && pi->pt_vars)
	{
	  if (!bitmap_empty_p (pi->pt_vars))
	    {
	      union_alias_set_into (pi->name_mem_tag, pi->pt_vars);
	      union_alias_set_into (tag, pi->pt_vars);
	      bitmap_clear_bit (MTAG_ALIASES (tag), DECL_UID (tag));
	    
	      /* It may be the case that this the tag uid was the only
		 bit we had set in the aliases list, and in this case,
		 we don't want to keep an empty bitmap, as this
		 asserts in tree-ssa-operands.c .  */
	      if (bitmap_empty_p (MTAG_ALIASES (tag)))
		BITMAP_FREE (MTAG_ALIASES (tag));
	    }
	}
    }
  timevar_pop (TV_FLOW_SENSITIVE);
}


/* Return TRUE if at least one symbol in TAG2's alias set is also
   present in TAG1's alias set.  */

static bool
have_common_aliases_p (bitmap tag1aliases, bitmap tag2aliases)
{

  /* This is the old behavior of have_common_aliases_p, which is to
     return false if both sets are empty, or one set is and the other
     isn't.  */
     if ((tag1aliases == NULL && tag2aliases != NULL)
      || (tag2aliases == NULL && tag1aliases != NULL)
      || (tag1aliases == NULL && tag2aliases == NULL))
    return false;

  return bitmap_intersect_p (tag1aliases, tag2aliases);
}

/* Compute type-based alias sets.  Traverse all the pointers and
   addressable variables found in setup_pointers_and_addressables.
   
   For every pointer P in AI->POINTERS and addressable variable V in
   AI->ADDRESSABLE_VARS, add V to the may-alias sets of P's symbol
   memory tag (SMT) if their alias sets conflict.  V is then marked as
   an aliased symbol so that the operand scanner knows that statements
   containing V have aliased operands.  */

static void
compute_flow_insensitive_aliasing (struct alias_info *ai)
{
  size_t i;

  timevar_push (TV_FLOW_INSENSITIVE);
  /* For every pointer P, determine which addressable variables may alias
     with P's symbol memory tag.  */
  for (i = 0; i < ai->num_pointers; i++)
    {
      size_t j;
      struct alias_map_d *p_map = ai->pointers[i];
      tree tag = symbol_mem_tag (p_map->var);
      tree var;

      /* Call-clobbering information is not finalized yet at this point.  */
      if (PTR_IS_REF_ALL (p_map->var))
	continue;

      for (j = 0; j < ai->num_addressable_vars; j++)
	{
	  struct alias_map_d *v_map;
	  var_ann_t v_ann;
	  bool tag_stored_p, var_stored_p;
	  
	  v_map = ai->addressable_vars[j];
	  var = v_map->var;
	  v_ann = var_ann (var);

	  /* Skip memory tags and variables that have never been
	     written to.  We also need to check if the variables are
	     call-clobbered because they may be overwritten by
	     function calls.  */
	  tag_stored_p = pointer_set_contains (ai->written_vars, tag)
	                 || is_call_clobbered (tag);
	  var_stored_p = pointer_set_contains (ai->written_vars, var)
	                 || is_call_clobbered (var);
	  if (!tag_stored_p && !var_stored_p)
	    continue;
	     
	  if (may_alias_p (p_map->var, p_map->set, var, v_map->set, false))
	    {
	      /* We should never have a var with subvars here, because
	         they shouldn't get into the set of addressable vars */
	      gcc_assert (!var_can_have_subvars (var)
			  || get_subvars_for_var (var) == NULL);

	      /* Add VAR to TAG's may-aliases set.  */
	      add_may_alias (tag, var);
	    }
	}
    }

  /* Since this analysis is based exclusively on symbols, it fails to
     handle cases where two pointers P and Q have different memory
     tags with conflicting alias set numbers but no aliased symbols in
     common.

     For example, suppose that we have two memory tags SMT.1 and SMT.2
     such that
     
     		may-aliases (SMT.1) = { a }
		may-aliases (SMT.2) = { b }

     and the alias set number of SMT.1 conflicts with that of SMT.2.
     Since they don't have symbols in common, loads and stores from
     SMT.1 and SMT.2 will seem independent of each other, which will
     lead to the optimizers making invalid transformations (see
     testsuite/gcc.c-torture/execute/pr15262-[12].c).

     To avoid this problem, we do a final traversal of AI->POINTERS
     looking for pairs of pointers that have no aliased symbols in
     common and yet have conflicting alias set numbers.  */
  for (i = 0; i < ai->num_pointers; i++)
    {
      size_t j;
      struct alias_map_d *p_map1 = ai->pointers[i];
      tree tag1 = symbol_mem_tag (p_map1->var);
      bitmap may_aliases1 = MTAG_ALIASES (tag1);

      if (PTR_IS_REF_ALL (p_map1->var))
	continue;

      for (j = i + 1; j < ai->num_pointers; j++)
	{
	  struct alias_map_d *p_map2 = ai->pointers[j];
	  tree tag2 = symbol_mem_tag (p_map2->var);
	  bitmap may_aliases2 = may_aliases (tag2);

	  if (PTR_IS_REF_ALL (p_map2->var))
	    continue;

	  /* If the pointers may not point to each other, do nothing.  */
	  if (!may_alias_p (p_map1->var, p_map1->set, tag2, p_map2->set, true))
	    continue;

	  /* The two pointers may alias each other.  If they already have
	     symbols in common, do nothing.  */
	  if (have_common_aliases_p (may_aliases1, may_aliases2))
	    continue;

	  if (may_aliases2 && !bitmap_empty_p (may_aliases2))
	    {
	      union_alias_set_into (tag1, may_aliases2);
	    }
	  else
	    {
	      /* Since TAG2 does not have any aliases of its own, add
		 TAG2 itself to the alias set of TAG1.  */
	      add_may_alias (tag1, tag2);
	    }
	}

    }
  timevar_pop (TV_FLOW_INSENSITIVE);
}


/* Finalize may-alias information for ref-all pointers.  Traverse all
   the addressable variables found in setup_pointers_and_addressables.

   If flow-sensitive alias analysis has attached a name memory tag to
   a ref-all pointer, we will use it for the dereferences because that
   will have more precise aliasing information.  But if there is no
   name tag, we will use a special symbol tag that aliases all the
   call-clobbered addressable variables.  */

static void
finalize_ref_all_pointers (struct alias_info *ai)
{
  size_t i;

  /* First add the real call-clobbered variables.  */
  for (i = 0; i < ai->num_addressable_vars; i++)
    {
      tree var = ai->addressable_vars[i]->var;
      if (is_call_clobbered (var))
	add_may_alias (ai->ref_all_symbol_mem_tag, var);
    }

  /* Then add the call-clobbered pointer memory tags.  See
     compute_flow_insensitive_aliasing for the rationale.  */
  for (i = 0; i < ai->num_pointers; i++)
    {
      tree ptr = ai->pointers[i]->var, tag;
      /* Avoid adding to self and clean up.  */
      if (PTR_IS_REF_ALL (ptr))
	{
	  struct ptr_info_def *pi = get_ptr_info (ptr);
	  if (pi->is_dereferenced)
	    pi->pt_anything = 0;
	  continue;
	}
      tag = symbol_mem_tag (ptr);
      if (is_call_clobbered (tag))
	add_may_alias (ai->ref_all_symbol_mem_tag, tag);
    }

}


/* Create a new alias set entry for VAR in AI->ADDRESSABLE_VARS.  */

static void
create_alias_map_for (tree var, struct alias_info *ai)
{
  struct alias_map_d *alias_map;
  alias_map = XCNEW (struct alias_map_d);
  alias_map->var = var;
  alias_map->set = get_alias_set (var);
  ai->addressable_vars[ai->num_addressable_vars++] = alias_map;
}


/* Create memory tags for all the dereferenced pointers and build the
   ADDRESSABLE_VARS and POINTERS arrays used for building the may-alias
   sets.  Based on the address escape and points-to information collected
   earlier, this pass will also clear the TREE_ADDRESSABLE flag from those
   variables whose address is not needed anymore.  */

static void
setup_pointers_and_addressables (struct alias_info *ai)
{
  size_t num_addressable_vars, num_pointers;
  referenced_var_iterator rvi;
  tree var;
  VEC (tree, heap) *varvec = NULL;
  safe_referenced_var_iterator srvi;

  /* Size up the arrays ADDRESSABLE_VARS and POINTERS.  */
  num_addressable_vars = num_pointers = 0;
  
  FOR_EACH_REFERENCED_VAR (var, rvi)
    {
      if (may_be_aliased (var))
	num_addressable_vars++;

      if (POINTER_TYPE_P (TREE_TYPE (var)))
	{
	  /* Since we don't keep track of volatile variables, assume that
	     these pointers are used in indirect store operations.  */
	  if (TREE_THIS_VOLATILE (var))
	    pointer_set_insert (ai->dereferenced_ptrs_store, var);

	  num_pointers++;
	}
    }

  /* Create ADDRESSABLE_VARS and POINTERS.  Note that these arrays are
     always going to be slightly bigger than we actually need them
     because some TREE_ADDRESSABLE variables will be marked
     non-addressable below and only pointers with unique symbol tags are
     going to be added to POINTERS.  */
  ai->addressable_vars = XCNEWVEC (struct alias_map_d *, num_addressable_vars);
  ai->pointers = XCNEWVEC (struct alias_map_d *, num_pointers);
  ai->num_addressable_vars = 0;
  ai->num_pointers = 0;

  FOR_EACH_REFERENCED_VAR_SAFE (var, varvec, srvi)
    {
      subvar_t svars;

      /* Name memory tags already have flow-sensitive aliasing
	 information, so they need not be processed by
	 compute_flow_insensitive_aliasing.  Similarly, symbol memory
	 tags are already accounted for when we process their
	 associated pointer. 
      
         Structure fields, on the other hand, have to have some of this
         information processed for them, but it's pointless to mark them
         non-addressable (since they are fake variables anyway).  */
      if (MTAG_P (var) && TREE_CODE (var) != STRUCT_FIELD_TAG)
	continue;

      /* Remove the ADDRESSABLE flag from every addressable variable whose
         address is not needed anymore.  This is caused by the propagation
         of ADDR_EXPR constants into INDIRECT_REF expressions and the
         removal of dead pointer assignments done by the early scalar
         cleanup passes.  */
      if (TREE_ADDRESSABLE (var))
	{
	  if (!bitmap_bit_p (gimple_addressable_vars (cfun), DECL_UID (var))
	      && TREE_CODE (var) != RESULT_DECL
	      && !is_global_var (var))
	    {
	      bool okay_to_mark = true;

	      /* Since VAR is now a regular GIMPLE register, we will need
		 to rename VAR into SSA afterwards.  */
	      mark_sym_for_renaming (var);

	      /* If VAR can have sub-variables, and any of its
		 sub-variables has its address taken, then we cannot
		 remove the addressable flag from VAR.  */
	      if (var_can_have_subvars (var)
		  && (svars = get_subvars_for_var (var)))
		{
		  unsigned int i;
		  tree subvar;

		  for (i = 0; VEC_iterate (tree, svars, i, subvar); ++i)
		    {	      
		      if (bitmap_bit_p (gimple_addressable_vars (cfun),
					DECL_UID (subvar)))
			okay_to_mark = false;
		      mark_sym_for_renaming (subvar);
		    }
		}

	      /* The address of VAR is not needed, remove the
		 addressable bit, so that it can be optimized as a
		 regular variable.  */
	      if (okay_to_mark)
		{
		  /* The memory partition holding VAR will no longer
		     contain VAR, and statements referencing it will need
		     to be updated.  */
		  if (memory_partition (var))
		    mark_sym_for_renaming (memory_partition (var));

		  mark_non_addressable (var);
		}
	    }
	}

      /* Global variables and addressable locals may be aliased.  Create an
         entry in ADDRESSABLE_VARS for VAR.  */
      if (may_be_aliased (var))
	{
	  if (!var_can_have_subvars (var)
	      || get_subvars_for_var (var) == NULL)
	    create_alias_map_for (var, ai);

	  mark_sym_for_renaming (var);
	}

      /* Add pointer variables that have been dereferenced to the POINTERS
         array and create a symbol memory tag for them.  */
      if (POINTER_TYPE_P (TREE_TYPE (var)))
	{
	  if ((pointer_set_contains (ai->dereferenced_ptrs_store, var)
	       || pointer_set_contains (ai->dereferenced_ptrs_load, var)))
	    {
	      tree tag, old_tag;
	      var_ann_t t_ann;

	      /* If pointer VAR still doesn't have a memory tag
		 associated with it, create it now or re-use an
		 existing one.  */
	      tag = get_smt_for (var, ai);
	      t_ann = var_ann (tag);

	      /* The symbol tag will need to be renamed into SSA
		 afterwards. Note that we cannot do this inside
		 get_smt_for because aliasing may run multiple times
		 and we only create symbol tags the first time.  */
	      mark_sym_for_renaming (tag);

	      /* Similarly, if pointer VAR used to have another type
		 tag, we will need to process it in the renamer to
		 remove the stale virtual operands.  */
	      old_tag = symbol_mem_tag (var);
	      if (old_tag)
		mark_sym_for_renaming (old_tag);

	      /* Associate the tag with pointer VAR.  */
	      set_symbol_mem_tag (var, tag);

	      /* If pointer VAR has been used in a store operation,
		 then its memory tag must be marked as written-to.  */
	      if (pointer_set_contains (ai->dereferenced_ptrs_store, var))
		pointer_set_insert (ai->written_vars, tag);
	    }
	  else
	    {
	      /* The pointer has not been dereferenced.  If it had a
		 symbol memory tag, remove it and mark the old tag for
		 renaming to remove it out of the IL.  */
	      tree tag = symbol_mem_tag (var);
	      if (tag)
		{
		  mark_sym_for_renaming (tag);
		  set_symbol_mem_tag (var, NULL_TREE);
		}
	    }
	}
    }

  VEC_free (tree, heap, varvec);
}


/* Determine whether to use .GLOBAL_VAR to model call clobbering
   semantics.  If the function makes no references to global
   variables and contains at least one call to a non-pure function,
   then we need to mark the side-effects of the call using .GLOBAL_VAR
   to represent all possible global memory referenced by the callee.  */

static void
maybe_create_global_var (void)
{
  /* No need to create it, if we have one already.  */
  if (gimple_global_var (cfun) == NULL_TREE)
    {
      struct mem_ref_stats_d *stats = gimple_mem_ref_stats (cfun);

      /* Create .GLOBAL_VAR if there are no call-clobbered
	 variables and the program contains a mixture of pure/const
	 and regular function calls.  This is to avoid the problem
	 described in PR 20115:

	      int X;
	      int func_pure (void) { return X; }
	      int func_non_pure (int a) { X += a; }
	      int foo ()
	      {
	 	int a = func_pure ();
		func_non_pure (a);
		a = func_pure ();
		return a;
	      }

	 Since foo() has no call-clobbered variables, there is
	 no relationship between the calls to func_pure and
	 func_non_pure.  Since func_pure has no side-effects, value
	 numbering optimizations elide the second call to func_pure.
	 So, if we have some pure/const and some regular calls in the
	 program we create .GLOBAL_VAR to avoid missing these
	 relations.  */
      if (bitmap_empty_p (gimple_call_clobbered_vars (cfun))
	  && stats->num_call_sites > 0
	  && stats->num_pure_const_call_sites > 0
	  && stats->num_call_sites > stats->num_pure_const_call_sites)
	create_global_var ();
    }
}


/* Return TRUE if pointer PTR may point to variable VAR.
   
   MEM_ALIAS_SET is the alias set for the memory location pointed-to by PTR
	This is needed because when checking for type conflicts we are
	interested in the alias set of the memory location pointed-to by
	PTR.  The alias set of PTR itself is irrelevant.
   
   VAR_ALIAS_SET is the alias set for VAR.  */

static bool
may_alias_p (tree ptr, alias_set_type mem_alias_set,
	     tree var, alias_set_type var_alias_set,
	     bool alias_set_only)
{
  tree mem;

  alias_stats.alias_queries++;
  alias_stats.simple_queries++;

  /* By convention, a variable cannot alias itself.  */
  mem = symbol_mem_tag (ptr);
  if (mem == var)
    {
      alias_stats.alias_noalias++;
      alias_stats.simple_resolved++;
      return false;
    }

  /* If -fargument-noalias-global is > 2, pointer arguments may
     not point to anything else.  */
  if (flag_argument_noalias > 2 && TREE_CODE (ptr) == PARM_DECL)
    {
      alias_stats.alias_noalias++;
      alias_stats.simple_resolved++;
      return false;
    }

  /* If -fargument-noalias-global is > 1, pointer arguments may
     not point to global variables.  */
  if (flag_argument_noalias > 1 && is_global_var (var)
      && TREE_CODE (ptr) == PARM_DECL)
    {
      alias_stats.alias_noalias++;
      alias_stats.simple_resolved++;
      return false;
    }

  /* If either MEM or VAR is a read-only global and the other one
     isn't, then PTR cannot point to VAR.  */
  if ((unmodifiable_var_p (mem) && !unmodifiable_var_p (var))
      || (unmodifiable_var_p (var) && !unmodifiable_var_p (mem)))
    {
      alias_stats.alias_noalias++;
      alias_stats.simple_resolved++;
      return false;
    }

  gcc_assert (TREE_CODE (mem) == SYMBOL_MEMORY_TAG);

  if (!DECL_NO_TBAA_P (ptr))
    {
      alias_stats.tbaa_queries++;

      /* If the alias sets don't conflict then MEM cannot alias VAR.  */
      if (!alias_sets_conflict_p (mem_alias_set, var_alias_set))
	{
	  alias_stats.alias_noalias++;
	  alias_stats.tbaa_resolved++;
	  return false;
	}

      /* If VAR is a record or union type, PTR cannot point into VAR
	 unless there is some explicit address operation in the
	 program that can reference a field of the type pointed-to by
	 PTR.  This also assumes that the types of both VAR and PTR
	 are contained within the compilation unit, and that there is
	 no fancy addressing arithmetic associated with any of the
	 types involved.  */
      if (mem_alias_set != 0 && var_alias_set != 0)
	{
	  tree ptr_type = TREE_TYPE (ptr);
	  tree var_type = TREE_TYPE (var);
      
	  /* The star count is -1 if the type at the end of the
	     pointer_to chain is not a record or union type. */ 
	  if ((!alias_set_only) && 
	      ipa_type_escape_star_count_of_interesting_type (var_type) >= 0)
	    {
	      int ptr_star_count = 0;
	  
	      /* ipa_type_escape_star_count_of_interesting_type is a
		 little too restrictive for the pointer type, need to
		 allow pointers to primitive types as long as those
		 types cannot be pointers to everything.  */
	      while (POINTER_TYPE_P (ptr_type))
		{
		  /* Strip the *s off.  */ 
		  ptr_type = TREE_TYPE (ptr_type);
		  ptr_star_count++;
		}
	  
	      /* There does not appear to be a better test to see if
		 the pointer type was one of the pointer to everything
		 types.  */
	      if (ptr_star_count > 0)
		{
		  alias_stats.structnoaddress_queries++;
		  if (ipa_type_escape_field_does_not_clobber_p (var_type, 
								TREE_TYPE (ptr)))
		    {
		      alias_stats.structnoaddress_resolved++;
		      alias_stats.alias_noalias++;
		      return false;
		    }
		}
	      else if (ptr_star_count == 0)
		{
		  /* If PTR_TYPE was not really a pointer to type, it cannot 
		     alias.  */ 
		  alias_stats.structnoaddress_queries++;
		  alias_stats.structnoaddress_resolved++;
		  alias_stats.alias_noalias++;
		  return false;
		}
	    }
	}
    }

  alias_stats.alias_mayalias++;
  return true;
}


/* Add ALIAS to the set of variables that may alias VAR.  */

static void
add_may_alias (tree var, tree alias)
{
  /* Don't allow self-referential aliases.  */
  gcc_assert (var != alias);

  /* ALIAS must be addressable if it's being added to an alias set.  */
#if 1
  TREE_ADDRESSABLE (alias) = 1;
#else
  gcc_assert (may_be_aliased (alias));
#endif

  /* VAR must be a symbol or a name tag.  */
  gcc_assert (TREE_CODE (var) == SYMBOL_MEMORY_TAG
              || TREE_CODE (var) == NAME_MEMORY_TAG);

  if (MTAG_ALIASES (var) == NULL)
    MTAG_ALIASES (var) = BITMAP_ALLOC (&alias_bitmap_obstack);
  
  bitmap_set_bit (MTAG_ALIASES (var), DECL_UID (alias));
}


/* Mark pointer PTR as pointing to an arbitrary memory location.  */

static void
set_pt_anything (tree ptr)
{
  struct ptr_info_def *pi = get_ptr_info (ptr);

  pi->pt_anything = 1;
  pi->pt_vars = NULL;

  /* The pointer used to have a name tag, but we now found it pointing
     to an arbitrary location.  The name tag needs to be renamed and
     disassociated from PTR.  */
  if (pi->name_mem_tag)
    {
      mark_sym_for_renaming (pi->name_mem_tag);
      pi->name_mem_tag = NULL_TREE;
    }
}


/* Return true if STMT is an "escape" site from the current function.  Escape
   sites those statements which might expose the address of a variable
   outside the current function.  STMT is an escape site iff:

   	1- STMT is a function call, or
	2- STMT is an __asm__ expression, or
	3- STMT is an assignment to a non-local variable, or
	4- STMT is a return statement.

   Return the type of escape site found, if we found one, or NO_ESCAPE
   if none.  */

enum escape_type
is_escape_site (tree stmt)
{
  tree call = get_call_expr_in (stmt);
  if (call != NULL_TREE)
    {
      if (!TREE_SIDE_EFFECTS (call))
	return ESCAPE_TO_PURE_CONST;

      return ESCAPE_TO_CALL;
    }
  else if (TREE_CODE (stmt) == ASM_EXPR)
    return ESCAPE_TO_ASM;
  else if (TREE_CODE (stmt) == GIMPLE_MODIFY_STMT)
    {
      tree lhs = GIMPLE_STMT_OPERAND (stmt, 0);

      /* Get to the base of _REF nodes.  */
      if (TREE_CODE (lhs) != SSA_NAME)
	lhs = get_base_address (lhs);

      /* If we couldn't recognize the LHS of the assignment, assume that it
	 is a non-local store.  */
      if (lhs == NULL_TREE)
	return ESCAPE_UNKNOWN;

      if (TREE_CODE (GIMPLE_STMT_OPERAND (stmt, 1)) == NOP_EXPR
	  || TREE_CODE (GIMPLE_STMT_OPERAND (stmt, 1)) == CONVERT_EXPR
	  || TREE_CODE (GIMPLE_STMT_OPERAND (stmt, 1)) == VIEW_CONVERT_EXPR)
	{
	  tree from
	    = TREE_TYPE (TREE_OPERAND (GIMPLE_STMT_OPERAND (stmt, 1), 0));
	  tree to = TREE_TYPE (GIMPLE_STMT_OPERAND (stmt, 1));

	  /* If the RHS is a conversion between a pointer and an integer, the
	     pointer escapes since we can't track the integer.  */
	  if (POINTER_TYPE_P (from) && !POINTER_TYPE_P (to))
	    return ESCAPE_BAD_CAST;

	  /* Same if the RHS is a conversion between a regular pointer and a
	     ref-all pointer since we can't track the SMT of the former.  */
	  if (POINTER_TYPE_P (from) && !TYPE_REF_CAN_ALIAS_ALL (from)
	      && POINTER_TYPE_P (to) && TYPE_REF_CAN_ALIAS_ALL (to))
	    return ESCAPE_BAD_CAST;
	}

      /* If the LHS is an SSA name, it can't possibly represent a non-local
	 memory store.  */
      if (TREE_CODE (lhs) == SSA_NAME)
	return NO_ESCAPE;

      /* FIXME: LHS is not an SSA_NAME.  Even if it's an assignment to a
	 local variables we cannot be sure if it will escape, because we
	 don't have information about objects not in SSA form.  Need to
	 implement something along the lines of

	 J.-D. Choi, M. Gupta, M. J. Serrano, V. C. Sreedhar, and S. P.
	 Midkiff, ``Escape analysis for java,'' in Proceedings of the
	 Conference on Object-Oriented Programming Systems, Languages, and
	 Applications (OOPSLA), pp. 1-19, 1999.  */
      return ESCAPE_STORED_IN_GLOBAL;
    }
  else if (TREE_CODE (stmt) == RETURN_EXPR)
    return ESCAPE_TO_RETURN;

  return NO_ESCAPE;
}

/* Create a new memory tag of type TYPE.
   Does NOT push it into the current binding.  */

tree
create_tag_raw (enum tree_code code, tree type, const char *prefix)
{
  tree tmp_var;

  tmp_var = build_decl (code, create_tmp_var_name (prefix), type);

  /* Make the variable writable.  */
  TREE_READONLY (tmp_var) = 0;

  /* It doesn't start out global.  */
  MTAG_GLOBAL (tmp_var) = 0;
  TREE_STATIC (tmp_var) = 0;
  TREE_USED (tmp_var) = 1;

  return tmp_var;
}

/* Create a new memory tag of type TYPE.  If IS_TYPE_TAG is true, the tag
   is considered to represent all the pointers whose pointed-to types are
   in the same alias set class.  Otherwise, the tag represents a single
   SSA_NAME pointer variable.  */

static tree
create_memory_tag (tree type, bool is_type_tag)
{
  tree tag = create_tag_raw (is_type_tag ? SYMBOL_MEMORY_TAG : NAME_MEMORY_TAG,
			     type, (is_type_tag) ? "SMT" : "NMT");

  /* By default, memory tags are local variables.  Alias analysis will
     determine whether they should be considered globals.  */
  DECL_CONTEXT (tag) = current_function_decl;

  /* Memory tags are by definition addressable.  */
  TREE_ADDRESSABLE (tag) = 1;

  set_symbol_mem_tag (tag, NULL_TREE);

  /* Add the tag to the symbol table.  */
  add_referenced_var (tag);

  return tag;
}


/* Create a name memory tag to represent a specific SSA_NAME pointer P_i.
   This is used if P_i has been found to point to a specific set of
   variables or to a non-aliased memory location like the address returned
   by malloc functions.  */

static tree
get_nmt_for (tree ptr)
{
  struct ptr_info_def *pi = get_ptr_info (ptr);
  tree tag = pi->name_mem_tag;

  if (tag == NULL_TREE)
    tag = create_memory_tag (TREE_TYPE (TREE_TYPE (ptr)), false);
  return tag;
}


/* Return the symbol memory tag associated to pointer PTR.  A memory
   tag is an artificial variable that represents the memory location
   pointed-to by PTR.  It is used to model the effects of pointer
   de-references on addressable variables.
   
   AI points to the data gathered during alias analysis.  This
   function populates the array AI->POINTERS.  */

static tree
get_smt_for (tree ptr, struct alias_info *ai)
{
  size_t i;
  tree tag;
  tree tag_type = TREE_TYPE (TREE_TYPE (ptr));
  alias_set_type tag_set = get_alias_set (tag_type);

  /* We use a unique memory tag for all the ref-all pointers.  */
  if (PTR_IS_REF_ALL (ptr))
    {
      if (!ai->ref_all_symbol_mem_tag)
	ai->ref_all_symbol_mem_tag = create_memory_tag (void_type_node, true);
      return ai->ref_all_symbol_mem_tag;
    }

  /* To avoid creating unnecessary memory tags, only create one memory tag
     per alias set class.  Note that it may be tempting to group
     memory tags based on conflicting alias sets instead of
     equivalence.  That would be wrong because alias sets are not
     necessarily transitive (as demonstrated by the libstdc++ test
     23_containers/vector/cons/4.cc).  Given three alias sets A, B, C
     such that conflicts (A, B) == true and conflicts (A, C) == true,
     it does not necessarily follow that conflicts (B, C) == true.  */
  for (i = 0, tag = NULL_TREE; i < ai->num_pointers; i++)
    {
      struct alias_map_d *curr = ai->pointers[i];
      tree curr_tag = symbol_mem_tag (curr->var);
      if (tag_set == curr->set)
	{
	  tag = curr_tag;
	  break;
	}
    }

  /* If VAR cannot alias with any of the existing memory tags, create a new
     tag for PTR and add it to the POINTERS array.  */
  if (tag == NULL_TREE)
    {
      struct alias_map_d *alias_map;

      /* If PTR did not have a symbol tag already, create a new SMT.*
	 artificial variable representing the memory location
	 pointed-to by PTR.  */
      tag = symbol_mem_tag (ptr);
      if (tag == NULL_TREE)
	tag = create_memory_tag (tag_type, true);

      /* Add PTR to the POINTERS array.  Note that we are not interested in
	 PTR's alias set.  Instead, we cache the alias set for the memory that
	 PTR points to.  */
      alias_map = XCNEW (struct alias_map_d);
      alias_map->var = ptr;
      alias_map->set = tag_set;
      ai->pointers[ai->num_pointers++] = alias_map;
    }

  /* If the pointed-to type is volatile, so is the tag.  */
  TREE_THIS_VOLATILE (tag) |= TREE_THIS_VOLATILE (tag_type);

  /* Make sure that the symbol tag has the same alias set as the
     pointed-to type or at least accesses through the pointer will
     alias that set.  The latter can happen after the vectorizer
     created pointers of vector type.  */
  gcc_assert (tag_set == get_alias_set (tag)
	      || alias_set_subset_of (tag_set, get_alias_set (tag)));

  return tag;
}


/* Create GLOBAL_VAR, an artificial global variable to act as a
   representative of all the variables that may be clobbered by function
   calls.  */

static void
create_global_var (void)
{
  tree global_var = build_decl (VAR_DECL, get_identifier (".GLOBAL_VAR"),
                                void_type_node);
  DECL_ARTIFICIAL (global_var) = 1;
  TREE_READONLY (global_var) = 0;
  DECL_EXTERNAL (global_var) = 1;
  TREE_STATIC (global_var) = 1;
  TREE_USED (global_var) = 1;
  DECL_CONTEXT (global_var) = NULL_TREE;
  TREE_THIS_VOLATILE (global_var) = 0;
  TREE_ADDRESSABLE (global_var) = 0;

  create_var_ann (global_var);
  mark_call_clobbered (global_var, ESCAPE_UNKNOWN);
  add_referenced_var (global_var);
  mark_sym_for_renaming (global_var);
  cfun->gimple_df->global_var = global_var;
}


/* Dump alias statistics on FILE.  */

static void 
dump_alias_stats (FILE *file)
{
  const char *funcname
    = lang_hooks.decl_printable_name (current_function_decl, 2);
  fprintf (file, "\nAlias statistics for %s\n\n", funcname);
  fprintf (file, "Total alias queries:\t%u\n", alias_stats.alias_queries);
  fprintf (file, "Total alias mayalias results:\t%u\n", 
	   alias_stats.alias_mayalias);
  fprintf (file, "Total alias noalias results:\t%u\n",
	   alias_stats.alias_noalias);
  fprintf (file, "Total simple queries:\t%u\n",
	   alias_stats.simple_queries);
  fprintf (file, "Total simple resolved:\t%u\n",
	   alias_stats.simple_resolved);
  fprintf (file, "Total TBAA queries:\t%u\n",
	   alias_stats.tbaa_queries);
  fprintf (file, "Total TBAA resolved:\t%u\n",
	   alias_stats.tbaa_resolved);
  fprintf (file, "Total non-addressable structure type queries:\t%u\n",
	   alias_stats.structnoaddress_queries);
  fprintf (file, "Total non-addressable structure type resolved:\t%u\n",
	   alias_stats.structnoaddress_resolved);
}
  

/* Dump alias information on FILE.  */

void
dump_alias_info (FILE *file)
{
  size_t i;
  const char *funcname
    = lang_hooks.decl_printable_name (current_function_decl, 2);
  referenced_var_iterator rvi;
  tree var;

  fprintf (file, "\nAlias information for %s\n\n", funcname);

  dump_memory_partitions (file);

  fprintf (file, "\nFlow-insensitive alias information for %s\n\n", funcname);

  fprintf (file, "Aliased symbols\n\n");
  
  FOR_EACH_REFERENCED_VAR (var, rvi)
    {
      if (may_be_aliased (var))
	dump_variable (file, var);
    }

  fprintf (file, "\nDereferenced pointers\n\n");

  FOR_EACH_REFERENCED_VAR (var, rvi)
    if (symbol_mem_tag (var))
      dump_variable (file, var);

  fprintf (file, "\nSymbol memory tags\n\n");
  
  FOR_EACH_REFERENCED_VAR (var, rvi)
    {
      if (TREE_CODE (var) == SYMBOL_MEMORY_TAG)
	dump_variable (file, var);
    }

  fprintf (file, "\n\nFlow-sensitive alias information for %s\n\n", funcname);

  fprintf (file, "SSA_NAME pointers\n\n");
  for (i = 1; i < num_ssa_names; i++)
    {
      tree ptr = ssa_name (i);
      struct ptr_info_def *pi;
      
      if (ptr == NULL_TREE)
	continue;

      pi = SSA_NAME_PTR_INFO (ptr);
      if (!SSA_NAME_IN_FREE_LIST (ptr)
	  && pi
	  && pi->name_mem_tag)
	dump_points_to_info_for (file, ptr);
    }

  fprintf (file, "\nName memory tags\n\n");
  
  FOR_EACH_REFERENCED_VAR (var, rvi)
    {
      if (TREE_CODE (var) == NAME_MEMORY_TAG)
	dump_variable (file, var);
    }

  fprintf (file, "\n");
}


/* Dump alias information on stderr.  */

void
debug_alias_info (void)
{
  dump_alias_info (stderr);
}


/* Return the alias information associated with pointer T.  It creates a
   new instance if none existed.  */

struct ptr_info_def *
get_ptr_info (tree t)
{
  struct ptr_info_def *pi;

  gcc_assert (POINTER_TYPE_P (TREE_TYPE (t)));

  pi = SSA_NAME_PTR_INFO (t);
  if (pi == NULL)
    {
      pi = GGC_CNEW (struct ptr_info_def);
      SSA_NAME_PTR_INFO (t) = pi;
    }

  return pi;
}


/* Dump points-to information for SSA_NAME PTR into FILE.  */

void
dump_points_to_info_for (FILE *file, tree ptr)
{
  struct ptr_info_def *pi = SSA_NAME_PTR_INFO (ptr);

  print_generic_expr (file, ptr, dump_flags);

  if (pi)
    {
      if (pi->name_mem_tag)
	{
	  fprintf (file, ", name memory tag: ");
	  print_generic_expr (file, pi->name_mem_tag, dump_flags);
	}

      if (pi->is_dereferenced)
	fprintf (file, ", is dereferenced");

      if (pi->value_escapes_p)
	fprintf (file, ", its value escapes");

      if (pi->pt_anything)
	fprintf (file, ", points-to anything");

      if (pi->pt_null)
	fprintf (file, ", points-to NULL");

      if (pi->pt_vars)
	{
	  fprintf (file, ", points-to vars: ");
	  dump_decl_set (file, pi->pt_vars);
	}
    }

  fprintf (file, "\n");
}


/* Dump points-to information for VAR into stderr.  */

void
debug_points_to_info_for (tree var)
{
  dump_points_to_info_for (stderr, var);
}


/* Dump points-to information into FILE.  NOTE: This function is slow, as
   it needs to traverse the whole CFG looking for pointer SSA_NAMEs.  */

void
dump_points_to_info (FILE *file)
{
  basic_block bb;
  block_stmt_iterator si;
  ssa_op_iter iter;
  const char *fname =
    lang_hooks.decl_printable_name (current_function_decl, 2);
  referenced_var_iterator rvi;
  tree var;

  fprintf (file, "\n\nPointed-to sets for pointers in %s\n\n", fname);

  /* First dump points-to information for the default definitions of
     pointer variables.  This is necessary because default definitions are
     not part of the code.  */
  FOR_EACH_REFERENCED_VAR (var, rvi)
    {
      if (POINTER_TYPE_P (TREE_TYPE (var)))
	{
	  tree def = gimple_default_def (cfun, var);
	  if (def)
	    dump_points_to_info_for (file, def);
	}
    }

  /* Dump points-to information for every pointer defined in the program.  */
  FOR_EACH_BB (bb)
    {
      tree phi;

      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	{
	  tree ptr = PHI_RESULT (phi);
	  if (POINTER_TYPE_P (TREE_TYPE (ptr)))
	    dump_points_to_info_for (file, ptr);
	}

	for (si = bsi_start (bb); !bsi_end_p (si); bsi_next (&si))
	  {
	    tree stmt = bsi_stmt (si);
	    tree def;
	    FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_DEF)
	      if (TREE_CODE (def) == SSA_NAME
		  && POINTER_TYPE_P (TREE_TYPE (def)))
		dump_points_to_info_for (file, def);
	  }
    }

  fprintf (file, "\n");
}


/* Dump points-to info pointed to by PTO into STDERR.  */

void
debug_points_to_info (void)
{
  dump_points_to_info (stderr);
}

/* Dump to FILE the list of variables that may be aliasing VAR.  */

void
dump_may_aliases_for (FILE *file, tree var)
{
  bitmap aliases;
  
  aliases = MTAG_ALIASES (var);
  if (aliases)
    {
      bitmap_iterator bi;
      unsigned int i;
      tree al;

      fprintf (file, "{ ");
      EXECUTE_IF_SET_IN_BITMAP (aliases, 0, i, bi)
	{
	  al = referenced_var (i);
	  print_generic_expr (file, al, dump_flags);
	  fprintf (file, " ");
	}
      fprintf (file, "}");
    }
}


/* Dump to stderr the list of variables that may be aliasing VAR.  */

void
debug_may_aliases_for (tree var)
{
  dump_may_aliases_for (stderr, var);
}


/* Return true if VAR may be aliased.  */

bool
may_be_aliased (tree var)
{
  /* Obviously.  */
  if (TREE_ADDRESSABLE (var))
    return true;

  /* Globally visible variables can have their addresses taken by other
     translation units.  */
  if (MTAG_P (var)
      && (MTAG_GLOBAL (var) || TREE_PUBLIC (var)))
    return true;
  else if (!MTAG_P (var)
           && (DECL_EXTERNAL (var) || TREE_PUBLIC (var)))
    return true;

  /* Automatic variables can't have their addresses escape any other
     way.  This must be after the check for global variables, as
     extern declarations do not have TREE_STATIC set.  */
  if (!TREE_STATIC (var))
    return false;

  /* If we're in unit-at-a-time mode, then we must have seen all
     occurrences of address-of operators, and so we can trust
     TREE_ADDRESSABLE.  Otherwise we can only be sure the variable
     isn't addressable if it's local to the current function.  */
  if (flag_unit_at_a_time)
    return false;

  if (decl_function_context (var) == current_function_decl)
    return false;

  return true;
}

/* The following is based on code in add_stmt_operand to ensure that the
   same defs/uses/vdefs/vuses will be found after replacing a reference
   to var (or ARRAY_REF to var) with an INDIRECT_REF to ptr whose value
   is the address of var.  Return a memtag for the ptr, after adding the 
   proper may_aliases to it (which are the aliases of var, if it has any,
   or var itself).  */

static tree
add_may_alias_for_new_tag (tree tag, tree var)
{
  bitmap aliases = NULL;
  
  if (MTAG_P (var))
    aliases = may_aliases (var);

  /* Case 1: |aliases| == 1  */
  if (aliases
      && bitmap_single_bit_set_p (aliases))
    {
      tree ali = referenced_var (bitmap_first_set_bit (aliases));
      if (TREE_CODE (ali) == SYMBOL_MEMORY_TAG)
        return ali;
    }

  /* Case 2: |aliases| == 0  */
  if (aliases == NULL)
    add_may_alias (tag, var);
  else
    {
      /* Case 3: |aliases| > 1  */
      union_alias_set_into (tag, aliases);
    }
  return tag;
}

/* Create a new symbol tag for PTR.  Construct the may-alias list of this type
   tag so that it has the aliasing of VAR, or of the relevant subvars of VAR
   according to the location accessed by EXPR.

   Note, the set of aliases represented by the new symbol tag are not marked
   for renaming.  */

void
new_type_alias (tree ptr, tree var, tree expr)
{
  tree tag_type = TREE_TYPE (TREE_TYPE (ptr));
  tree tag;
  subvar_t svars;
  tree ali = NULL_TREE;
  HOST_WIDE_INT offset, size, maxsize;
  tree ref;
  VEC (tree, heap) *overlaps = NULL;
  unsigned int len, i;
  tree subvar;


  gcc_assert (symbol_mem_tag (ptr) == NULL_TREE);
  gcc_assert (!MTAG_P (var));

  ref = get_ref_base_and_extent (expr, &offset, &size, &maxsize);
  gcc_assert (ref);

  tag = create_memory_tag (tag_type, true);
  set_symbol_mem_tag (ptr, tag);

  /* Add VAR to the may-alias set of PTR's new symbol tag.  If VAR has
     subvars, add the subvars to the tag instead of the actual var.  */
  if (var_can_have_subvars (ref)
      && (svars = get_subvars_for_var (ref)))
    {
      for (i = 0; VEC_iterate (tree, svars, i, subvar); ++i)
	{
          bool exact;

          if (overlap_subvar (offset, maxsize, subvar, &exact))
            VEC_safe_push (tree, heap, overlaps, subvar);
        }
      gcc_assert (overlaps != NULL);
    }
  else if (var_can_have_subvars (var)
	   && (svars = get_subvars_for_var (var)))
    {
      /* If the REF is not a direct access to VAR (e.g., it is a dereference
	 of a pointer), we should scan the virtual operands of REF the same
	 way as tree-ssa-operands do.  At the moment, this is somewhat
	 difficult, so we just give up and add all the subvars of VAR.
	 On mem-ssa branch, the scanning for virtual operands have been
	 split from the rest of tree-ssa-operands, so it should be much
	 easier to fix this problem correctly once mem-ssa is merged.  */
      for (i = 0; VEC_iterate (tree, svars, i, subvar); ++i)
	VEC_safe_push (tree, heap, overlaps, subvar);

      gcc_assert (overlaps != NULL);
    }
  else
    ali = add_may_alias_for_new_tag (tag, var);

  len = VEC_length (tree, overlaps);
  if (len > 0)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "\nnumber of overlapping subvars = %u\n", len);

      if (len == 1)
	ali = add_may_alias_for_new_tag (tag, VEC_index (tree, overlaps, 0));
      else if (len > 1)
	{
	  unsigned int k;
	  tree sv_var;

	  for (k = 0; VEC_iterate (tree, overlaps, k, sv_var); k++)
	    {
	      ali = add_may_alias_for_new_tag (tag, sv_var);

	      if (ali != tag)
		{
		  /* Can happen only if 'Case 1' of add_may_alias_for_new_tag
		     took place.  Since more than one svar was found, we add 
		     'ali' as one of the may_aliases of the new tag.  */ 
		  add_may_alias (tag, ali);
		  ali = tag;
		}
	    }
	}
      VEC_free (tree, heap, overlaps);
    }

  set_symbol_mem_tag (ptr, ali);
  MTAG_GLOBAL (tag) = is_global_var (var);
}

/* This represents the used range of a variable.  */

typedef struct used_part
{
  HOST_WIDE_INT minused;
  HOST_WIDE_INT maxused;
  /* True if we have an explicit use/def of some portion of this variable,
     even if it is all of it. i.e. a.b = 5 or temp = a.b.  */
  bool explicit_uses;
  /* True if we have an implicit use/def of some portion of this
     variable.  Implicit uses occur when we can't tell what part we
     are referencing, and have to make conservative assumptions.  */
  bool implicit_uses;
  /* True if the structure is only written to or taken its address.  */
  bool write_only;
} *used_part_t;

/* An array of used_part structures, indexed by variable uid.  */

static htab_t used_portions;

struct used_part_map
{
  unsigned int uid;
  used_part_t to;
};

/* Return true if the uid in the two used part maps are equal.  */

static int
used_part_map_eq (const void *va, const void *vb)
{
  const struct used_part_map *a = (const struct used_part_map *) va;
  const struct used_part_map *b = (const struct used_part_map *) vb;
  return (a->uid == b->uid);
}

/* Hash a from uid in a used_part_map.  */

static unsigned int
used_part_map_hash (const void *item)
{
  return ((const struct used_part_map *)item)->uid;
}

/* Free a used part map element.  */

static void 
free_used_part_map (void *item)
{
  free (((struct used_part_map *)item)->to);
  free (item);
}

/* Lookup a used_part structure for a UID.  */

static used_part_t
up_lookup (unsigned int uid)
{
  struct used_part_map *h, in;
  in.uid = uid;
  h = (struct used_part_map *) htab_find_with_hash (used_portions, &in, uid);
  if (!h)
    return NULL;
  return h->to;
}

/* Insert the pair UID, TO into the used part hashtable.  */
 
static void 
up_insert (unsigned int uid, used_part_t to)
{ 
  struct used_part_map *h;
  void **loc;

  h = XNEW (struct used_part_map);
  h->uid = uid;
  h->to = to;
  loc = htab_find_slot_with_hash (used_portions, h,
				  uid, INSERT);
  if (*loc != NULL)
    free (*loc);
  *(struct used_part_map **)  loc = h;
}


/* Given a variable uid, UID, get or create the entry in the used portions
   table for the variable.  */

static used_part_t
get_or_create_used_part_for (size_t uid)
{
  used_part_t up;
  if ((up = up_lookup (uid)) == NULL)
    {
      up = XCNEW (struct used_part);
      up->minused = INT_MAX;
      up->maxused = 0;
      up->explicit_uses = false;
      up->implicit_uses = false;
      up->write_only = true;
    }

  return up;
}


/* Create and return a structure sub-variable for field type FIELD at
   offset OFFSET, with size SIZE, of variable VAR.  If ALIAS_SET not
   -1 this field is non-addressable and we should use this alias set
   with this field.  */

static tree
create_sft (tree var, tree field, unsigned HOST_WIDE_INT offset,
	    unsigned HOST_WIDE_INT size, alias_set_type alias_set,
	    bool base_for_components)
{
  tree subvar = create_tag_raw (STRUCT_FIELD_TAG, field, "SFT");

  /* We need to copy the various flags from VAR to SUBVAR, so that
     they are is_global_var iff the original variable was.  */
  DECL_CONTEXT (subvar) = DECL_CONTEXT (var);
  MTAG_GLOBAL (subvar) = DECL_EXTERNAL (var);
  TREE_PUBLIC  (subvar) = TREE_PUBLIC (var);
  TREE_STATIC (subvar) = TREE_STATIC (var);
  TREE_READONLY (subvar) = TREE_READONLY (var);
  TREE_ADDRESSABLE (subvar) = TREE_ADDRESSABLE (var);

  /* Add the new variable to REFERENCED_VARS.  */
  set_symbol_mem_tag (subvar, NULL);
  add_referenced_var (subvar);
  SFT_PARENT_VAR (subvar) = var;
  SFT_OFFSET (subvar) = offset;
  SFT_SIZE (subvar) = size;
  SFT_ALIAS_SET (subvar) = alias_set;
  SFT_BASE_FOR_COMPONENTS_P (subvar) = base_for_components;
  SFT_UNPARTITIONABLE_P (subvar) = false;

  return subvar;
}


/* Given an aggregate VAR, create the subvariables that represent its
   fields.  */

static void
create_overlap_variables_for (tree var)
{
  VEC(fieldoff_s,heap) *fieldstack = NULL;
  used_part_t up;
  size_t uid = DECL_UID (var);

  up = up_lookup (uid);
  if (!up
      || up->write_only)
    return;

  push_fields_onto_fieldstack (TREE_TYPE (var), &fieldstack, 0, NULL,
			       TREE_TYPE (var));
  /* Make sure to not create SFTs for structs we won't generate variable
     infos for.  See tree-ssa-structalias.c:create_variable_info_for ().  */
  if (VEC_length (fieldoff_s, fieldstack) > 1
      && VEC_length (fieldoff_s, fieldstack) <= MAX_FIELDS_FOR_FIELD_SENSITIVE)
    {
      subvar_t *subvars;
      fieldoff_s *fo;
      bool notokay = false;
      int fieldcount = 0;
      int i;
      HOST_WIDE_INT lastfooffset = -1;
      HOST_WIDE_INT lastfosize = -1;
      tree lastfotype = NULL_TREE;

      /* Not all fields have DECL_SIZE set, and those that don't, we don't
	 know their size, and thus, can't handle.
	 The same is true of fields with DECL_SIZE that is not an integer
	 constant (such as variable sized fields).
	 Fields with offsets which are not constant will have an offset < 0 
	 We *could* handle fields that are constant sized arrays, but
	 currently don't.  Doing so would require some extra changes to
	 tree-ssa-operands.c.  */

      for (i = 0; VEC_iterate (fieldoff_s, fieldstack, i, fo); i++)
	{
	  if (!fo->size
	      || TREE_CODE (fo->size) != INTEGER_CST
	      || fo->offset < 0)
	    {
	      notokay = true;
	      break;
	    }
          fieldcount++;
	}

      /* The current heuristic we use is as follows:
	 If the variable has no used portions in this function, no
	 structure vars are created for it.
	 Otherwise,
         If the variable has less than SALIAS_MAX_IMPLICIT_FIELDS,
	 we always create structure vars for them.
	 If the variable has more than SALIAS_MAX_IMPLICIT_FIELDS, and
	 some explicit uses, we create structure vars for them.
	 If the variable has more than SALIAS_MAX_IMPLICIT_FIELDS, and
	 no explicit uses, we do not create structure vars for them.
      */
      
      if (fieldcount >= SALIAS_MAX_IMPLICIT_FIELDS
	  && !up->explicit_uses)
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Variable ");
	      print_generic_expr (dump_file, var, 0);
	      fprintf (dump_file, " has no explicit uses in this function, and is > SALIAS_MAX_IMPLICIT_FIELDS, so skipping\n");
	    }
	  notokay = true;
	}
      
      /* Bail out, if we can't create overlap variables.  */
      if (notokay)
	{
	  VEC_free (fieldoff_s, heap, fieldstack);
	  return;
	}
      
      /* Otherwise, create the variables.  */
      subvars = lookup_subvars_for_var (var);
      *subvars = VEC_alloc (tree, gc, VEC_length (fieldoff_s, fieldstack));
 
      sort_fieldstack (fieldstack);

      for (i = 0; VEC_iterate (fieldoff_s, fieldstack, i, fo); ++i)
	{
	  HOST_WIDE_INT fosize;
	  tree currfotype, subvar;

	  fosize = TREE_INT_CST_LOW (fo->size);
	  currfotype = fo->type;

	  /* If this field isn't in the used portion,
	     or it has the exact same offset and size as the last
	     field, skip it.  Note that we always need the field at
	     offset 0 so we can properly handle pointers to the
	     structure.  */

	  if ((fo->offset != 0
	       && ((fo->offset <= up->minused
		    && fo->offset + fosize <= up->minused)
		   || fo->offset >= up->maxused))
	      || (fo->offset == lastfooffset
		  && fosize == lastfosize
		  && currfotype == lastfotype))
	    continue;
	  subvar = create_sft (var, fo->type, fo->offset,
			       fosize, fo->alias_set, fo->base_for_components);
	  VEC_quick_push (tree, *subvars, subvar);

	  if (dump_file)
	    {
	      fprintf (dump_file, "structure field tag %s created for var %s",
		       get_name (subvar), get_name (var));
	      fprintf (dump_file, " offset " HOST_WIDE_INT_PRINT_DEC,
		       SFT_OFFSET (subvar));
	      fprintf (dump_file, " size " HOST_WIDE_INT_PRINT_DEC,
		       SFT_SIZE (subvar));
	      fprintf (dump_file, "\n");
	    }
	  
	  lastfotype = currfotype;
	  lastfooffset = fo->offset;
	  lastfosize = fosize;
	}

      /* Once we have created subvars, the original is no longer call
	 clobbered on its own.  Its call clobbered status depends
	 completely on the call clobbered status of the subvars.

	 add_referenced_var in the above loop will take care of
	 marking subvars of global variables as call clobbered for us
	 to start, since they are global as well.  */
      clear_call_clobbered (var);
    }

  VEC_free (fieldoff_s, heap, fieldstack);
}


/* Find the conservative answer to the question of what portions of what 
   structures are used by this statement.  We assume that if we have a
   component ref with a known size + offset, that we only need that part
   of the structure.  For unknown cases, or cases where we do something
   to the whole structure, we assume we need to create fields for the 
   entire structure.  */

static tree
find_used_portions (tree *tp, int *walk_subtrees, void *lhs_p)
{
  switch (TREE_CODE (*tp))
    {
    case GIMPLE_MODIFY_STMT:
      /* Recurse manually here to track whether the use is in the
	 LHS of an assignment.  */
      find_used_portions (&GIMPLE_STMT_OPERAND (*tp, 0), walk_subtrees, tp);
      return find_used_portions (&GIMPLE_STMT_OPERAND (*tp, 1),
	  			 walk_subtrees, NULL);
    case REALPART_EXPR:
    case IMAGPART_EXPR:
    case COMPONENT_REF:
    case ARRAY_REF:
      {
	HOST_WIDE_INT bitsize;
	HOST_WIDE_INT bitmaxsize;
	HOST_WIDE_INT bitpos;
	tree ref;
	ref = get_ref_base_and_extent (*tp, &bitpos, &bitsize, &bitmaxsize);
	if (DECL_P (ref)
	    && var_can_have_subvars (ref)
	    && bitmaxsize != -1)
	  {
	    size_t uid = DECL_UID (ref);
	    used_part_t up;

	    up = get_or_create_used_part_for (uid);	    

	    if (bitpos <= up->minused)
	      up->minused = bitpos;
	    if ((bitpos + bitmaxsize >= up->maxused))
	      up->maxused = bitpos + bitmaxsize;

	    if (bitsize == bitmaxsize)
	      up->explicit_uses = true;
	    else
	      up->implicit_uses = true;
	    if (!lhs_p)
	      up->write_only = false;
	    up_insert (uid, up);

	    *walk_subtrees = 0;
	    return NULL_TREE;
	  }
      }
      break;
      /* This is here to make sure we mark the entire base variable as used
	 when you take its address.  Because our used portion analysis is
	 simple, we aren't looking at casts or pointer arithmetic to see what
	 happens when you take the address.  */
    case ADDR_EXPR:
      {
	tree var = get_base_address (TREE_OPERAND (*tp, 0));

	if (var 
	    && DECL_P (var)
	    && DECL_SIZE (var)
	    && var_can_have_subvars (var)
	    && TREE_CODE (DECL_SIZE (var)) == INTEGER_CST)
	  {
	    used_part_t up;
	    size_t uid = DECL_UID (var);
	    
	    up = get_or_create_used_part_for (uid);
 
	    up->minused = 0;
	    up->maxused = TREE_INT_CST_LOW (DECL_SIZE (var));
	    up->implicit_uses = true;
	    if (!lhs_p)
	      up->write_only = false;

	    up_insert (uid, up);
	    *walk_subtrees = 0;
	    return NULL_TREE;
	  }
      }
      break;
    case CALL_EXPR:
      {
	int i;
	int nargs = call_expr_nargs (*tp);
	for (i = 0; i < nargs; i++)
	  {
	    tree *arg = &CALL_EXPR_ARG (*tp, i);
	    if (TREE_CODE (*arg) == ADDR_EXPR)
              find_used_portions (arg, walk_subtrees, NULL);
	  }
	*walk_subtrees = 0;
	return NULL_TREE;
      }
    case VAR_DECL:
    case PARM_DECL:
    case RESULT_DECL:
      {
	tree var = *tp;
	if (DECL_SIZE (var)
	    && var_can_have_subvars (var)
	    && TREE_CODE (DECL_SIZE (var)) == INTEGER_CST)
	  {
	    used_part_t up;
	    size_t uid = DECL_UID (var);
	    
	    up = get_or_create_used_part_for (uid);
 
	    up->minused = 0;
	    up->maxused = TREE_INT_CST_LOW (DECL_SIZE (var));
	    up->implicit_uses = true;

	    up_insert (uid, up);
	    *walk_subtrees = 0;
	    return NULL_TREE;
	  }
      }
      break;
      
    default:
      break;
      
    }
  return NULL_TREE;
}

/* Create structure field variables for structures used in this function.  */

static unsigned int
create_structure_vars (void)
{
  basic_block bb;
  safe_referenced_var_iterator rvi;
  VEC (tree, heap) *varvec = NULL;
  tree var;

  used_portions = htab_create (10, used_part_map_hash, used_part_map_eq, 
                               free_used_part_map);
  
  FOR_EACH_BB (bb)
    {
      block_stmt_iterator bsi;
      tree phi;
      
      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	{
	  use_operand_p use;
	  ssa_op_iter iter;

	  FOR_EACH_PHI_ARG (use, phi, iter, SSA_OP_USE)
	    {
	      tree op = USE_FROM_PTR (use);
	      walk_tree_without_duplicates (&op, find_used_portions,
					    NULL);
	    }
	}

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  walk_tree_without_duplicates (bsi_stmt_ptr (bsi), 
					find_used_portions,
					NULL);
	}
    }
  FOR_EACH_REFERENCED_VAR_SAFE (var, varvec, rvi)
    {
      /* The C++ FE creates vars without DECL_SIZE set, for some reason.  */
      if (var 	  
	  && DECL_SIZE (var)
	  && var_can_have_subvars (var)
	  && !MTAG_P (var)
	  && TREE_CODE (DECL_SIZE (var)) == INTEGER_CST)
	create_overlap_variables_for (var);
    }
  htab_delete (used_portions);
  VEC_free (tree, heap, varvec);

  /* Update SSA operands of statements mentioning variables we split.  */
  if (gimple_in_ssa_p (cfun))
    FOR_EACH_BB (bb)
      {
	block_stmt_iterator bsi;
	for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	  {
	    tree stmt = bsi_stmt (bsi);
	    bool update = false;
	    unsigned int i;
	    bitmap_iterator bi;

	    if (STORED_SYMS (stmt))
	       EXECUTE_IF_SET_IN_BITMAP (STORED_SYMS (stmt), 0, i, bi)
		{
		  tree sym = referenced_var_lookup (i);
		  if (get_subvars_for_var (sym))
		    {
		      update = true;
		      break;
		    }
		}

	    if (LOADED_SYMS (stmt) && !update)
	       EXECUTE_IF_SET_IN_BITMAP (LOADED_SYMS (stmt), 0, i, bi)
		{
		  tree sym = referenced_var_lookup (i);
		  if (get_subvars_for_var (sym))
		    {
		      update = true;
		      break;
		    }
		}

	    if (stmt_ann (stmt)->addresses_taken && !update)
	       EXECUTE_IF_SET_IN_BITMAP (stmt_ann (stmt)->addresses_taken,
					 0, i, bi)
		{
		  tree sym = referenced_var_lookup (i);
		  if (get_subvars_for_var (sym))
		    {
		      update = true;
		      break;
		    }
		}

	    if (update)
	      update_stmt (stmt);
	  }
      }

  return TODO_rebuild_alias;
}

static bool
gate_structure_vars (void)
{
  return flag_tree_salias != 0;
}

struct tree_opt_pass pass_create_structure_vars = 
{
  "salias",		 /* name */
  gate_structure_vars,	 /* gate */
  create_structure_vars, /* execute */
  NULL,			 /* sub */
  NULL,			 /* next */
  0,			 /* static_pass_number */
  0,			 /* tv_id */
  PROP_cfg,		 /* properties_required */
  0,			 /* properties_provided */
  0,			 /* properties_destroyed */
  0,			 /* todo_flags_start */
  TODO_dump_func,	 /* todo_flags_finish */
  0			 /* letter */
};

/* Reset the call_clobbered flags on our referenced vars.  In
   theory, this only needs to be done for globals.  */

static unsigned int
reset_cc_flags (void)
{
  tree var;
  referenced_var_iterator rvi;

  FOR_EACH_REFERENCED_VAR (var, rvi)
    var_ann (var)->call_clobbered = false;
  return 0;
}

struct tree_opt_pass pass_reset_cc_flags =
{
  NULL,		 /* name */
  NULL,  	 /* gate */
  reset_cc_flags, /* execute */
  NULL,			 /* sub */
  NULL,			 /* next */
  0,			 /* static_pass_number */
  0,			 /* tv_id */
  PROP_referenced_vars |PROP_cfg, /* properties_required */
  0,			 /* properties_provided */
  0,			 /* properties_destroyed */
  0,			 /* todo_flags_start */
  0,         	         /* todo_flags_finish */
  0			 /* letter */
};

static bool
gate_build_alias (void)
{
  return !gate_structure_vars();
}


struct tree_opt_pass pass_build_alias =
{
  "build_alias",            /* name */
  gate_build_alias,         /* gate */
  NULL,                     /* execute */
  NULL,                     /* sub */
  NULL,                     /* next */
  0,                        /* static_pass_number */
  0,                        /* tv_id */
  PROP_cfg | PROP_ssa,      /* properties_required */
  PROP_alias,               /* properties_provided */
  0,                        /* properties_destroyed */
  0,                        /* todo_flags_start */
  TODO_rebuild_alias,        /* todo_flags_finish */
  0                         /* letter */
};
