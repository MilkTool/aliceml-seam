//
// Author:
//   Thorsten Brunklaus <brunklaus@ps.uni-sb.de>
//
// Copyright:
//   Thorsten Brunklaus, 2000-2002
//
// Last Change:
//   $Date$ by $Author$
//   $Revision$
//
#include <cstdlib>
#include <cstring>
#include <cstdio>

#if defined(STORE_PROFILE)
#include <sys/time.h>
#include <unistd.h>
#endif

#if defined(INTERFACE)
#pragma implementation "store/StatusWord.hh"
#pragma implementation "store/HeaderOp.hh"
#pragma implementation "store/PointerOp.hh"
#pragma implementation "store/Store.hh"
#pragma implementation "store/GCHelper.hh"
#pragma implementation "store/Value.hh"
#pragma implementation "store/Set.hh"
#endif

#include "store/StatusWord.hh"
#include "store/Store.hh"
#include "store/Memory.hh"
#include "store/GCHelper.hh"

// Using Set in a anonymous namespace prevents
// class Set from appearing outside
namespace {
#include "store/Set.hh"

  Set *intgenSet = INVALID_POINTER;
  Set *wkDictSet = INVALID_POINTER;
};

// Status Word
u_int StatusWord::status;

//
// Class Fields and Global Vars
//
MemChunk *Store::roots[STORE_GENERATION_NUM];
u_int Store::memMax[STORE_GENERATION_NUM];
u_int Store::memFree;
u_int Store::memTolerance;

MemChunk *Store::curChunk;
char *Store::chunkTop;
char *Store::chunkMax;

u_int Store::hdrGen;
u_int Store::dstGen;

#if defined(STORE_PROFILE)
u_int Store::totalMem  = 0;
u_int Store::gcLiveMem = 0;
struct timeval *Store::sum_t;
#endif

//
// Method Implementations
//

// was inline before
u_int Store::GetMemUsage(MemChunk *chunk) {
  u_int size = 0;
  while (chunk != NULL) {
    size += (u_int) (chunk->GetTop() - chunk->GetBase());
    chunk = chunk->GetNext();
  }
  return size;
}

inline void Store::AllocNewMemChunk(u_int size, const u_int gen) {
  // Compute necessary MemChunk Size (requested size must fit in)
  u_int alloc_size = STORE_MEMCHUNK_SIZE;
  size += sizeof(u_int);
  if (alloc_size < size) {
    div_t d    = std::div(size, STORE_MEMCHUNK_SIZE);
    alloc_size = ((d.quot + (d.rem ? 1 : 0)) * STORE_MEMCHUNK_SIZE);
  }
  // Allocate a new Chunk
  MemChunk *chunk = new MemChunk(roots[gen], alloc_size);
  roots[gen] = chunk;
  curChunk   = chunk;
  if (GetMemUsage(roots[gen]) > memMax[gen])
    StatusWord::SetStatus(GCStatus());
}

inline char *Store::GCAlloc(u_int size, u_int header) {
  for (;;) {
    char *p      = curChunk->GetTop();
    char *newtop = p + size; 
    if (newtop >= curChunk->GetMax()) {
      AllocNewMemChunk(size, Store::dstGen);
      continue;
    }
    curChunk->SetTop(newtop);
    ((u_int *) p)[0] = header;
    return p;
  }
}

inline char *Store::GCAlloc(u_int size) {
  size = HeaderOp::TranslateSize(size);
  return Store::GCAlloc(BlockMemSize(size),
			HeaderOp::EncodeHeader(MIN_DATA_LABEL, size, hdrGen));
}

inline Block *Store::AddToFinSet(Block *p, word value) {
  if (p == INVALID_POINTER) {
    p = (Block *) Store::GCAlloc(4);
    p->InitArg(0, 1);
  }

  u_int top    = Store::DirectWordToInt(p->GetArg(0));
  u_int newtop = (top + 1);
  u_int size   = p->GetSize();
  Block *np;

  if (newtop >= size) {
    u_int newsize = ((size * 3) >> 1);

    np = (Block *) Store::GCAlloc(newsize);
    HeaderOp::EncodeHeader(MIN_DATA_LABEL, newsize, hdrGen);
    AssertStore(np != INVALID_POINTER);
    std::memcpy(np->GetBase(), p->GetBase(), (size * sizeof(u_int)));
  }
  else
    np = p;

  np->InitArg(0, newtop);
  np->InitArg(top, value);
  return np;
}

void Store::AllocNewMemChunkStd() {
  Block *p = (Block *) chunkTop;
  curChunk->SetTop(chunkTop);
  AllocNewMemChunk(BlockMemSize(HeaderOp::DecodeSize(p)), 0);
  chunkTop = curChunk->GetTop();
  chunkMax = curChunk->GetMax();
}

inline void Store::FreeMemChunks(MemChunk *chunk) {
  MemChunk *next = chunk->GetNext();
  chunk->Clear();
  chunk = next;
  while (chunk != NULL) {
    MemChunk *next = chunk->GetNext();
    delete chunk;
    chunk = next;
  }
}

inline Block *Store::CloneBlock(Block *p) {
  u_int size   = HeaderOp::DecodeSize(p);
  u_int header = HeaderOp::EncodeHeader(HeaderOp::DecodeLabel(p), size, hdrGen);
  Block *newp  = (Block *)
    Store::GCAlloc(BlockMemSize(size), header); // No size translation here
  std::memcpy(newp->GetBase(), p->GetBase(), (size * sizeof(u_int)));
  GCHelper::MarkMoved(p, newp);
  return newp;
}

inline word Store::ForwardWord(word p) {
  if (PointerOp::IsInt(p) == 0) {
    Block *sp = PointerOp::RemoveTag(p);
    // order is important because moving ptr overwrites gen assignment
    if (GCHelper::AlreadyMoved(sp)) {
      sp = GCHelper::GetForwardPtr(sp);
      p  = PointerOp::EncodeTag(sp, PointerOp::DecodeTag(p));
    }
    else if (HeaderOp::DecodeGeneration(sp) < dstGen) {
      sp = CloneBlock(sp);
      p  = PointerOp::EncodeTag(sp, PointerOp::DecodeTag(p));
    }
  }
  return p;
}

inline Block *Store::ForwardSet(Block *p) {
  if (HeaderOp::DecodeGeneration(p) < dstGen)
    return CloneBlock(p);
  else
    return p;
}

inline s_int Store::CanFinalize(Block *p) {
  BlockLabel l = p->GetLabel();
  // Value is non Dict or empty Dict ?
  return ((l != WEAK_DICT_LABEL) ||
	  ((l == WEAK_DICT_LABEL) && ((WeakDictionary *) p)->GetCounter() == 0));
}

inline void Store::CheneyScan(MemChunk *chunk, char *scan) {
  goto have_scan;
  do {
    scan = chunk->GetBase();
  have_scan:
    while (scan < chunk->GetTop()) {
      Block *p      = (Block *) scan;
      u_int cursize = HeaderOp::DecodeSize(p);
      BlockLabel l  = p->GetLabel();
      // CHUNK_LABEL and WEAK_DICT_LABEL are the largest possible labels
      if (l < CHUNK_LABEL) {
	for (u_int i = cursize; i--;) {
	  word item = p->GetArg(i);
	  item = PointerOp::Deref(item);
	  item = Store::ForwardWord(item);
	  p->InitArg(i, item);
	}
      }
      scan += BlockMemSize(cursize);
    }
    chunk = chunk->GetPrev();
  } while (chunk != NULL);
}

// to be done: more efficient solution
static int IsInFromSpace(MemChunk **roots, char *p) {
  for (u_int i = STORE_GENERATION_NUM - 1; i--;) {
    MemChunk *chunk = roots[i];
    while (chunk != NULL) {
      if ((chunk->GetBase() >= p) && (p <= chunk->GetTop()))
	return 1;
      chunk = chunk->GetNext();
    }
  }
  return 0;
}

// Finalization needs a second cheney scan. This yields the problem
// that we cannot distinguish the from and the to space for the oldest
// generation using our generational match. Instead we do explicit
// checking which is expensive; therefore use finalization with care.
//
// to be done: more efficient and better code reusing solution
inline void Store::FinalizeCheneyScan(MemChunk *chunk, char *scan) {
  goto have_scan;
  do {
    scan = chunk->GetBase();
  have_scan:
    while (scan < chunk->GetTop()) {
      Block *p      = (Block *) scan;
      u_int cursize = HeaderOp::DecodeSize(p);
      BlockLabel l  = p->GetLabel();
      // CHUNK_LABEL and WEAK_DICT_LABEL are the largest possible labels
      if (l < CHUNK_LABEL) {
	for (u_int i = cursize; i--;) {
	  word item = p->GetArg(i);
	  item = PointerOp::Deref(item);
	  if (PointerOp::IsInt(item) == 0) {
	    Block *sp = PointerOp::RemoveTag(item);
	    if (GCHelper::AlreadyMoved(sp)) {
	      sp   = GCHelper::GetForwardPtr(sp);
	      item = PointerOp::EncodeTag(sp, PointerOp::DecodeTag(item)); 
	    }
	    else if (IsInFromSpace(roots, (char *) sp)) {
	      sp   = CloneBlock(sp);
	      item = PointerOp::EncodeTag(sp, PointerOp::DecodeTag(item)); 
	    }
	  }
	  p->InitArg(i, item);
	}
      }
      scan += BlockMemSize(cursize);
    }
    chunk = chunk->GetPrev();
  } while (chunk != NULL);
}

void Store::InitStore(u_int mem_max[STORE_GENERATION_NUM],
		      u_int mem_free, u_int mem_tolerance) {
  StatusWord::Init();
  for (u_int i = STORE_GENERATION_NUM; i--;) {
    Store::roots[i]  = new MemChunk(NULL, STORE_MEMCHUNK_SIZE);
    Store::memMax[i] = mem_max[i];
  }
  Store::memFree      = mem_free;
  Store::memTolerance = mem_tolerance;
  // Prepare Memory Allocation
  curChunk = roots[0];
  chunkTop = curChunk->GetTop();
  chunkMax = curChunk->GetMax();
  // Alloc Intgen- and WKDict-Set
  intgenSet = Set::New(STORE_INTGENSET_SIZE);
  wkDictSet = Set::New(STORE_WKDICTSET_SIZE);
  // Enable BlockHashTables
  BlockHashTable::Init();
#if defined(STORE_PROFILE)
  totalMem = 0;
  sum_t    = (struct timeval *) malloc(sizeof(struct timeval));
#endif
}

void Store::CloseStore() {
  for (int i = (STORE_GENERATION_NUM - 1); i--;) {
    MemChunk *chain = roots[i];
    while (chain != NULL) {
      MemChunk *tmp = chain->GetNext();
      delete chain;
      chain = tmp;
    }
  }
}

void Store::AddToIntgenSet(Block *v) {
  HeaderOp::SetChildishFlag(v);
  intgenSet = intgenSet->Push(v->ToWord());
}

void Store::RegisterWeakDict(WeakDictionary *v) {
  wkDictSet = wkDictSet->Push(v->ToWord());
}

inline void Store::HandleInterGenerationalPointers(u_int gen) {
  Set *intgen_set = intgenSet;
#if defined(STORE_GC_DEBUG)
  std::fprintf(stderr, "initial intgen_size is %d\n", intgen_set->GetSize());
#endif
  u_int rs_size = intgen_set->GetSize();
  intgen_set->MakeEmpty();
  // Traverse intgen_set entries (to be changed soon)
  for (u_int i = 1; i <= rs_size; i++) {
    word p = PointerOp::Deref(intgen_set->GetArg(i));
    if (!PointerOp::IsInt(p)) {
      Block *curp = PointerOp::RemoveTag(p);
      // Block is no longer old but alive. It can't contain intgens any longer
      if (GCHelper::AlreadyMoved(curp))
	HeaderOp::ClearChildishFlag(GCHelper::GetForwardPtr(curp));
      else {
	u_int curgen = HeaderOp::DecodeGeneration(curp);
	// Block is still old
	if (curgen > gen) {
	  u_int hasyoungptrs = 0;
	  // Traverse intgen_set entry for young references
	  // and remove reference chains
	  for (u_int k = curp->GetSize(); k--;) {
	    word fp = PointerOp::Deref(curp->GetArg(k));
	    curp->InitArg(k, fp);
	    if (!PointerOp::IsInt(fp)) {
	      Block *curfp = PointerOp::RemoveTag(fp);
	      // found young moved ptr
	      if (GCHelper::AlreadyMoved(curfp)) {
		hasyoungptrs = 1;
		curfp = GCHelper::GetForwardPtr(curfp);
		fp = PointerOp::EncodeTag(curfp, PointerOp::DecodeTag(fp));
		curp->InitArg(k, fp);
	      }
	      // need to check ptrs age
	      else {
		u_int curfgen = HeaderOp::DecodeGeneration(curfp);
		// found young ptr to be moved
		if (curfgen <= gen) {
		  hasyoungptrs = 1;
		  curfp = CloneBlock(curfp);
		  fp = PointerOp::EncodeTag(curfp, PointerOp::DecodeTag(fp));
		  curp->InitArg(k, fp);
		}
		// found young normal ptr
		else if (curfgen < curgen)
		  hasyoungptrs = 1;
		// ptr is equal or older
	      }
	    }
	  }
	  // p contains young ptrs and remains within intgen_set
	  if (hasyoungptrs)
	    intgen_set->Push(p);
	  // p does not contain young ptrs any longer
	  else
	    HeaderOp::ClearChildishFlag(curp);
	}
	// block is garbage
      }
    }
  }
#if defined(STORE_GC_DEBUG)
  std::fprintf(stderr, "new_intgen_size is %d\n", intgen_set->GetSize());
#endif
}

inline Block *Store::HandleWeakDictionaries() {
  Set *wkdict_set = wkDictSet;
#if defined(STORE_GC_DEBUG)
  std::fprintf(stderr, "initial weakdict_size is %d\n", wkdict_set->GetSize()); 
  std::fprintf(stderr, "Handling weak dictionaries\n");
#endif
  // Allocate and initialize Finalisation Set
  Block *finset = INVALID_POINTER;

  u_int rs_size = wkdict_set->GetSize();
  Block *db_set = (Block *) Store::GCAlloc((rs_size + 1));
  wkdict_set->MakeEmpty();
  std::memcpy(db_set->GetBase(), ((Block *) wkdict_set)->GetBase(),
	      ((rs_size + 1) * sizeof(u_int)));

  // Phase One: Forward all Dictionaries but not the contents
  for (u_int i = rs_size; i >= 1; i--) {
    word dict = db_set->GetArg(i);
    Block *dp = Store::DirectWordToBlock(dict);
    word ndict;
    // Dictionary has been reached from Root Set and must kept alive
    if (GCHelper::AlreadyMoved(dp)) {
      ndict = PointerOp::EncodeTag(GCHelper::GetForwardPtr(dp),
				   PointerOp::DecodeTag(dict));
      wkdict_set->Push(ndict);
    }
    // Dictionary might be finalized
    else if (HeaderOp::DecodeGeneration(dp) < dstGen) {
      Block *newp = CloneBlock(dp);
      ndict = PointerOp::EncodeTag(newp, PointerOp::DecodeTag(dict));
      // Finalize only empty dict
      if (((WeakDictionary *) newp)->GetCounter() == 0) {
	word handler = ((WeakDictionary *) newp)->GetHandler();
	finset = Store::AddToFinSet(finset, ndict);
	finset = Store::AddToFinSet(finset, handler);
      }
      // Keep it alive (thanks to Denys for pointing that out)
      else
	wkdict_set->Push(ndict);
    }
    // Can't decide whether it was reached or not; must assume yes.
    else {
      ndict = dict;
      wkdict_set->Push(ndict);
    }
    // Keep Dict References complete for working
    db_set->InitArg(i, ndict);

    // Now Process DictTable and its HashNodes but NOT the content
    WeakDictionary *p = WeakDictionary::FromWordDirect(ndict);
    word arr          = Store::ForwardWord(p->GetTable()->ToWord());
    p->SetTable(arr);
    Block *table = Store::DirectWordToBlock(arr);
    for (u_int k = table->GetSize(); k--;) {
      word nodes = Store::ForwardWord(table->GetArg(k));
      table->InitArg(k, nodes);
      while (nodes != Store::IntToWord(0)) {
	HashNode *node = HashNode::FromWordDirect(nodes);
	nodes = Store::ForwardWord(node->GetNext());
	node->SetNextDirect(nodes);
      }
    }
  }
  // Phase Two: Check for integer or forwarded entries
  // in all dictionaries and handle them
  for (u_int i = rs_size; i >= 1; i--) {
    WeakDictionary *dict = WeakDictionary::FromWordDirect(db_set->GetArg(i));
    Block *table         = dict->GetTable();

    for (u_int k = table->GetSize(); k--;) {
      word nodes = table->GetArg(k);
      while (nodes != Store::IntToWord(0)) {
	HashNode *node = HashNode::FromWordDirect(nodes);
	word val       = PointerOp::Deref(node->GetValue());
	// Store Integers and mark node as handled
	if (PointerOp::IsInt(val)) {
	  node->SetValue(val);
	  node->MarkHandled();
	}
	// Store Forward ptr and mark node as handled; otherwise leave untouched
	else {
	  Block *valp = PointerOp::RemoveTag(val);
	  if (GCHelper::AlreadyMoved(valp)) {
	    node->SetValue(PointerOp::EncodeTag(GCHelper::GetForwardPtr(valp),
						PointerOp::DecodeTag(val)));
	    node->MarkHandled();
	  }
	}
	nodes = node->GetNext();
      }
    }
  }
  // Phase Three: Forward Dictionary Contents and record Finalize Candiates
  MemChunk *chunk = curChunk;
  char *scan      = curChunk->GetTop();
  for (u_int i = rs_size; i >= 1; i--) {
    WeakDictionary *dict = WeakDictionary::FromWordDirect(db_set->GetArg(i));
    word handler         = dict->GetHandler();
    Block *table         = dict->GetTable();
    for (u_int k = table->GetSize(); k--;) {
      word nodes = table->GetArg(k);
      word prev  = Store::IntToWord(0);
      while (nodes != Store::IntToWord(0)) {
	HashNode *node = HashNode::FromWordDirect(nodes);
	// Remove handled marks
	if (node->IsHandled())
	  node->MarkNormal();
	// This node possibly contains finalisation data
	// invariant: it is a block
	else {
	  word val    = PointerOp::Deref(node->GetValue());
	  Block *valp = PointerOp::RemoveTag(val);
	  // Value has been finalized or saved before
	  if (GCHelper::AlreadyMoved(valp)) {
	    if (Store::CanFinalize(valp))
	      dict->RemoveEntry(k, prev, node);
	    else
	      node->SetValue(PointerOp::EncodeTag(GCHelper::GetForwardPtr(valp),
						  PointerOp::DecodeTag(val)));
	  }
	  // Value might be finalized
	  else if (HeaderOp::DecodeGeneration(valp) < dstGen) {
	    if (Store::CanFinalize(valp)) {
	      dict->RemoveEntry(k, prev, node);
	      finset = Store::AddToFinSet(finset, ForwardWord(val));
	      finset = Store::AddToFinSet(finset, handler);
	    }
	    // No, forward and save it again
	    else
	      node->SetValue(ForwardWord(val));
	  }
	  // Unable to decide; leave value untouched but derefed
	  else
	    node->SetValue(val);
	}
	prev  = nodes;
	nodes = node->GetNext();
      }
    }
  }
  // Now successivly forward the finalized tree
#if defined(STORE_GC_DEBUG)
  std::fprintf(stderr, "HandleWeakDictionaries: performing cheney scan\n");
#endif
  if (dstGen == (STORE_GENERATION_NUM - 1))
    Store::FinalizeCheneyScan(chunk, scan);
  else
    Store::CheneyScan(chunk, scan);
#if defined(STORE_GC_DEBUG)
  std::fprintf(stderr, "new_weakdict_size is %d\n", wkdict_set->GetSize());
#endif
  return finset;
}

void Store::HandleBlockHashTables() {
  word tables = BlockHashTable::tables;
  BlockHashTable::tables = Store::IntToWord(0);
  while (tables != Store::IntToWord(0)) {
    BTListNode *node = BTListNode::FromWordDirect(tables);
    Block *p         = Store::DirectWordToBlock(node->GetTable());
    // Get current Table ptr
    if (GCHelper::AlreadyMoved(p))
      p = GCHelper::GetForwardPtr(p);
    else if (HeaderOp::DecodeGeneration(p) < dstGen)
      p = INVALID_POINTER;
    // Do rehash only if table is alive
    if (p != INVALID_POINTER) {
      ((BlockHashTable *) p)->Rehash();
      BTListNode *node = (BTListNode *) Store::GCAlloc(BTListNode::GetSize());
      node->Init(p->ToWord(), BlockHashTable::tables);
      BlockHashTable::tables = node->ToWord();
    }
    tables = node->GetNext();
  }
}

static inline u_int min(u_int a, u_int b) {
  return ((a <= b) ? a : b);
}

inline void Store::NextGCLimits() {
  //  u_int wanted = ((GetMemUsage(roots[hdrGen]) * 100) / (100 - memFree));
  u_int wanted;
  switch (hdrGen) {
  case 0:
    wanted = GetMemUsage(roots[0]) * 4;
    break;
  case 1:
    wanted = memMax[1];
    break;
  case 2:
    {
      // to be done: find appropriate heuristics
      u_int usage = GetMemUsage(roots[2]);
      if (usage >= 35 * 1024 * 1024)
	usage -= 35 * 1024 * 1024;
      wanted = min(memMax[2], usage * 4 + 35 * 1024 * 1024);
    }
    break;
  default:
    Error("wrong header gen");
  }
  // Try to align them to block size
  s_int block_size = STORE_MEMCHUNK_SIZE;
  s_int block_dist = wanted % block_size;
  if (block_dist > 0)
    block_dist = block_size - block_dist;
  wanted += min(block_dist, ((wanted * memTolerance) / 100));
  memMax[hdrGen] = wanted;
}

static u_int gcCounter = 0;

inline void Store::DoGC(word &root, const u_int gen) {
  dstGen = (gen + 1);
  hdrGen = ((dstGen == (STORE_GENERATION_NUM - 1)) ? gen : dstGen);
  // Switch to the new Generation
  curChunk = roots[dstGen];
  // Copy Root-, Intgen- and WeakDict-Set to New Memory (if appropriate)
  Block *root_set = ForwardSet(Store::DirectWordToBlock(root));
  intgenSet       = (Set *) ForwardSet((Block *) intgenSet);
  wkDictSet       = (Set *) ForwardSet((Block *) wkDictSet);
  // Obtain scan start
  MemChunk *chunk = curChunk;
  char *scan      = curChunk->GetTop();
  // Copy matching rootset entries
  for (u_int i = root_set->GetSize(); i--;) {
    word item = root_set->GetArg(i);
    item = PointerOp::Deref(item);
    item = Store::ForwardWord(item);
    root_set->InitArg(i, item);
  }
  // Scanning chunks (root_set amount)
  Store::CheneyScan(chunk, scan);
  // Obtain new scan start (to scan intgen set stuff)
  chunk = curChunk;
  scan  = curChunk->GetTop();
  // Handle InterGenerational Pointers
  Store::HandleInterGenerationalPointers(gen);
  // Scan chunks (intgen_set amount)
  Store::CheneyScan(chunk, scan);
  // Handle Weak Dictionaries, if any (performs scanning itself)
  Block *arr = INVALID_POINTER;
  if (wkDictSet->GetSize() != 0)
    arr = Store::HandleWeakDictionaries();
  // Handle BlockHashTables
  Store::HandleBlockHashTables();
  // Clean up Collected regions
  for (u_int i = dstGen; i--;)
    Store::FreeMemChunks(roots[i]);
  // Switch Semispaces
  if (dstGen == (STORE_GENERATION_NUM - 1)) {
    MemChunk *tmp = roots[STORE_GENERATION_NUM - 2];
    roots[STORE_GENERATION_NUM - 2] = roots[STORE_GENERATION_NUM - 1];
    roots[STORE_GENERATION_NUM - 1] = tmp;
    // Cut down shadow region
    Store::FreeMemChunks(roots[STORE_GENERATION_NUM - 1]);
  }
  // Clear GC Flag
  StatusWord::ClearStatus(GCStatus());
  // Calc Limits for next GC
  //  u_int wanted = ((GetMemUsage(roots[hdrGen]) * 100) / (100 - memFree));
  u_int wanted;
  switch (hdrGen) {
  case 0:
    wanted = GetMemUsage(roots[0]) * 4;
    break;
  case 1:
    wanted = memMax[1];
    break;
  case 2:
    {
      // to be done: find appropriate heuristics
      u_int usage = GetMemUsage(roots[2]);
      if (usage >= 35 * 1024 * 1024)
	usage -= 35 * 1024 * 1024;
      wanted = min(memMax[2], usage * 4 + 35 * 1024 * 1024);
    }
    break;
  default:
    Error("wrong header gen");
  }
  // Try to align them to block size
  s_int block_size = STORE_MEMCHUNK_SIZE;
  s_int block_dist = wanted % block_size;
  if (block_dist > 0)
    block_dist = block_size - block_dist;
  wanted += min(block_dist, ((wanted * memTolerance) / 100));
  memMax[hdrGen] = wanted;
  // Switch back to Generation Zero and Adjust Root Set
  curChunk = roots[0];
  chunkTop = curChunk->GetTop();
  chunkMax = curChunk->GetMax();
  root = root_set->ToWord();
  // Call Finalization Handler
  if (arr != INVALID_POINTER) {
    u_int size = (Store::WordToInt(arr->GetArg(0)) - 1);
    for (u_int i = size; i >= 1; i--) {
      Finalization *handler =
	(Finalization *) Store::DirectWordToUnmanagedPointer(arr->GetArg(i--));
      handler->Finalize(arr->GetArg(i));
    }
  }
}

void Store::DoGC(word &root) {
#if defined(STORE_DEBUG)
  std::fprintf(stderr, "GC Nb %d...\n", gcCounter++);
#endif
  //MemStat();
#if defined(STORE_GC_DEBUG)
  std::fprintf(stderr, "Pre-GC checking...\n");
  VerifyGC(root);
  std::fprintf(stderr, "passed.\n");
#endif
#if defined(STORE_PROFILE)
  struct timeval start_t, end_t;
  gettimeofday(&start_t, INVALID_POINTER);
#endif
  // Determine GC Range
  u_int gen = (STORE_GENERATION_NUM - 2);
  // to be done
  while ((gen > 0) && (GetMemUsage(roots[gen]) <= memMax[gen])) {
    gen--;
  }

#if defined(STORE_GC_DEBUG)
  std::fprintf(stderr, "GCing all gens <= %d.\n", gen);
  std::fprintf(stderr, "root_set   gen %d\n", HeaderOp::DecodeGeneration(Store::WordToBlock(root)));
  std::fprintf(stderr, "intgen_set gen %d\n", HeaderOp::DecodeGeneration((Block *) intgenSet));
  std::fprintf(stderr, "wkdict_set gen %d\n", HeaderOp::DecodeGeneration((Block *) wkDictSet));
#endif

#if defined(STORE_PROFILE)
  u_int dstGen   = (gen + 1);
  u_int hdrGen   = ((dstGen == (STORE_GENERATION_NUM - 1)) ? gen : dstGen);
  u_int memUsage = GetMemUsage(roots[hdrGen]);
#endif

  switch (gen) {
  case STORE_GEN_YOUNGEST:
    DoGC(root, STORE_GEN_YOUNGEST); break;
#if defined(STORE_GEN_OLDEST)
  case STORE_GEN_OLDEST:
    DoGC(root, STORE_GEN_OLDEST); break;
#endif
  default:
    DoGC(root, gen); break;
  }
#if defined(STORE_PROFILE)
  gettimeofday(&end_t, INVALID_POINTER);
  sum_t->tv_sec  += (end_t.tv_sec - start_t.tv_sec);
  sum_t->tv_usec += (end_t.tv_usec - start_t.tv_usec);
  gcLiveMem      += (GetMemUsage(roots[hdrGen]) - memUsage);
#endif
#if defined(STORE_GC_DEBUG)
  std::fprintf(stderr, "Post-GC checking...\n");
  VerifyGC(root);
#endif
  //MemStat();
#if defined(STORE_DEBUG)
  std::fprintf(stderr, "done.\n");
#endif
}

void Store::SetGCParams(u_int mem_free, u_int mem_tolerance) {
  Store::memFree      = mem_free;
  Store::memTolerance = mem_tolerance;
}

#if defined(STORE_GC_DEBUG)
#define MAX_ITERATION_STEPS 40000000

static u_int path[MAX_ITERATION_STEPS];
static u_int depth = 0;

static Block *elems[MAX_ITERATION_STEPS];
static u_int size = 0;

static word rootWord;

static void InitVerify() {
  for (u_int i = MAX_ITERATION_STEPS; i--;)
    elems[i] = NULL;
  size  = 0;
  depth = 0;
}

static const char *LabelToString(u_int l) {
  switch ((BlockLabel) l) {
  case HOLE_LABEL:
    return "HOLE";
  case FUTURE_LABEL:
    return "FUTURE";
  case REF_LABEL:
    return "REF";
  case CANCELLED_LABEL:
    return "CANCELLED";
  case BYNEED_LABEL:
    return "BYNEED";
  case HASHTABLE_LABEL:
    return "HASHTABLE";
  case QUEUE_LABEL:
    return "QUEUE";
  case STACK_LABEL:
    return "STACK";
  case THREAD_LABEL:
    return "THREAD";
  case TUPLE_LABEL:
    return "TUPLE";
  case ARGS_LABEL:
    return "ARGS";
  case CLOSURE_LABEL:
    return "CLOSURE";
  case CONCRETE_LABEL:
    return "CONCRETE";
  default:
    return NULL;
  }
}
static void PrintFailurePath() {
  Block *level = Store::WordToBlock(rootWord);
  for (u_int i = 0; i < depth; i++) {
    u_int branch  = path[i];
    u_int label   = level->GetLabel();
    u_int sz      = level->GetSize();
    const char *s = LabelToString(label);
    if (s == NULL)
      std::fprintf(stderr, "Branch %d/%d in %x, type %d\n",
		   branch, sz, level, label);
    else
      std::fprintf(stderr, "Branch %d/%d in %x, type %s\n",
		   branch, sz, level, s);
    level = Store::WordToBlock(level->GetArg(branch));
  }
}

static void PrintLocatePath() {
  Block *level = Store::WordToBlock(rootWord);
  for (u_int i = 0; i < depth; i++) {
    u_int branch  = path[i];
    std::fprintf(stderr, "%d/", branch);
  }
  std::fprintf(stderr, "\n");
}

static bool IsAlive(MemChunk **roots, char *p) {
  if (p != NULL) {
    for (u_int i = 0; i < STORE_GENERATION_NUM - 1; i++) {
      MemChunk *chunk = roots[i];
      while (chunk != NULL) {
	if (p >= chunk->GetBase() && (p < chunk->GetTop()))
	  return true;
	chunk = chunk->GetNext();
      }
    }
  }
  return false;
}

static void Verify(MemChunk **roots, word x) {
  AssertStore(depth < MAX_ITERATION_STEPS);
  AssertStore(size < MAX_ITERATION_STEPS);
  if (PointerOp::IsInt(x)) {
    AssertStore(PointerOp::DecodeInt(x) != INVALID_INT);
  } else {
    Block *p = PointerOp::RemoveTag(x);
    if (p == NULL) {
      std::fprintf(stderr, "Verify: null pointer encountered: %x --> %x\n",
	      (word) p, x);
      PrintFailurePath();
      AssertStore(0);
    }
    else if (GCHelper::AlreadyMoved(p)) {
      std::fprintf(stderr, "Verify: found forward pointer %x (%x)\n",
		   (word) p, x);
      PrintFailurePath();
      AssertStore(0);
    }
    else if (!IsAlive(roots, (char *) p)) {
      std::fprintf(stderr, "Verify: found non-alive pointer %x (word=%x)\n",
		   p, x);
      PrintFailurePath();
      AssertStore(0);
    }
    u_int key = ((u_int) p % MAX_ITERATION_STEPS);
    if (elems[key] == NULL) {
      elems[key] = p;
      size++;
    }
    else {
      while ((elems[key] != NULL) && (key < MAX_ITERATION_STEPS)) {
	if (elems[key] == p)
	  return;
	else
	  key++;
      }
      AssertStore(key < MAX_ITERATION_STEPS);
      elems[key] = p;
      size++;
    }
    BlockLabel l = p->GetLabel();
    if (l != CHUNK_LABEL) {
      u_int size = p->GetSize();
      for (u_int i = size; i--;) {
  	word item = p->GetArg(i);
  	path[depth++] = i;
  	Verify(roots, item);
  	depth--;
      }
    }
  }
}

static void Locate(word x, word v) {
  AssertStore(depth < MAX_ITERATION_STEPS);
  AssertStore(size < MAX_ITERATION_STEPS);
  if (!PointerOp::IsInt(x)) {
    Block *p = PointerOp::RemoveTag(x);
    u_int key = ((u_int) p % MAX_ITERATION_STEPS);
    if (elems[key] == NULL) {
      elems[key] = p;
      size++;
    }
    else {
      while ((elems[key] != NULL) && (key < MAX_ITERATION_STEPS)) {
	if (elems[key] == p)
	  return;
	else
	  key++;
      }
      AssertStore(key < MAX_ITERATION_STEPS);
      elems[key] = p;
      size++;
    }
    BlockLabel l = p->GetLabel();
    if (l != CHUNK_LABEL) {
      u_int size = p->GetSize();
      for (u_int i = size; i--;) {
  	word item = p->GetArg(i);
  	path[depth++] = i;
	if (item == v) {
	  PrintLocatePath();
	}
  	Locate(item, v);
  	depth--;
      }
    }
  }
}

void Store::VerifyGC(word root) {
  curChunk->SetTop(chunkTop);
  InitVerify();
  rootWord = root;
  Verify(roots, root);
  InitVerify();
  rootWord = BlockHashTable::tables;
  Verify(roots, BlockHashTable::tables);
}
#endif

#if defined(STORE_DEBUG)
void Store::ForceGC(word &root, const u_int gen) {
  Store::DoGC(root, gen);
}
#endif

void Store::MemStat() {
  std::fprintf(stderr, "---\n");
  for (u_int i = 0; i < STORE_GENERATION_NUM - 1; i++) {
    MemChunk *chunk = roots[i];
    u_int used      = 0;
    u_int total     = 0;
    while (chunk != NULL) {
      char *base = chunk->GetBase();
      used  += (u_int) (chunk->GetTop() - base);
      total += (u_int) (chunk->GetMax() - base);
      chunk = chunk->GetNext();
    }
    std::fprintf(stderr, "G%d --> Used: %8u; Total: %8u; GC-Limit: %8u.\n",
		 i, used, total, memMax[i]);
  }
  std::fprintf(stderr, "---\n");
}

void Store::JITReplaceArg(u_int i, Block *p, word v) {
  AssertStore(v != (word) 0);
  if (!PointerOp::IsInt(v)) {
    u_int valgen = HeaderOp::DecodeGeneration(PointerOp::RemoveTag(v));
    u_int mygen  = HeaderOp::DecodeGeneration(p);
    if ((valgen < mygen) && (!HeaderOp::IsChildish(p)))
      Store::AddToIntgenSet(p);
  }
  p->InitArg(i, v);
}

#if defined(STORE_PROFILE)
void Store::ResetTime() {
  sum_t->tv_sec = sum_t->tv_usec = 0;
  totalMem = 0;
  for (u_int i = STORE_GENERATION_NUM; i--;) {
    totalMem += Store::GetMemUsage(roots[i]);
  }
  gcLiveMem = 0;
}

struct timeval *Store::ReadTime() {
  u_int total = 0;

  for (u_int i = STORE_GENERATION_NUM; i--;) {
    total += Store::GetMemUsage(roots[i]);
  }
  totalMem = (total - totalMem);
  return sum_t;
}
#endif
