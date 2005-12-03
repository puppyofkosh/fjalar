
/*--------------------------------------------------------------------*/
/*--- The main DynComp file (analogous to mc_main.c)               ---*/
/*---                                               dyncomp_main.c ---*/
/*--------------------------------------------------------------------*/

/*
  This file is part of DynComp, a dynamic comparability analysis tool
  for C/C++ based upon the Valgrind binary instrumentation framework
  and the Valgrind MemCheck tool (Copyright (C) 2000-2005 Julian
  Seward, jseward@acm.org)

  Copyright (C) 2004-2005 Philip Guo, MIT CSAIL Program Analysis Group

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.
*/

#include "kvasir_main.h"
#include "mc_include.h"
#include "dyncomp_main.h"
#include "dyncomp_runtime.h"
#include <limits.h>

// Special reserved tags
const UInt ESP_TAG          =  UINT_MAX;
const UInt LITERAL_TAG      = (UINT_MAX - 1);
const UInt LARGEST_REAL_TAG = (UINT_MAX - 2);


// For debug printouts
//extern char within_main_program;

/*------------------------------------------------------------------*/
/*--- Tags and the value comparability union-find data structure ---*/
/*------------------------------------------------------------------*/

// This is a serial number which increases every time a new tag
// The tag of 0 for a byte of memory means NO tag associated
// with it.  That's why nextTag starts at 1 and NOT 0.
// After garbage collection, this will hopefully decrease.
UInt nextTag = 1;

// The total number of tags that have ever been assigned throughout the
// duration of the program.  This is non-decreasing throughout the
// execution of the program
UInt totalNumTagsAssigned = 0;


/* The two-level tag map works almost like the memory map.  Its
   purpose is to implement a sparse array which can hold up to 2^32
   UInts.  The primary map holds 2^16 references to secondary maps.
   Each secondary map holds 2^16 UInt entries, each of which is 4
   bytes total.  Thus, each secondary map takes up 262,144 bytes.
   Each byte of memory should be shadowed with a corresponding tag.  A
   tag value of 0 means that there is NO tag associated with the byte.
*/
UInt* primary_tag_map[PRIMARY_SIZE];

// The number of entries in primary_tag_map that are initialized
// Range is [0, PRIMARY_SIZE]
UInt n_primary_tag_map_init_entries = 0;

#define IS_SECONDARY_TAG_MAP_NULL(a) (primary_tag_map[PM_IDX(a)] == NULL)

__inline__ UInt get_tag ( Addr a )
{
  if (IS_SECONDARY_TAG_MAP_NULL(a))
    return 0; // 0 means NO tag for that byte
  else
    return primary_tag_map[PM_IDX(a)][SM_OFF(a)];
}

__inline__ void set_tag ( Addr a, UInt tag )
{
  if (IS_SECONDARY_TAG_MAP_NULL(a)) {
    UInt* new_tag_array =
      (UInt*)VG_(shadow_alloc)(SECONDARY_SIZE * sizeof(*new_tag_array));
    VG_(memset)(new_tag_array, 0, (SECONDARY_SIZE * sizeof(*new_tag_array)));
    primary_tag_map[PM_IDX(a)] = new_tag_array;
    n_primary_tag_map_init_entries++;

    //    if (!dyncomp_no_gc) {
    //      check_whether_to_garbage_collect();
    //    }
  }
  primary_tag_map[PM_IDX(a)][SM_OFF(a)] = tag;
}

// Return a fresh tag and create a singleton set
// for the uf_object associated with that tag
static __inline__ UInt grab_fresh_tag() {
  UInt tag;

  // Let's try garbage collecting here.  Remember to assign
  // tag = nextTag AFTER garbage collection (if it occurs) because
  // nextTag may decrease due to the garbage collection step
  if ((!dyncomp_no_gc) &&
      totalNumTagsAssigned && // Don't garbage collect when it's zero
      (totalNumTagsAssigned % dyncomp_gc_after_n_tags == 0)) {
    garbage_collect_tags();
  }

  // For debug:
  //  if ((nextTag % 10000000) == 0) {
  //    VG_(printf)("nextTag: %u\n", nextTag);
  //  }

  tag = nextTag;

  // Remember to make a new singleton set for the
  // uf_object associated with that tag
  val_uf_make_set_for_tag(tag);

  if (nextTag == LARGEST_REAL_TAG) {
    VG_(printf)("Error! Maximum tag has been used.\n");
  }
  else {
    nextTag++;
  }

  totalNumTagsAssigned++;

  return tag;
}

// Allocate a new unique tag for all bytes in range [a, a + len)
__inline__ void allocate_new_unique_tags ( Addr a, SizeT len ) {
  Addr curAddr;
  UInt newTag;

/*   if (within_main_program) { */
/*     DYNCOMP_DPRINTF("allocate_new_unique_tags (a=0x%x, len=%d)\n", */
/*                     a, len); */
/*   } */
  for (curAddr = a; curAddr < (a+len); curAddr++) {
    newTag = grab_fresh_tag();
    set_tag(curAddr, newTag);
  }
}

// Copies tags of len bytes from src to dst
// Set both the tags of 'src' and 'dst' to their
// respective leaders for every byte
void copy_tags(  Addr src, Addr dst, SizeT len ) {
  SizeT i;

  for (i = 0; i < len; i++) {
    UInt leader = val_uf_find_leader(get_tag(src + i));
    set_tag (src + i, leader);
    set_tag (dst + i, leader);
  }
}


/* The two-level value uf_object map works almost like the memory map.
   Its purpose is to implement a sparse array which can hold
   up to 2^32 uf_object entries.  The primary map holds 2^16
   references to secondary maps.  Each secondary map holds 2^16
   uf_object entries, each of which is 12 bytes total.  Thus,
   each secondary map takes up 786,432 bytes.
   The main difference between this sparse array structure and
   the tag map is that this one fills up sequentially from
   lower indices to higher indices because tags are assigned
   (more or less) sequentially using nextTag and tag serial
   numbers are used as indices into the uf_object map
*/

// val_uf_object_map: A map from tag (32-bit int) to uf_objects
// Each entry either points to NULL or to a dynamically-allocated
// array (of size SECONDARY_SIZE) of uf_object objects
uf_object* primary_val_uf_object_map[PRIMARY_SIZE];

// The number of entries that are initialized in primary_val_uf_object_map
// Range is [0, PRIMARY_SIZE]
UInt n_primary_val_uf_object_map_init_entries = 0;

void val_uf_make_set_for_tag(UInt tag) {
  //  VG_(printf)("val_uf_make_set_for_tag(%u);\n", tag);

  if (IS_ZERO_TAG(tag))
    return;

  if (IS_SECONDARY_UF_NULL(tag)) {
    uf_object* new_uf_obj_array =
      (uf_object*)VG_(shadow_alloc)(SECONDARY_SIZE * sizeof(*new_uf_obj_array));

    // PG - We can skip this step and leave them uninitialized
    //      until somebody explicitly calls val_uf_make_set_for_tag() on
    //      that particular tag

    // Each new uf_object should be initialized using uf_make_set()
    //    UInt i;
    //    UInt curTag; // Set to upper 16-bits of the tag
    //    for (i = 0, curTag = ((PM_IDX(tag)) << SECONDARY_SHIFT);
    //         i < SECONDARY_SIZE;
    //         i++, curTag++) {
    //      uf_make_set(new_uf_obj_array + i, curTag);
    //      //      VG_(printf)("      uf_make_set(%u, %u);\n",
    //      //                  new_uf_obj_array + i, curTag);
    //    }
    primary_val_uf_object_map[PM_IDX(tag)] = new_uf_obj_array;
    n_primary_val_uf_object_map_init_entries++;

    //    if (!dyncomp_no_gc) {
      //      check_whether_to_garbage_collect();
      //    }
  }
  //  else {
  //    uf_make_set(GET_UF_OBJECT_PTR(tag), tag);
  //  }

  // Do this unconditionally now:
  uf_make_set(GET_UF_OBJECT_PTR(tag), tag);
}

// Merge the sets of tag1 and tag2 and return the leader
static __inline__ UInt val_uf_tag_union(UInt tag1, UInt tag2) {
  if (!IS_ZERO_TAG(tag1) && !IS_SECONDARY_UF_NULL(tag1) &&
      !IS_ZERO_TAG(tag2) && !IS_SECONDARY_UF_NULL(tag2)) {
    uf_object* leader = uf_union(GET_UF_OBJECT_PTR(tag1),
                                 GET_UF_OBJECT_PTR(tag2));
    return leader->tag;
  }
  else {
    return 0;
  }
}

static  __inline__ uf_name val_uf_tag_find(UInt tag) {
  if (IS_ZERO_TAG(tag) || IS_SECONDARY_UF_NULL(tag)) {
    return NULL;
  }
  else {
    return uf_find(GET_UF_OBJECT_PTR(tag));
  }
}

// Be careful not to bust a false positive by naively
// comparing val_uf_tag_find(tag1) and val_uf_tag_find(tag2)
// because you could be comparing 0 == 0 if both satisfy
// IS_SECONDARY_UF_NULL
/* static UChar val_uf_tags_in_same_set(UInt tag1, UInt tag2) { */
/*   if (!IS_ZERO_TAG(tag1) && !IS_SECONDARY_UF_NULL(tag1) && */
/*       !IS_ZERO_TAG(tag2) && !IS_SECONDARY_UF_NULL(tag2)) { */
/*     return (val_uf_tag_find(tag1) == val_uf_tag_find(tag2)); */
/*   } */
/*   else { */
/*     return 0; */
/*   } */
/* } */

// Helper functions called from mc_translate.c:

// Write tag into all addresses in the range [a, a+len)
static __inline__ void set_tag_for_range(Addr a, SizeT len, UInt tag) {
  Addr curAddr;

  // Don't be as aggressive on the leader optimization for now:
  //  UInt leader = val_uf_find_leader(tag);

  for (curAddr = a; curAddr < (a+len); curAddr++) {
    set_tag(curAddr, tag);
  }
}

// Only used for 'anchoring' the IR tree branch generated by Mux and
// conditional exit expressions so that the optimizer doesn't delete
// them
VGA_REGPARM(1)
UInt MC_(helperc_TAG_NOP) ( UInt tag ) {
   return tag;
}

// When we're requesting to store tags for X bytes,
// we will write the tag into all X bytes.

// When kvasir_dyncomp_fast_mode is on, check whether
// the tag to be stored is LITERAL_TAG.  If it is, create
// a new tag and store that in memory instead of LITERAL_TAG.
// (We don't need to do a check for kvasir_dyncomp_fast_mode
//  because LITERAL_TAG should never be used for regular real tags
//  regardless of whether kvasir_dyncomp_fast_mode is on or not.
//  Real tags are in the range of [1, LARGEST_REAL_TAG])

// For some reason, 64-bit stuff needs REGPARM(1) (Look in
// mc_translate.c) - this is very important for some reason
VGA_REGPARM(1)
void MC_(helperc_STORE_TAG_8) ( Addr a, UInt tag ) {
  UInt tagToWrite;

  if (LITERAL_TAG == tag) {
    tagToWrite = grab_fresh_tag();
  }
  else {
    tagToWrite = tag;
  }

  set_tag_for_range(a, 8, tagToWrite);
}

VGA_REGPARM(2)
void MC_(helperc_STORE_TAG_4) ( Addr a, UInt tag ) {
  UInt tagToWrite;

  if (LITERAL_TAG == tag) {
    tagToWrite = grab_fresh_tag();
  }
  else {
    tagToWrite = tag;
  }

  set_tag_for_range(a, 4, tagToWrite);
}

VGA_REGPARM(2)
void MC_(helperc_STORE_TAG_2) ( Addr a, UInt tag ) {
  UInt tagToWrite;

  if (LITERAL_TAG == tag) {
    tagToWrite = grab_fresh_tag();
  }
  else {
    tagToWrite = tag;
  }

  set_tag_for_range(a, 2, tagToWrite);
}

VGA_REGPARM(2)
void MC_(helperc_STORE_TAG_1) ( Addr a, UInt tag ) {
  UInt tagToWrite;

  if (LITERAL_TAG == tag) {
    tagToWrite = grab_fresh_tag();
  }
  else {
    tagToWrite = tag;
  }

  set_tag(a, tagToWrite);
}

// Return the leader (canonical tag) of the set which 'tag' belongs to
__inline__ UInt val_uf_find_leader(UInt tag) {
  uf_name canonical = val_uf_tag_find(tag);
  if (canonical) {
    return canonical->tag;
  }
  else {
    return 0;
  }
}

// Unions the tags belonging to these addresses and set
// the tags of both to the canonical tag (for efficiency)
void val_uf_union_tags_at_addr(Addr a1, Addr a2) {
  UInt canonicalTag;
  UInt tag1 = get_tag(a1);
  UInt tag2 = get_tag(a2);

  if ((0 == tag1) ||
      (0 == tag2) ||
      (tag1 == tag2)) {
    return;
  }

  canonicalTag = val_uf_tag_union(tag1, tag2);

  set_tag(a1, canonicalTag);
  set_tag(a2, canonicalTag);

  DYNCOMP_DPRINTF("val_uf_union_tags_at_addr(0x%x, 0x%x) canonicalTag=%u\n",
                  a1, a2, canonicalTag);
}

// Union the tags of all addresses in the range [a, a+max)
// and sets them all equal to the canonical tag of the merged set
// (An optimization which could help out with garbage collection
//  because we want to have as few tags 'in play' at one time
//  as possible)
// Returns the canonical tag of the merged set as the result
UInt val_uf_union_tags_in_range(Addr a, SizeT len) {
  Addr curAddr;
  UInt canonicalTag;
  UInt tagToMerge = 0;
  UInt curTag;

  // If kvasir_dyncomp_fast_mode is on, then if all of the tags
  // in range are LITERAL_TAG, then create a new tag and copy
  // it into all of those locations
  // (This is currently turned off because it loses too much precision)
/*   if (kvasir_dyncomp_fast_mode) { */
/*     char allLiteralTags = 1; */
/*     for (curAddr = a; curAddr < (a + len); curAddr++) { */
/*       if (get_tag(curAddr) != LITERAL_TAG) { */
/*         allLiteralTags = 0; */
/*         break; */
/*       } */
/*     } */

/*     if (allLiteralTags) { */
/*       UInt freshTag = grab_fresh_tag(); */
/*       //      VG_(printf)("val_uf_union_tags_in_range(%u, %u) saw LITERAL_TAG & created tag %u\n", */
/*       //                  a, len, freshTag); */
/*       set_tag_for_range(a, len, freshTag); */
/*       return freshTag; // Ok, we're done now */
/*     } */
/*   } */

  // Scan the range for the first non-zero tag and use
  // that as the basis for all the mergings:
  // (Hopefully this should only take one iteration
  //  because the tag of address 'a' should be non-zero)
  for (curAddr = a; curAddr < (a + len); curAddr++) {
    curTag = get_tag(curAddr);
    if (curTag) {
      tagToMerge = curTag;
      break;
    }
  }

  // If they are all zeroes, then we're done;
  // Don't merge anything
  if (0 == tagToMerge) {
    return 0;
  }
  // Otherwise, merge all the stuff and set them to canonical:
  else {
    for (curAddr = a; curAddr < (a + len); curAddr++) {
      curTag = get_tag(curAddr);
      if (tagToMerge != curTag) {
        val_uf_tag_union(tagToMerge, curTag);
      }
    }

    // Find out the canonical tag
    canonicalTag = val_uf_find_leader(tagToMerge);

    // Set all the tags in this range to the canonical tag
    for (curAddr = a; curAddr < (a + len); curAddr++) {
      set_tag(curAddr, canonicalTag);
    }

    return canonicalTag;
  }
}

// Create a new tag but don't put it anywhere in memory ... just return it
// This is to handle literals in the code.  If somebody actually wants
// to use this literal, then it will get assigned somewhere ... otherwise
// there is no record of it anywhere in memory so that it can get
// garbage-collected.
VGA_REGPARM(0)
UInt MC_(helperc_CREATE_TAG) () {
  UInt newTag = grab_fresh_tag();

/*   if (within_main_program) { */
/*     DYNCOMP_DPRINTF("helperc_CREATE_TAG() = %u [nextTag=%u]\n", */
/*                     newTag, nextTag); */
/*   } */

  return newTag;
}


VGA_REGPARM(1)
UInt MC_(helperc_LOAD_TAG_8) ( Addr a ) {
  return val_uf_union_tags_in_range(a, 8);
}

VGA_REGPARM(1)
UInt MC_(helperc_LOAD_TAG_4) ( Addr a ) {
  return val_uf_union_tags_in_range(a, 4);
}

VGA_REGPARM(1)
UInt MC_(helperc_LOAD_TAG_2) ( Addr a ) {
  return val_uf_union_tags_in_range(a, 2);
}

VGA_REGPARM(1)
UInt MC_(helperc_LOAD_TAG_1) ( Addr a ) {
  return get_tag(a);
}

// Merge tags during any binary operation which
// qualifies as an interaction and returns the leader
// of the merged set
VGA_REGPARM(2)
UInt MC_(helperc_MERGE_TAGS) ( UInt tag1, UInt tag2 ) {

/*   if (within_main_program) { */
/*     DYNCOMP_DPRINTF("helperc_MERGE_TAGS(%u, %u)\n", */
/*                     tag1, tag2); */
/*   } */

  // Important special case - if one of the tags is 0, then
  // simply return the OTHER tag and don't do any merging.
  if IS_ZERO_TAG(tag1) {
    return tag2;
  }
  else if IS_ZERO_TAG(tag2) {
    return tag1;
  }
  // If either tag was retrieved from ESP,
  // (it has the special reserved value of ESP_TAG)
  // then do not perform a merge and simply return a 0 tag.
  // This will mean that local stack addresses created by
  // the &-operator will each have unique tags because they
  // assemble into code which take a constant offset from ESP.
  else if ((tag1 == ESP_TAG) ||
           (tag2 == ESP_TAG)) {
    return 0;
  }
  // If either tag is LITERAL_TAG, return the other one.
  // (If both are LITERAL_TAG's, then return LITERAL_TAG,
  //  but that's correctly handled)
  else if (LITERAL_TAG == tag1) {
    return tag2;
  }
  else if (LITERAL_TAG == tag2) {
    return tag1;
  }
  else {
    return val_uf_tag_union(tag1, tag2);
  }
}


// Merge tags but return a value of 0.  This simulate interaction of
// the two parameters but not passing along the tag to the result (the
// intended behavior for comparisons, for example).
VGA_REGPARM(2)
UInt MC_(helperc_MERGE_TAGS_RETURN_0) ( UInt tag1, UInt tag2 ) {
  if (IS_ZERO_TAG(tag1) ||
      IS_ZERO_TAG(tag2) ||
      (tag1 == ESP_TAG) ||
      (tag2 == ESP_TAG)) {
    return 0;
  }
  else {
    val_uf_tag_union(tag1, tag2);
    return 0;
  }
}


// Clear all tags for all bytes in range [a, a + len)
// TODO: We need to do something with their corresponding
// uf_objects in order to prepare them for garbage collection
// (when it's implemented)
__inline__ void clear_all_tags_in_range( Addr a, SizeT len ) {
  Addr curAddr;

/*   if (within_main_program) { */
/*     DYNCOMP_DPRINTF("clear_all_tags_in_range(a=0x%x, len=%d)\n", */
/*                     a, len); */
/*   } */

  for (curAddr = a; curAddr < (a+len); curAddr++) {
    // TODO: Do something else with uf_objects (maybe put them on a to-be-freed
    // list) to prepare them for garbage collection

    // Set the tag to 0
    set_tag(curAddr, 0);
  }
}


/*------------------------------------------------------------------*/
/*--- Linked-lists of tags for garbage collection                ---*/
/*------------------------------------------------------------------*/

// (This is currently not used right now)

// Adds a new tag to the tail of the list
// Pre: (tag != 0)
void enqueue_tag(TagList* listPtr, UInt tag) {
  tl_assert(tag);

  //  VG_(printf)("enqueue_tag ... numElts = %u\n", listPtr->numElts);

  // Special case for no elements
  if (listPtr->numElts == 0) {
    listPtr->first = listPtr->last =
      (TagNode*)VG_(calloc)(1, sizeof(TagNode));
  }
  else {
    listPtr->last->next = (TagNode*)VG_(calloc)(1, sizeof(TagNode));
    listPtr->last = listPtr->last->next;
  }

  listPtr->last->tag = tag;
  listPtr->numElts++;
}

// Removes and returns tag from head of the list
// Pre: listPtr->numElts > 0
UInt dequeue_tag(TagList* listPtr) {
  UInt retTag = 0;
  TagNode* nodeToKill;

  tl_assert(listPtr->numElts > 0);

  nodeToKill = listPtr->first;

  retTag = listPtr->first->tag;

  listPtr->first = listPtr->first->next;
  VG_(free)(nodeToKill);
  listPtr->numElts--;

  // Special case for no elements
  if (listPtr->numElts == 0) {
    listPtr->last = listPtr->first = NULL;
  }

  return retTag;
}

// Returns 1 if the tag is found in the list, 0 otherwise
// Only searches through the first n elts in *listPtr
// Pre: (tag != 0)
char is_tag_in_list(TagList* listPtr, UInt tag, UInt n) {
  UInt count = 0;

  tl_assert(tag);

  //  VG_(printf)("is_tag_in_list ... numElts = %u\n", listPtr->numElts);

  if (listPtr->numElts == 0) {
    return 0;
  }
  else {
    TagNode* i;
    for (i = listPtr->first;
         (i != NULL) && (count < n);
         i = i->next, count++) {
      if (i->tag == tag) {
        return 1;
      }
    }

    return 0;
  }
}

void clear_list(TagList* listPtr) {
  if (listPtr->numElts > 0) {
    TagNode* i = listPtr->first;
    TagNode* next = i->next;
    for (i = listPtr->first; i != NULL; i = next) {
      next = i->next;
      VG_(free)(i);
      listPtr->numElts--;
    }
  }

  tl_assert(listPtr->numElts == 0);
}