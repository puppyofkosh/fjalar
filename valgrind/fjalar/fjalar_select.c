/*
   This file is part of Fjalar, a dynamic analysis framework for C/C++
   programs.

   Copyright (C) 2004-2006 Philip Guo (pgbovine@alum.mit.edu),
   MIT CSAIL Program Analysis Group

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
*/


/* fjalar_select.c:

Implements selective tracing of particular program points and
variables.

*/

#include "my_libc.h"

#include "fjalar_main.h"
#include "fjalar_select.h"
#include "generate_fjalar_entries.h"
#include "fjalar_traversal.h"

// Output file pointer for dumping program point names
FILE* prog_pt_dump_fp = 0;
// Output file pointer for dumping variable names
FILE* var_dump_fp = 0;

// Input file pointer for list of program points to trace
FILE* trace_prog_pts_input_fp = 0;
// Input file pointer for list of variables to trace
FILE* trace_vars_input_fp = 0;

const char COMMENT_CHAR = '#';
const char* ENTRY_DELIMETER = "----SECTION----";
const char* GLOBAL_STRING = "globals";

// Temporary scratch buffer for reading lines in from files
//  TODO: This is crude and unsafe but works for now
char input_line[200];

// GNU binary tree (built into search.h) (access using tsearch and
// tfind) which holds the Fjalar names of the program points which we
// are interested in tracing.
char* prog_pts_tree = NULL;

// GNU binary tree that holds names of functions and trees of variables
// to trace within those functions (packed into a FunctionTree struct)
FunctionTree* vars_tree = NULL;

// Special entry for global variables
FunctionTree* globalFunctionTree = 0;

// TODO: Warning! We never free the memory used by prog_pts_tree and vars_tree
// but don't worry about it for now

// Iterate through each line of the file trace_prog_pts_input_fp and
// VG_(strdup) each string into a new entry of prog_pts_tree Every
// line in a ppt program point file must be the Fjalar name of a
// function.
//
// e.g.:   FunctionNamesTest.c.staticFoo()
//
// A list of program point names can be generated by running Fjalar
// with the --dump-ppt-file=<string> command-line option
//
// (Comments are allowed - ignore all lines starting with COMMENT_CHAR)
//
// Example file (with some lines commented-out):
/*
..main()
Stack.cpp.Stack::getNumStacksCreated()
Stack.cpp.Stack::Link::initialize(char*, Stack::Link*)
#Stack.cpp.Stack()
#Stack.cpp.~Stack()
Stack.cpp.Stack::getName()
Stack.cpp.Stack::privateStuff()
#Stack.cpp.Stack::push(char*)
#Stack.cpp.Stack::peek()
Stack.cpp.Stack::pop()
*/
void initializeProgramPointsTree()
{
  while (fgets(input_line, 200, trace_prog_pts_input_fp))
    {
      char *newString;
      int lineLen = VG_(strlen)(input_line);

      // Skip blank lines (those consisting of solely the newline character)
      // Also skip comment lines (those beginning with COMMENT_CHAR)
      if(('\n' == input_line[0]) ||
         (COMMENT_CHAR == input_line[0])) {
        //        VG_(printf)("skipping blank line ...\n");
        continue;
      }

      // Strip '\n' off the end of the line
      // NOTE: Only do this if the end of the line is a newline character.
      // If the very last line of a file is not followed by a newline character,
      // then blindly stripping off the last character will truncate the actual
      // string, which is undesirable.
      if (input_line[lineLen - 1] == '\n') {
        input_line[lineLen - 1] = '\0';
      }

      newString = VG_(strdup)(input_line);

      // That is the Fjalar name of the function so grab it
      tsearch((void*)newString, (void**)&prog_pts_tree, compareStrings);
    }

  fclose(trace_prog_pts_input_fp);
  trace_prog_pts_input_fp = 0;
}


// Iterate through each line of the file trace_vars_input_fp
// and insert the line below ENTRY_DELIMETER into vars_tree as
// a new FunctionTree.  Then iterate through all variables within that function
// and add them to a tree of strings in FunctionTree.variable_tree
// Close the file when you're done
//
// (Comments are allowed - ignore all lines starting with COMMENT_CHAR)
//
/* This is an example of a variables output format:

----SECTION----
globals
StaticArraysTest_c/staticStrings
StaticArraysTest_c/staticStrings[]
StaticArraysTest_c/staticShorts
StaticArraysTest_c/staticShorts[]

----SECTION----
..f()
arg
strings
strings[]
return


----SECTION----
..b()
oneShort
manyShorts
manyShorts[]
return

----SECTION----
..main()
return

*/
void initializeVarsTree()
{
  char nextLineIsFunction = 0;
  FunctionTree* currentFunctionTree = 0;
  while (fgets(input_line, 200, trace_vars_input_fp))
    {
      int lineLen = VG_(strlen)(input_line);

      // Skip blank lines (those consisting of solely the newline character)
      // Also skip comment lines (those beginning with COMMENT_CHAR)
      if(('\n' == input_line[0]) ||
         (COMMENT_CHAR == input_line[0])) {
        //        VG_(printf)("skipping blank line ...\n");
        continue;
      }

      // Strip '\n' off the end of the line
      // NOTE: Only do this if the end of the line is a newline character.
      // If the very last line of a file is not followed by a newline character,
      // then blindly stripping off the last character will truncate the actual
      // string, which is undesirable.
      if (input_line[lineLen - 1] == '\n') {
        input_line[lineLen - 1] = '\0';
      }

      if (VG_STREQ(input_line, ENTRY_DELIMETER))
	{
	  nextLineIsFunction = 1;
	}
      else
	{
	  // Create a new FunctionTree and insert it into vars_tree
	  if (nextLineIsFunction)
	    {
	      currentFunctionTree = VG_(malloc)(sizeof(*currentFunctionTree));
	      currentFunctionTree->function_fjalar_name = VG_(strdup)(input_line);
	      currentFunctionTree->function_variables_tree = NULL; // Remember to initialize to null!

	      tsearch((void*)currentFunctionTree, (void**)&vars_tree, compareFunctionTrees);

	      // Keep a special pointer for global variables to trace
              if (VG_STREQ(input_line, GLOBAL_STRING))
		{
		  globalFunctionTree = currentFunctionTree;
                  //                  VG_(printf)("globalFunctionTree: %p\n", globalFunctionTree);
		}
              else {
                //                VG_(printf)("Function: %s\n", currentFunctionTree->function_fjalar_name);
              }
	    }
	  // Otherwise, create a new variable and stuff it into
	  // the function_variables_tree of the current function_tree
	  else
	    {
	      char* newString = VG_(strdup)(input_line);
	      tsearch((void*)newString, (void**)&(currentFunctionTree->function_variables_tree), compareStrings);
              //              VG_(printf)("variable: %s\n", newString);
	    }

	  nextLineIsFunction = 0;
	}
    }

  fclose(trace_vars_input_fp);
  trace_vars_input_fp = 0;
}


// Returns 1 if the proper function name of cur_entry is found in
// prog_pts_tree and 0 otherwise.  Always look for cur_entry->fjalar_name.
Bool prog_pts_tree_entry_found(FunctionEntry* cur_entry) {

  if (tfind((void*)cur_entry->fjalar_name,
            (void**)&prog_pts_tree,
            compareStrings)) {
    return 1;
  }
  else {
    return 0;
  }
}

// Compares the function's fjalar names names
int compareFunctionTrees(const void *a, const void *b)
{
  return VG_(strcmp)(((FunctionTree*) a)->function_fjalar_name,
		     ((FunctionTree*) b)->function_fjalar_name);
}

int compareStrings(const void *a, const void *b)
{
  return VG_(strcmp)((char*)a, (char*)b);
}

// Set this appropriately before using printVarNameAction:
FILE* g_open_fp = 0;


// All this action does is print out the name of a variable to
// g_open_fp:
static TraversalResult
printVarNameAction(VariableEntry* var,
		   char* varName,
		   VariableOrigin varOrigin,
		   UInt numDereferences,
		   UInt layersBeforeBase,
		   Bool overrideIsInit,
		   DisambigOverride disambigOverride,
		   Bool isSequence,
		   Addr pValue,
		   Addr* pValueArray,
		   UInt numElts,
		   FunctionEntry* varFuncInfo,
		   Bool isEnter) {
  (void)var; (void)varOrigin; (void)numDereferences;
  (void)layersBeforeBase; (void)overrideIsInit; (void)disambigOverride;
  (void)isSequence; (void)pValue; (void)pValueArray; (void)numElts;
  (void)varFuncInfo; (void)isEnter; /* silence unused variable warnings */

  fprintf(g_open_fp, "%s\n", varName);
  return DISREGARD_PTR_DEREFS;
}


// Output a list of program points to the file specified by
// prog_pt_dump_fp.  The format is a list of program point names.
// Always use the fjalar_name of the function.
/*
..main()
Stack.cpp.Stack::getNumStacksCreated()
Stack.cpp.Stack::Link::initialize(char*, Stack::Link*)
Stack.cpp.Stack()
Stack.cpp.~Stack()
Stack.cpp.Stack::getName()g
Stack.cpp.Stack::privateStuff()
Stack.cpp.Stack::push(char*)
Stack.cpp.Stack::peek()
Stack.cpp.Stack::pop()
*/
void outputProgramPointsToFile() {
  FuncIterator* funcIt = newFuncIterator();
  // Print out all program points in FunctionTable
  while (hasNextFunc(funcIt)) {
    FunctionEntry* cur_entry = nextFunc(funcIt);

    tl_assert(cur_entry);

    fputs(cur_entry->fjalar_name, prog_pt_dump_fp);
    fputs("\n", prog_pt_dump_fp);
  }
  deleteFuncIterator(funcIt);
}

// Output a list of variable names to the file specified by
// var_dump_fp.  Example file:
/*
----SECTION----
globals

----SECTION----
..main()
return

----SECTION----
Stack.cpp.Stack::getNumStacksCreated()
return

----SECTION----
Stack.cpp.Stack::Link::initialize(char*, Stack::Link*)
this
this->data
this->data[]
this->next
this->next[].data
this->next[].data[0]
*/
void outputVariableNamesToFile() {
  FuncIterator* funcIt;
  g_open_fp = var_dump_fp;

  // Print out a section for all global variables:
  fputs(ENTRY_DELIMETER, var_dump_fp);
  fputs("\n", var_dump_fp);
  fputs(GLOBAL_STRING, var_dump_fp);
  fputs("\n", var_dump_fp);

  visitVariableGroup(GLOBAL_VAR,
                     0,
                     0,
                     0,
                     &printVarNameAction);

  fputs("\n", var_dump_fp);

  funcIt = newFuncIterator();

  // Print out a section for all relevant functions:
  while (hasNextFunc(funcIt)) {
    FunctionEntry* cur_entry = nextFunc(funcIt);

    tl_assert(cur_entry);

    // Only dump variable entries for program points that are listed
    // in prog-pts-file, if we are using the --prog-pts-file option:
    if (!fjalar_trace_prog_pts_filename ||
        // If fjalar_trace_prog_pts_filename is on (we are reading in
        // a ppt list file), then DO NOT OUTPUT entries for program
        // points that we are not interested in.
        prog_pts_tree_entry_found(cur_entry)) {
      fputs(ENTRY_DELIMETER, var_dump_fp);
      fputs("\n", var_dump_fp);
      fputs(cur_entry->fjalar_name, var_dump_fp);
      fputs("\n", var_dump_fp);

      // Print out all function parameter return value variable names:
      visitVariableGroup(FUNCTION_FORMAL_PARAM,
                         cur_entry,
                         0,
                         0,
                         &printVarNameAction);

      visitVariableGroup(FUNCTION_RETURN_VAR,
                         cur_entry,
                         0,
                         0,
                         &printVarNameAction);

      fputs("\n", var_dump_fp);
    }
  }
  deleteFuncIterator(funcIt);

  g_open_fp = 0;
}
