//
// Author:
//   Thorsten Brunklaus <brunklaus@ps.uni-sb.de>
//
// Copyright:
//   Thorsten Brunklaus, 2000-2001
//
// Last Change:
//   $Date$ by $Author$
//   $Revision$
//
#ifndef __STORE__VALUE_HH__
#define __STORE__VALUE_HH__

#if defined(INTERFACE)
#pragma interface "store/Value.hh"
#endif

class Block {
private:
  static const u_int HANDLER_POS = 0;
public:
  word *GetBase() {
    return (word *) this + 1;
  }
  BlockLabel GetLabel() {
    return HeaderOp::DecodeLabel(this);
  }
  u_int GetSize() {
    return HeaderOp::DecodeSize(this);
  }
  word GetArg(u_int f) {
    AssertStore(f < GetSize());
    return ((word *) this)[f + 1];
  }
  void InitArg(u_int f, word v) {
    AssertStore(f < GetSize());
    AssertStore(v != NULL);
    ((word *) this)[f + 1] = v;
  }
  void InitArg(u_int f, int v) {
    InitArg(f, Store::IntToWord(v));
  }
  void ReplaceArg(u_int f, word v) {
    AssertStore(f < GetSize());
    AssertStore(v != NULL);
    if (!PointerOp::IsInt(v)) {
      u_int valgen = HeaderOp::DecodeGeneration(PointerOp::RemoveTag(v));
      u_int mygen  = HeaderOp::DecodeGeneration(this);
      
      if ((valgen < mygen) && (!HeaderOp::IsChildish(this))) {
	Store::AddToIntgenSet(this);
      }
    }
    ((word *) this)[f + 1] = v;
  }
  void ReplaceArg(u_int f, int v) {
    InitArg(f, Store::IntToWord(v));
  }
  word ToWord() {
    return PointerOp::EncodeBlock(this);
  }
};

class Transient : private Block {
private:
  static const u_int REF_POS = 0;
public:
  using Block::GetLabel;

  word ToWord() {
    return PointerOp::EncodeTransient(this);
  }
  word GetArg() {
    return Block::GetArg(REF_POS);
  }
  void InitArg(word w) {
    Block::InitArg(REF_POS, w);
  }
  void InitArg(int w) {
    Block::InitArg(REF_POS, w);
  }
  void ReplaceArg(word w) {
    Block::ReplaceArg(REF_POS, w);
  }
  void ReplaceArg(int w) {
    Block::InitArg(REF_POS, w);
  }
  void Become(BlockLabel l, word w) {
    AssertStore((GetLabel() >= MIN_TRANSIENT_LABEL) &&
		(GetLabel() <= MAX_TRANSIENT_LABEL) &&
		(GetLabel() != REF_LABEL));
    AssertStore(l >= MIN_TRANSIENT_LABEL && l <= MAX_TRANSIENT_LABEL);
    HeaderOp::EncodeLabel(this, l);
    Block::ReplaceArg(REF_POS, w);
  }
  void Become(BlockLabel l, int i) {
    AssertStore((GetLabel() >= MIN_TRANSIENT_LABEL) &&
		(GetLabel() <= MAX_TRANSIENT_LABEL) &&
		(GetLabel() != REF_LABEL));
    AssertStore(l >= MIN_TRANSIENT_LABEL && l <= MAX_TRANSIENT_LABEL);
    HeaderOp::EncodeLabel(this, l);
    Block::ReplaceArg(REF_POS, i);
  }
};

class Chunk : private Block {
private:
  static const u_int BYTESIZE_POS = 0;
public:
  using Block::GetLabel;
  using Block::ToWord;

  char *GetBase() {
    return (char *) this + 2 * sizeof(u_int);
  }
  u_int GetSize() {
    return Store::DirectWordToInt(GetArg(BYTESIZE_POS));
  }
  // String hashing function is taken from
  // 'Aho, Sethi, Ullman: Compilers..., page 436
  u_int Hash() {
    u_int len      = this->GetSize();
    const char *s  = this->GetBase();
    const char *sm = (s + len);
    unsigned h = 0, g;
    for (const char *p = s; p < sm; p++) {
      h = (h << 4) + (*p);
      if ((g = h & 0xf0000000)) {
	h = h ^ (g >> 24);
	h = h ^ g;
      }
    }
    return h;
  }
  
  static Chunk *FromWord(word x) {
    Block *p = Store::WordToBlock(x);

    AssertStore((p == INVALID_POINTER) || (p->GetLabel() == CHUNK_LABEL));
    return (Chunk *) p;
  }
  static Chunk *FromWordDirect(word x) {
    Block *p = Store::DirectWordToBlock(x);
    
    AssertStore((p == INVALID_POINTER) || (p->GetLabel() == CHUNK_LABEL));
    return (Chunk *) p;
  }
};

#endif
