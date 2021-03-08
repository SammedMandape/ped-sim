// ped-sim: pedigree simulation tool
//
// This program is distributed under the terms of the GNU General Public License

#include <vector>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#ifndef DATASTRUCTS_H
#define DATASTRUCTS_H

using namespace std;

////////////////////////////////////////////////////////////////////////////////
// Used to store details about each simulation
struct Parent {
  // The generation and branch number of the given parent
  int gen;
  int branch;
};

struct ParentComp {
  bool operator() (const Parent &lhs, const Parent &rhs) const {
    return (lhs.gen < rhs.gen) ||
				(lhs.gen == rhs.gen && lhs.branch < rhs.branch);
  }
};

////////////////////////////////////////////////////////////////////////////////
// Used to store details about each simulation
struct SimDetails {
  SimDetails(int nFam, int nGen, int **print, int *branches, Parent **parents,
	     int **sexes, int i1FixedSex, int **spouses, char *theName) {
    numFam = nFam;
    numGen = nGen;
    numSampsToPrint = print;
    numBranches = branches;
    branchParents = parents;
    sexConstraints = sexes;
    i1Sex = i1FixedSex;
    branchNumSpouses = spouses;
    name = new char[ strlen(theName) + 1 ];
    if (name == NULL) {
      printf("ERROR: out of memory");
      exit(5);
    }
    strcpy(name, theName);
  }
  SimDetails(const SimDetails &other) {
    numFam = other.numFam;
    numGen = other.numGen;
    numSampsToPrint = other.numSampsToPrint;
    numBranches = other.numBranches;
    branchParents = other.branchParents;
    sexConstraints = other.sexConstraints;
    i1Sex = other.i1Sex;
    branchNumSpouses = other.branchNumSpouses;
    name = new char[ strlen(other.name) + 1 ];
    if (name == NULL) {
      printf("ERROR: out of memory");
      exit(5);
    }
    strcpy(name, other.name);

    founderOffset = other.founderOffset;
    numFounders = other.numFounders;
    founderIdSuffix = other.founderIdSuffix;
  }
  ~SimDetails() {
    delete [] name;
  }
  int numFam;
  int numGen;
  int **numSampsToPrint;
  int *numBranches;
  Parent **branchParents;
  int **sexConstraints;
  int i1Sex;
  int **branchNumSpouses;
  char *name;

  int founderOffset;
  int numFounders;
  vector<char *> founderIdSuffix;
};

////////////////////////////////////////////////////////////////////////////////
// Used to store necessary details about the source and length of each segment:
//
// note: start marker is implicit
struct Segment {
  Segment() { }
  Segment(int fhn, int ep) {
    foundHapNum = fhn;
    endPos = ep;
  }
  int foundHapNum, endPos;
};

typedef vector<Segment> Haplotype;
struct Person {
  Person() {
    // by default assume using sex-averaged map: all 0
    sex = 0;
    // by default assume not using fixed COs
    fixedCOidxs[0] = fixedCOidxs[1] = UINT_MAX;
  }
  int sex;
  vector<Haplotype> haps[2]; // haplotype pair for <this>
  unsigned int fixedCOidxs[2];
};

// For the <hapCarriers> structure -- stores the sample id that inherited (or
// is the founder of) a given haplotype
struct InheritRecord {
  InheritRecord() { assert(false); }
  InheritRecord(unsigned int p, int f, int g, int b, int i, int s, int e) {
    ped = p;
    fam = f;
    gen = g;
    branch = b;
    ind = i;
    startPos = s;
    endPos = e;
    assert(startPos <= endPos);
  }
  unsigned int ped;
  int fam;
  int gen;
  int branch;
  int ind;
  int startPos;
  int endPos;

  vector< pair<int, int> > hbd;
};

struct IBDRecord {
  IBDRecord() { assert(false); }
  IBDRecord(int og, int ob, int oi, int ci, int start, int end, int foundHap) {
    otherGen = og;
    otherBranch = ob;
    otherInd = oi;
    chrIdx = ci;
    startPos = start;
    endPos = end;
    foundHapNum = foundHap;

    assert(startPos <= endPos);
  }
  int otherGen;
  int otherBranch;
  int otherInd;
  int chrIdx;
  int startPos;
  int endPos;
  int foundHapNum;
};

#endif // DATASTRUCTS_H
