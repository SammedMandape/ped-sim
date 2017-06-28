#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <vector>
#include <random>
#include <chrono>
#include <assert.h>

// TODO! implement half-sibs
// TODO! randomly sampling founders
// TODO! update all comments before functions

using namespace std;

////////////////////////////////////////////////////////////////////////////////
// Used to store details about each simulation
struct SimDetails {
  SimDetails(char t, int nF, int nG, int *retain) {
    type = t;
    numFam = nF;
    numGen = nG;
    numSampsToRetain = retain;
  }
  // type: either 'f' for full sibs/cousins, 'h' for half sibs/cousins, or
  // 'd' for double cousins
  char type;
  int numFam;
  int numGen;
  int *numSampsToRetain;
};

////////////////////////////////////////////////////////////////////////////////
// Used to store the genetic map
struct PhysGeneticPos {
  PhysGeneticPos(int p, double m1, double m2) {
    physPos = p; mapPos[0] = m1; mapPos[1] = m2;
  }
  int physPos; double mapPos[2];
};

////////////////////////////////////////////////////////////////////////////////
// Used to store necessary details about the source and length of each segment:
//
// note: start marker is implicit
struct Segment { int foundHapNum, endPos; };
typedef vector<Segment> Haplotype;
struct Person {
  Person() { sex = 0; } // by default assume using sex-averaged map: all 0
  int sex;
  vector<Haplotype> haps[2]; // haplotype pair for <this>
};

////////////////////////////////////////////////////////////////////////////////
// Function decls
void readDat(vector<SimDetails> &simDetails, char *datFile);
void readMap(vector< pair<char*, vector<PhysGeneticPos>* > > &geneticMap,
	     char *mapFile, bool &sexSpecificMaps);
int simulate(vector <SimDetails> &simDetails, Person *****&theSamples,
	      vector< pair<char*, vector<PhysGeneticPos>* > > &geneticMap,
	      bool sexSpecificMaps);
void generateHaplotype(Haplotype &toGenerate, Person &parent,
		       vector<PhysGeneticPos> *curMap);
void printBPs(vector<SimDetails> &simDetails, Person *****theSamples,
	      vector< pair<char*, vector<PhysGeneticPos>* > > &geneticMap,
	      char *outFile);
void makeVCF(vector<SimDetails> &simDetails, Person *****theSamples,
	     int totalFounderHaps, char *inVCFfile, char *outVCFfile,
	     vector< pair<char*, vector<PhysGeneticPos>* > > &geneticMap);
template<typename T>
void pop_front(vector<T> &vec);
void printUsage(char **argv);

mt19937 randomGen;
uniform_int_distribution<int> coinFlip(0,1);
exponential_distribution<double> crossoverDist(1.0 / 100); // in cM units

////////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv) {
  if (argc != 6) {
    printUsage(argv);
  }

  // seed random number generator:
  unsigned int seed = random_device().entropy();
  if (seed == 0 && random_device().entropy() == 0) {
    // random_device is not a real random number generator: fall back on
    // using time to generate a seed:
    seed = chrono::system_clock::now().time_since_epoch().count();
  }
//  seed = 695558636u; // for testing
  printf("Using random seed: %u\n\n", seed);
  randomGen.seed(seed);

  vector<SimDetails> simDetails;
  readDat(simDetails, /*datFile=*/ argv[1]);

  vector< pair<char*, vector<PhysGeneticPos>* > > geneticMap;
  bool sexSpecificMaps;
  readMap(geneticMap, /*mapFile=*/ argv[2], sexSpecificMaps);

  // The first index is the pedigree number corresponding to the description of
  // the pedigree to be simulated in the dat file
  // The second index is the family: we replicate the same pedigree structure
  // some number of times as specified in the dat file
  // The third index is the side of the pedigree (0 or 1) -- in the second
  // generation (generation index 1 here), two full or half siblings become the
  // parents of each side
  // The fourth index is the generation number (0-based)
  // The fifth index is the individual number
  Person *****theSamples;

  printf("Simulating... ");
  fflush(stdout);
  int totalFounderHaps = simulate(simDetails, theSamples, geneticMap,
				  sexSpecificMaps);
  printf("done.\n");

  printf("Printing break points... ");
  fflush(stdout);
  printBPs(simDetails, theSamples, geneticMap, /*outFile=*/ argv[5]);
  printf("done.\n");

  printf("Generating VCF file... ");
  fflush(stdout);
  makeVCF(simDetails, theSamples, totalFounderHaps, /*inVCFfile=*/ argv[3],
	  /*outVCFfile=*/ argv[4], geneticMap);
  printf("done.\n");

  return 0;
}

// Reads in the pedigree formats from the dat file, including the type of the
// pedigree (full, half, or double) and the number of samples to produce in
// every generation
void readDat(vector<SimDetails> &simDetails, char *datFile) {
  // open dat file:
  FILE *in = fopen(datFile, "r");
  if (!in) {
    printf("ERROR: could not open dat file %s!\n", datFile);
    exit(1);
  }

  // dat file contains information about how many samples to generate / store
  // information for in some of the generations; we store this in an array
  // with length equal to the number of generations to be simulated
  int *curNumSampsToRetain = NULL;
  int curNumGen = 0;

  size_t bytesRead = 1024;
  char *buffer = (char *) malloc(bytesRead + 1);
  const char *delim = " \t\n";

  int line = 0;
  while (getline(&buffer, &bytesRead, in) >= 0) {
    line++;

    char *token, *saveptr;
    token = strtok_r(buffer, delim, &saveptr);

    if (token == NULL || token[0] == '#') {
      // blank line or comment -- skip
      continue;
    }

    if (strcmp(token, "full") == 0 || strcmp(token, "half") == 0 ||
	strcmp(token, "double") == 0) {
      // new type of pedigree
      char *type = token;
      char *numFamStr = strtok_r(NULL, delim, &saveptr);
      char *numGenStr = strtok_r(NULL, delim, &saveptr);
      if (numFamStr == NULL || numGenStr == NULL ||
				      strtok_r(NULL, delim, &saveptr) != NULL) {
	fprintf(stderr, "ERROR: line %d in dat: expect three fields for pedigree declaration:\n",
		line);
	fprintf(stderr, "       [full/half/double] [numFam] [numGen]\n");
	exit(5);
      }
      int curNumFam = atoi(numFamStr);
      curNumGen = atoi(numGenStr);
      curNumSampsToRetain = new int[curNumGen];
      for(int i = 0; i < curNumGen; i++)
	curNumSampsToRetain[i] = 0;
      simDetails.emplace_back(type[0], curNumFam, curNumGen,
			      curNumSampsToRetain);
      continue;
    }

    // line contains information about sample storage for the current pedigree
    char *genNumStr = token;
    char *numSampsStr = strtok_r(NULL, delim, &saveptr);

    if (numSampsStr == NULL || strtok_r(NULL, delim, &saveptr) != NULL) {
      printf("ERROR: improper line number %d in dat file: expected two fields\n",
	      line);
    }

    int generation = atoi(genNumStr);
    int numSamps = atoi(numSampsStr);

    if (generation < 2 || generation > curNumGen) { // TODO: document
      fprintf(stderr, "ERROR: line %d in dat: generation %d below 2 or above %d (max number\n",
	      line, generation, curNumGen);
      fprintf(stderr, "       of generations)\n");
      exit(1);
    }
    if (numSamps <= 0) {
      fprintf(stderr, "ERROR: line %d in dat: in generation %d, number of samples to simulate\n",
	      line, generation);
      fprintf(stderr, "       below 0\n");
      exit(2);
    }

    // subtract 1 because array is 0 based
    if (curNumSampsToRetain[generation - 1] != 0) {
      fprintf(stderr, "ERROR: line %d in dat: multiple entries for generation %d\n",
	      line, generation);
    }
    curNumSampsToRetain[generation - 1] = numSamps;
  }

  for(auto it = simDetails.begin(); it != simDetails.end(); it++) {
    if (it->numSampsToRetain[ it->numGen - 1 ] == 0) {
      // TODO: document
      const char *typeName;
      if (it->type == 'f')
	typeName = "full";
      else if (it->type == 'h')
	typeName = "half";
      else if (it->type == 'd')
	typeName = "double";
      else
	typeName = "error";

      fprintf(stderr, "ERROR: request to simulate '%s' type pedigree, %d families, %d generations\n",
	      typeName, it->numFam, it->numGen);
      fprintf(stderr, "       but no request print any samples from last generation (number %d)\n",
	      it->numGen);
      exit(4);
    }
  }

  if (simDetails.size() == 0) {
    fprintf(stderr, "ERROR: dat file does not contain pedigree descriptions;\n");
    fprintf(stderr, "       nothing to simulate\n");
    exit(3);
  }

  fclose(in);
}

// Read in genetic map from <mapFile> into <geneticMap>. Also determines whether
// there are male and female maps present and sets <sexSpecificMaps> to true if
// so. If only one map is present, this is assumed to be the sex-averaged map
// and in that case, sets <sexSpecificMap> to false.
void readMap(vector< pair<char*, vector<PhysGeneticPos>* > > &geneticMap,
	     char *mapFile, bool &sexSpecificMaps) {
  size_t bytesRead = 1024;
  char *buffer = (char *) malloc(bytesRead + 1);
  const char *delim = " \t\n";

  FILE *in = fopen(mapFile, "r");
  if (!in) {
    printf("ERROR: could not open map file %s!\n", mapFile);
    exit(1);
  }

  char *curChr = NULL;
  vector<PhysGeneticPos> *curMap = NULL;
  sexSpecificMaps = false; // will be updated on first pass below

  while (getline(&buffer, &bytesRead, in) >= 0) {
    char *chrom, *physPosStr, *mapPos1Str, *mapPos2Str;
    char *saveptr;

    // get all the tokens:
    chrom = strtok_r(buffer, delim, &saveptr);
    if (chrom[0] == '#')
      continue; // comment
    physPosStr = strtok_r(NULL, delim, &saveptr);
    mapPos1Str = strtok_r(NULL, delim, &saveptr);
    mapPos2Str = strtok_r(NULL, delim, &saveptr);

    if (curChr == NULL && mapPos2Str != NULL)
      sexSpecificMaps = true;

    // need a new entry in <geneticMap> for a new chrom?
    // TODO: document that maps must be in order in terms of chroms and
    // physical position
    if (curChr == NULL || strcmp(chrom, curChr) != 0) {
      curChr = (char *) malloc(strlen(chrom) + 1);
      strcpy(curChr, chrom);
      curMap = new vector<PhysGeneticPos>;
      geneticMap.emplace_back(curChr, curMap);
    }

    int physPos;
    double mapPos1, mapPos2 = 0.0;
    physPos = atoi(physPosStr);
    mapPos1 = atof(mapPos1Str);
    if (sexSpecificMaps) {
      mapPos2 = atof(mapPos2Str);
    }
    else {
      assert(mapPos2Str == NULL); // TODO: do something smarter here
    }

    curMap->emplace_back(physPos, mapPos1, mapPos2);
  }

  free(buffer);
  fclose(in);
}

// Simulate data for each specified pedigree type for the number of requested
// families. Returns the number of founder haplotypes used to produce these
// simulated samples.
int simulate(vector<SimDetails> &simDetails, Person *****&theSamples,
	     vector< pair<char*, vector<PhysGeneticPos>* > > &geneticMap,
	     bool sexSpecificMaps) {
  // Note: throughout we use 0-based values for generations though the input
  // dat file is 1-based

  int totalFounderHaps = 0;

  // Make Person objects for the top-most generation:
  Person dad, mom;
  if (sexSpecificMaps)
    mom.sex = 1;

  theSamples = new Person****[simDetails.size()];
  for(unsigned int ped = 0; ped < simDetails.size(); ped++) { // for each ped
    int numFam = simDetails[ped].numFam;
    int numGen = simDetails[ped].numGen;
    int *numSampsToRetain = simDetails[ped].numSampsToRetain;

    ////////////////////////////////////////////////////////////////////////////
    // Allocate space and make Person objects for all those we will simulate,
    // assigning sex if <sexSpecificMaps> is true
    theSamples[ped] = new Person***[numFam];
    for (int fam = 0; fam < numFam; fam++) {

      // Always exactly 2 sides (may extend this later)
      theSamples[ped][fam] = new Person**[2];
      for(int side = 0; side < 2; side++) {

	theSamples[ped][fam][side] = new Person*[numGen];

	for(int curGen = 1; curGen < numGen; curGen++) {
	  // Determine how many samples we need data for in <curGen>:
	  int numPersons = numSampsToRetain[curGen];
	  if (numPersons == 0) // not saving, but need parent of next generation
	    numPersons = 1;
	  // additional person that is the other parent of next generation
	  numPersons++;

	  theSamples[ped][fam][side][curGen] = new Person[numPersons];

	  if (sexSpecificMaps) {
	    // the two individuals that reproduce are index 0 (a founder) and 1
	    // randomly decide which one to make female
	    int theFemale = coinFlip(randomGen);
	    theSamples[ped][fam][side][curGen][theFemale].sex = 1;
	  }
	}
      }
    }

    ////////////////////////////////////////////////////////////////////////////
    // Simulate all the families for the current pedigree type
    for(int fam = 0; fam < numFam; fam++) {

      for (int h = 0; h < 2; h++) { // clear out the old haplotypes for dad,mom
	dad.haps[h].clear();
	mom.haps[h].clear();
      }

      for(int side = 0; side < 2; side++) { // each side of the current family

	for(int curGen = 1; curGen < numGen; curGen++) {

	  // for each chromosome:
	  for(auto it = geneticMap.begin(); it != geneticMap.end(); it++) {
	    vector<PhysGeneticPos> *curMap = it->second;

	    Segment trivialSeg;
	    if (side == 0 && curGen == 1) {
	      // Make trivial haplotypes for generation 0 founders:
	      // no recombinations in founders
	      trivialSeg.endPos = curMap->back().physPos;
	      for(int h = 0; h < 2; h++) {
		trivialSeg.foundHapNum = totalFounderHaps++;
		// makes a copy of <trivialSeg>, can reuse
		dad.haps[h].emplace_back();
		dad.haps[h].back().push_back(trivialSeg);
	      }
	      for(int h = 0; h < 2; h++){
		trivialSeg.foundHapNum = totalFounderHaps++;
		mom.haps[h].emplace_back();
		mom.haps[h].back().push_back(trivialSeg);
	      }
	    }

	    // First make trivial haplotypes for the two founders in <curGen>;
	    // use convention that sample 0 is the founder on each side:
	    if (curGen != numGen -1) { // no founders in the last generation
	      for(int h = 0; h < 2; h++) {
		// 4 founder haplotypes per generation
		trivialSeg.foundHapNum = totalFounderHaps++;
		// the following copies <trivialSeg>, so we can reuse it
		theSamples[ped][fam][side][curGen][0].haps[h].emplace_back();
		theSamples[ped][fam][side][curGen][0].haps[h].back().push_back(
								    trivialSeg);
	      }
	    }

	    int numPersons = numSampsToRetain[curGen];
	    if (numPersons == 0)
	      // not saving any, but need parent of next generation
	      numPersons = 1;
	    // additional person that is the other parent of next generation
	    numPersons++;

	    // Now simulate the non-founders in <curGen>
	    for(int ind = 1; ind < numPersons; ind++) {
	      if (curGen == 1) {
		// no "sides" for creating generation 1; use <dad> and <mom>
		theSamples[ped][fam][side][curGen][ind].haps[0].emplace_back();
		theSamples[ped][fam][side][curGen][ind].haps[1].emplace_back();
		generateHaplotype(
			theSamples[ped][fam][side][curGen][ind].haps[0].back(),
			dad, curMap);
		generateHaplotype(
			theSamples[ped][fam][side][curGen][ind].haps[1].back(),
			mom, curMap);
	      }
	      else {
		if (sexSpecificMaps) {
		  assert(theSamples[ped][fam][side][curGen-1][0].sex !=
				  theSamples[ped][fam][side][curGen-1][1].sex);
		}
		for(int parIdx = 0; parIdx < 2; parIdx++) {
		  int hapIdx = parIdx;
		  if (sexSpecificMaps)
		    hapIdx = theSamples[ped][fam][side][curGen-1][parIdx].sex;

		  theSamples[ped][fam][side][curGen][ind].haps[hapIdx].
								 emplace_back();
		  generateHaplotype(
		    theSamples[ped][fam][side][curGen][ind].haps[hapIdx].back(),
		    /*parent=*/ theSamples[ped][fam][side][curGen-1][parIdx],
		    curMap);
		}
	      }
	    } // <ind>
	  } // <geneticMap> (chroms)
	} // <curGen>

      } // <side>
    } // <fam>

  } // <ped>

  return totalFounderHaps;
}

// Simulate one haplotype <toGenerate> by sampling crossovers and switching
// between the two haplotypes stored in <parent>. Uses the genetic map stored
// in <curMap> which is expected to correspond to the chromosome being
// simulated and may contain either one sex-averaged map or a male and female
// map.
void generateHaplotype(Haplotype &toGenerate, Person &parent,
		       vector<PhysGeneticPos> *curMap) {
  // For the two haplotypes in <parent>, which index is the current
  // <switchMarker> position contained in?
  unsigned int curIndex[2] = { 0, 0 };

  int mapIdx = parent.sex; // either 0 or 1 for sex-averaged/male or female

  // Pick haplotype for the beginning of the transmitted one:
  int curHap = coinFlip(randomGen);
  // centiMorgan position of next crossover -- exponentially random distance
  // from first physical position in the map
  double cMPosNextCO = curMap->front().mapPos[mapIdx] +
						      crossoverDist(randomGen);
  // initially assume we'll recombine between first and positions with map info
  int switchIdx = 1;

  int mapSize = curMap->size();
  double lastcMPos = curMap->back().mapPos[mapIdx];

  while (cMPosNextCO < lastcMPos) {
    // TODO: slow linear search for switching index, but probably fast enough
    for( ; switchIdx < mapSize; switchIdx++) {
      if ((*curMap)[switchIdx].mapPos[mapIdx] > cMPosNextCO)
	break;
    }
    if (switchIdx == mapSize)
      break; // let code below this while loop insert the final segments

    // segment ends between map indexes 1 less than the current <switchIdx>, so:
    switchIdx--;

    // get physical position using linear interpolation:
    double frac = (cMPosNextCO - (*curMap)[switchIdx].mapPos[mapIdx]) /
	      ((*curMap)[switchIdx+1].mapPos[mapIdx] -
					   (*curMap)[switchIdx].mapPos[mapIdx]);
    assert(frac >= 0.0 && frac <= 1.0);
    int switchPos = (*curMap)[switchIdx].physPos +
	frac * ((*curMap)[switchIdx+1].physPos - (*curMap)[switchIdx].physPos);

    // copy Segments from <curHap>
    // TODO: comment about back()
    for( ; curIndex[curHap] < parent.haps[curHap].back().size();
							  curIndex[curHap]++) {
      Segment &seg = parent.haps[curHap].back()[ curIndex[curHap] ];
      if (seg.endPos >= switchPos) {
	// last segment to copy, and we will break it at <switchPos>
	Segment copy = seg; // don't modify <seg.endPos> directly
	copy.endPos = switchPos;
	toGenerate.push_back(copy);
	if (seg.endPos == switchPos)
	  curIndex[curHap]++;
	break; // done copying
      }
      else {
	toGenerate.push_back(seg);
      }
    }
    assert(curIndex[curHap] < parent.haps[curHap].back().size());

    // swap haplotypes
    curHap ^= 1;
    // must update <curIndex[curHap]>
    for( ; curIndex[curHap] < parent.haps[curHap].back().size();
							  curIndex[curHap]++) {
      Segment &seg = parent.haps[curHap].back()[ curIndex[curHap] ];
      if (seg.endPos > switchPos)
	// current segment spans from just after <switchMarker> to <endMarker>
	break;
    }
    assert(curIndex[curHap] < parent.haps[curHap].back().size());

    cMPosNextCO += crossoverDist(randomGen);

    // TODO: implement? document if so
    // find location of next crossover; we use this loop to ensure that the next
    // <switchPos> value is strictly greater than the current one:
    // (Presumably this will get triggered next to never, but just in case. Note
    // that this changes the distribution of the crossover locations, but since
    // there's CO interference in real data, this seems fine.)
//    do {
//      cMPosNextCO += crossoverDist(randomGen);
//    } while (cMPosNextCO < curMap[switchIdx].mapPos[mapIdx]);
  }

  // copy through to the end of the chromosome:
  for( ; curIndex[curHap] < parent.haps[curHap].back().size();
							  curIndex[curHap]++) {
    Segment &seg = parent.haps[curHap].back()[ curIndex[curHap] ];
    toGenerate.push_back(seg);
  }
}

// Print the break points to <outFile>
void printBPs(vector<SimDetails> &simDetails, Person *****theSamples,
	      vector< pair<char*, vector<PhysGeneticPos>* > > &geneticMap,
	      char *outFile) {
  FILE *out = fopen(outFile, "w");
  if (!out) {
    printf("ERROR: could not open output file %s!\n", outFile);
    exit(1);
  }

  for(unsigned int ped = 0; ped < simDetails.size(); ped++) {
    char pedType = simDetails[ped].type;
    int numFam = simDetails[ped].numFam;
    int numGen = simDetails[ped].numGen;
    int *numSampsToRetain = simDetails[ped].numSampsToRetain;

    assert(numSampsToRetain[0] == 0);
    for(int fam = 0; fam < numFam; fam++) {
      for(int side = 0; side < 2; side++) {
	for(int gen = 1; gen < numGen; gen++) {
	  if (numSampsToRetain[gen] > 0) {
	    for(int ind = 1; ind < numSampsToRetain[gen] + 1; ind++) {
	      for(int h = 0; h < 2; h++) {
		fprintf(out, "%c%d_f%d_s%d_g%d_i%d h%d", pedType, ped+1, fam+1,
			side, gen+1, ind, h);

		for(unsigned int chr = 0; chr < geneticMap.size(); chr++) {
		  // print chrom name and starting position
		  fprintf(out, " %s|%d", geneticMap[chr].first,
			  geneticMap[chr].second->front().physPos);
		  Haplotype &curHap = theSamples[ped][fam][side][gen][ind].
								   haps[h][chr];
		  for(unsigned int s = 0; s < curHap.size(); s++) {
		    Segment &seg = curHap[s];
		    fprintf(out, " %d:%d", seg.foundHapNum, seg.endPos);
		  }
		}
		fprintf(out, "\n");
	      }
	    }
	  }
	}
      }
    }
  }

  fclose(out);
}

// Given the simulated break points for individuals in each pedigree/family
// stored in <theSamples> and other necessary information, reads input VCF
// format data from the file named <inVCFfile> and prints the simulated
// haplotypes for each sample to <outVCFfile> in VCF format.
void makeVCF(vector<SimDetails> &simDetails, Person *****theSamples,
	     int totalFounderHaps, char *inVCFfile, char *outVCFfile,
	     vector< pair<char*, vector<PhysGeneticPos>* > > &geneticMap) {
  // open input VCF file:
  FILE *in = fopen(inVCFfile, "r");
  if (!in) {
    printf("ERROR: could not open input VCF file %s!\n", inVCFfile);
    exit(1);
  }

  // open output VCF file:
  FILE *out = fopen(outVCFfile, "w");
  if (!out) {
    printf("ERROR: could not open output VCF file %s!\n", outVCFfile);
    exit(1);
  }

  size_t bytesRead = 1024;
  char *buffer = (char *) malloc(bytesRead + 1);
  const char *tab = "\t";
  const char *bar = "|";
  // Below when we print the VCF output, we alternate printing tab
  // and | between successive alleles. Make this simpler with:
  const char *betweenAlleles[2] = { tab, bar };

  int *founderHaps = new int[totalFounderHaps];

  // iterate over chromosomes in the genetic map
  unsigned int chrIdx = 0; // index of current chromosome number;
  char *chrName = geneticMap[chrIdx].first;
  int chrBegin = geneticMap[chrIdx].second->front().physPos;
  int chrEnd = geneticMap[chrIdx].second->back().physPos;

  bool gotSomeData = false;

  while (getline(&buffer, &bytesRead, in) >= 0) { // read each line of input VCF
    if (buffer[0] == '#' && buffer[1] == '#') {
      // header line: print to output
      fprintf(out, "%s", buffer);
      continue;
    }

    if (buffer[0] == '#') {
      // header line indicating fields and sample ids -- print version for the
      // simulated individuals
      fprintf(out, "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT");

      // sample ids:
      for(unsigned int ped = 0; ped < simDetails.size(); ped++) {
	int numFam = simDetails[ped].numFam;
	int numGen = simDetails[ped].numGen;
	int *numSampsToRetain = simDetails[ped].numSampsToRetain;

	for(int fam = 0; fam < numFam; fam++)
	  for(int side = 0; side < 2; side++)
	    for(int gen = 1; gen < numGen; gen++)
	      if (numSampsToRetain[gen] > 0)
		for(int ind = 1; ind < numSampsToRetain[gen] + 1; ind++)
		  fprintf(out, "\tp%d_f%d_s%d_g%d_i%d", ped+1, fam+1, side,
			  gen+1, ind);
      }
      fprintf(out, "\n");
      continue;
    }

    char *saveptr;
    char *chrom = strtok_r(buffer, tab, &saveptr);

    if (strcmp(chrom, chrName) != 0) {
      if (gotSomeData) {
	chrIdx++;
	if (chrIdx == geneticMap.size())
	  // no more chromosomes to process; will ignore remainder of VCF
	  break;
	chrName = geneticMap[chrIdx].first;
      }
      if (!gotSomeData || strcmp(chrom, chrName) != 0) {
	printf("ERROR: chromosome %s in VCF file either out of order or not present\n",
	       chrom);
	printf("       in genetic map\n");
	exit(5);
      }

      // update beginning / end positions for this chromosome
      chrBegin = geneticMap[chrIdx].second->front().physPos;
      chrEnd = geneticMap[chrIdx].second->back().physPos;
    }

    gotSomeData = true;

    char *posStr = strtok_r(NULL, tab, &saveptr);
    int pos = atoi(posStr);
    if (pos < chrBegin || pos > chrEnd)
      continue; // no genetic map information for this position: skip

    // read/save the ID, REF, ALT, QUAL, FILTER, INFO, and FORMAT fields:
    char *otherFields[7];
    for(int i = 0; i < 7; i++)
      otherFields[i] = strtok_r(NULL, tab, &saveptr);

    // read in/store the haplotypes
    int numRead = 0;
    char *token;
    // TODO! want to randomize
    while(numRead < totalFounderHaps &&
				      (token = strtok_r(NULL, tab, &saveptr))) {
      char *alleles[2];
      char *saveptr2;
      alleles[0] = strtok_r(token, bar, &saveptr2);
      alleles[1] = strtok_r(NULL, bar, &saveptr2);
      if (alleles[1] == NULL) {
	printf("ERROR: VCF contains data field %s, which is not phased\n",
		token);
	exit(5);
      }
      if (strtok_r(NULL, bar, &saveptr2) != NULL) {
	printf("ERROR: multiple '|' chacters in data field\n");
	exit(5);
      }

      for(int h = 0; h < 2; h++) {
	founderHaps[ numRead + h ] = atoi( alleles[h] );
      }
      numRead += 2;
    }

    if (numRead < totalFounderHaps) {
      printf("ERROR: fewer than the needed %d haplotypes found in VCF\n",
	      totalFounderHaps);
      exit(6);
    }

    // Print this line to the output file
    fprintf(out, "%s\t%s", chrom, posStr);
    for(int i = 0; i < 7; i++)
      fprintf(out, "\t%s", otherFields[i]);

    for(unsigned int ped = 0; ped < simDetails.size(); ped++) {
      int numFam = simDetails[ped].numFam;
      int numGen = simDetails[ped].numGen;
      int *numSampsToRetain = simDetails[ped].numSampsToRetain;

      for(int fam = 0; fam < numFam; fam++)
	for(int side = 0; side < 2; side++)
	  for(int gen = 1; gen < numGen; gen++)
	    if (numSampsToRetain[gen] > 0)
	      for(int ind = 1; ind < numSampsToRetain[gen] + 1; ind++) {
		for(int h = 0; h < 2; h++) {
		  Haplotype &curHap = theSamples[ped][fam][side][gen][ind].
								haps[h][chrIdx];
		  while (curHap.front().endPos < pos) {
		    pop_front(curHap);
		  }
		  assert(curHap.front().endPos >= pos);
		  int foundHapNum = curHap.front().foundHapNum;
		  fprintf(out, "%s%d", betweenAlleles[h],
			  founderHaps[foundHapNum]);
		}
	      }
    }
    fprintf(out, "\n");
  }

  free(buffer);
  fclose(out);
  fclose(in);
}

// removes the first element from <vec>. This has time that is linear in the
// number of elements. We could use a list<> instead of a vector<> to avoid this
// linear time, but having random access ability on the vectors is very
// convenient, so we'll live with this.
template<typename T>
void pop_front(vector<T> &vec) {
  vec.erase(vec.begin());
}

void printUsage(char **argv) {
  printf("Usage:\n");
  printf("  %s [in.dat] [map file] [in.vcf] [out.vcf] [out.bp]\n\n", argv[0]);
  printf("Where:\n");
//  printf("  [numFam] is an integer indicating the number of families to simulate\n");
//  printf("  [numGen] is an integer specifying the number of generations in the pedigree\n");
  printf("  [map file] contains either a sex averaged genetic map or both male and\n");
  printf("             female maps\n\n");
  printf("  The genetic map file should be formatted with three or four columns:\n\n");
  printf("    [chrom name] [physical position] [map position1 (cM)] <map position2 (cM)>\n\n");
  printf("  [chrom name] must match with the names in the VCF file of phased samples\n");
  printf("  [map position1] gives the sex-averaged map if there are only three columns\n");
  printf("                  or it gives the male map\n");
  printf("  [map position2] gives the female map (if using sex-specific maps)\n");
  exit(1);
}
