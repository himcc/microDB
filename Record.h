#ifndef RECORD_H
#define RECORD_H

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include<iostream>
#include "Defs.h"
#include "ParseTree.h"
#include "Schema.h"
#include "File.h"
#include "Comparison.h"
#include "ComparisonEngine.h"



// Basic record data structure. Data is actually stored in "bits" field. The layout of bits is as follows:
//  1) First sizeof(int) bytes: length of the record in bytes
//  2) Next sizeof(int) bytes: byte offset to the start of the first att
//  3) Next sizeof(int) bytes: byte offset to the start of the second att
//  4) Next sizeof(int) bytes: byte offset to the start of the third att
//  ....
//  n) Byte offset to the start of the att in position numAtts
//  n+1) Bits encoding the record's data. This is the location pointed to
//       by 2) above
class Record {

friend class ComparisonEngine;
friend class Page;

private:
  char* GetBits ();
  void SetBits (char *bits);
  void CopyBits(char *bits, int b_len);

public:
  char *bits;
  Record ();
  ~Record();

  // suck the contents of the record fromMe into this; note that after
  // this call, fromMe will no longer have anything inside of it
  void Consume (Record *fromMe);

  // make a copy of the record fromMe; note that this is far more
  // expensive (requiring a bit-by-bit copy) than Consume, which is
  // only a pointer operation
  void Copy (Record *copyMe);

  // reads the next record from a pointer to a text file; also requires
  // that the schema be given; returns a 0 if there is no data left or
  // if there is an error and returns a 1 otherwise
  int SuckNextRecord (Schema *mySchema, FILE *textFile);

  int ComposeRecord (Schema *mySchema, const char *src);

  // this projects away various attributes...
  // the array attsToKeep should be sorted, and lists all of the attributes
  // that should still be in the record after Project is called.  numAttsNow
  // tells how many attributes are currently in the record
  void Project (int *attsToKeep, int numAttsToKeep, int numAttsNow);

  // takes two input records and creates a new record by concatenating them;
  // this is useful for a join operation
  // attsToKeep[] = {0, 1, 2, 0, 2, 4} --gets 0,1,2 records from left 0, 2, 4 recs from right and startOfRight=3
  // startOfRight is the index position in attsToKeep for the first att from right rec
  void MergeRecords (Record *left, Record *right, int numAttsLeft,
    int numAttsRight, int *attsToKeep, int numAttsToKeep, int startOfRight);

  // prints the contents of the record; this requires
  // that the schema also be given so that the record can be interpreted
  void Print (Schema *mySchema);

  // Prints the contents of the record to an output file.
  // Added in the final demo. Called from setOutput() in main.cc
  void Print (Schema *mySchema, ostream& out);

  // prints the contents of the record to a File; this requires
  // that the schema also be given so that the record can be interpreted
  // added in assignment 3 so it can be called from RelationalOp.WriteOut
  void PrintToFile (FILE* myFile, Schema *mySchema);

  // returns the number of attributes in the record. added in assignment 3
  // because Join in RelOp.cc needs to know the number of attributes in the
  // records from the two relations so that it can use MergeRecords to combine
  // the records that satisfy the join.
  int GetNumAtts();
};

#endif
