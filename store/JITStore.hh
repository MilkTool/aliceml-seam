//
// Author:
//   Thorsten Brunklaus <brunklaus@ps.uni-sb.de>
//
// Copyright:
//   Thorsten Brunklaus, 2002
//
// Last Change:
//   $Date$ by $Author$
//   $Revision$
//
#ifndef __STORE__JIT_STORE_HH__
#define __STORE__JIT_STORE_HH__

#if defined(INTERFACE)
#pragma interface "store/JITStore.hh"
#endif

#include <cstdio>
#include "store/Store.hh"
#include <lightning.h>

// Make sure to have one lightning copy only
extern jit_state lightning;
#define _jit lightning

// Provide own jump/branch macros
// This is to eliminate the "value computed not used" warning
// in case the destination label is already known

// Lightning Jumps
#define drop_jit_jmpi(O1) \
  { jit_insn *dummy = jit_jmpi(O1); dummy = dummy; }

#define drop_jit_jmpr(O1) \
  { jit_insn *dummy = jit_jmpr(O1); dummy = dummy; }

// Lightning Branches (ui part only)
#define drop_jit_blti_ui(O1, O2, O3) \
  { jit_insn *dummy = jit_blti_ui(O1, O2, O3); dummy = dummy; }
#define drop_jit_bltr_ui(O1, O2, O3) \
  { jit_insn *dummy = jit_bltr_ui(O1, O2, O3); dummy = dummy; }

#define drop_jit_blei_ui(O1, O2, O3) \
  { jit_insn *dummy = jit_blei_ui(O1, O2, O3); dummy = dummy; }
#define drop_jit_bler_ui(O1, O2, O3) \
  { jit_insn *dummy = jit_bler_ui(O1, O2, O3); dummy = dummy; }

#define drop_jit_bgti_ui(O1, O2, O3) \
  { jit_insn *dummy = jit_bgti_ui(O1, O2, O3); dummy = dummy; }
#define drop_jit_bgtr_ui(O1, O2, O3) \
  { jit_insn *dummy = jit_bgtr_ui(O1, O2, O3); dummy = dummy; }

#define drop_jit_bgei_ui(O1, O2, O3) \
  { jit_insn *dummy = jit_bgei_ui(O1, O2, O3); dummy = dummy; }
#define drop_jit_bger_ui(O1, O2, O3) \
  { jit_insn *dummy = jit_bger_ui(O1, O2, O3); dummy = dummy; }

#define drop_jit_beqi_ui(O1, O2, O3) \
  { jit_insn *dummy = jit_beqi_ui(O1, O2, O3); dummy = dummy; }
#define drop_jit_beqr_ui(O1, O2, O3) \
  { jit_insn *dummy = jit_beqr_ui(O1, O2, O3); dummy = dummy; }

#define drop_jit_bnei_ui(O1, O2, O3) \
  { jit_insn *dummy = jit_bnei_ui(O1, O2, O3); dummy = dummy; }
#define drop_jit_bner_ui(O1, O2, O3) \
  { jit_insn *dummy = jit_bner_ui(O1, O2, O3); dummy = dummy; }

extern "C" {
  void disassemble(FILE *, char *, char *);
};
typedef void (*value_plotter)(word);

class JITStore {
protected:
public:
  static void ClearStack(u_int nbArgs) {
    if (nbArgs != 0) {
      jit_addi_ui(JIT_SP, JIT_SP, nbArgs * sizeof(word));
    }
  }
  static void PerformCall(u_int nbArgs) {
    jit_callr(JIT_R0);
    ClearStack(nbArgs);
  }
  static void Call(u_int nbArgs, void *proc) {
    jit_movi_p(JIT_R0, proc);
    PerformCall(nbArgs);
  }
protected:
  static void Prepare() {
    jit_pushr_ui(JIT_R1);
    jit_pushr_ui(JIT_R2);
  }
  static void Finish() {
    jit_popr_ui(JIT_R2);
    jit_popr_ui(JIT_R1);
  }
  // Output: Ptr holds Allocated Block
  static void Alloc(u_int Ptr, u_int size, u_int header) {
    Assert(Ptr != JIT_R0);
    Assert(Ptr != JIT_FP);
    // Allocation Loop
    jit_insn *loop = jit_get_label();
    jit_ldi_p(Ptr, &Store::chunkTop);
    jit_movi_ui(JIT_R0, header);
    jit_str_ui(Ptr, JIT_R0);
    jit_movr_p(JIT_R0, Ptr);
    jit_addi_p(JIT_R0, JIT_R0, size);
    jit_ldi_p(JIT_FP, &Store::chunkMax);
    jit_insn *succeeded = jit_bltr_p(jit_forward(), JIT_R0, JIT_FP);
    Prepare();
    Call(0, (void *) Store::AllocNewMemChunkStd);
    Finish();
    drop_jit_jmpi(loop);
    jit_patch(succeeded);
    jit_sti_p(&Store::chunkTop, JIT_R0);
  }
  // Input: word ptr
  // Output: derefed word ptr
  static void Deref(u_int Ptr) {
    Assert(Ptr != JIT_R0);
    // Main deref loop
    jit_insn *loop = jit_get_label();
    jit_movr_ui(JIT_R0, Ptr);
    jit_andi_ui(JIT_R0, JIT_R0, TAGMASK);
    jit_insn *no_transient = jit_bnei_ui(jit_forward(), JIT_R0, TRTAG);
    jit_xori_ui(Ptr, Ptr, TRTAG);
    // R0 = Ptr->GetLabel();
    jit_ldr_ui(JIT_R0, Ptr);
    jit_andi_ui(JIT_R0, JIT_R0, TAG_MASK);
    u_int tag = REF_LABEL << TAG_SHIFT;
    jit_insn *unbound_transient = jit_bnei_ui(jit_forward(), JIT_R0, tag);
    jit_ldxi_p(Ptr, Ptr, 1 * sizeof(word));
    drop_jit_jmpi(loop); // Continue deref
    jit_patch(unbound_transient);
    jit_xori_ui(Ptr, Ptr, TRTAG); // Restore tag
    jit_patch(no_transient);
  }
public:
  // Logging Support
  static void InitLoggging();
  static void LogMesg(const char *mesg);
  static void LogReg(u_int Value);
   static void DumpReg(u_int Value, value_plotter plotter);
  static void LogRead(u_int Dest, u_int Ptr, u_int Index);
  static void LogWrite(u_int Ptr, u_int Index, u_int Value);
  static void LogSetArg(u_int pos, u_int Value);
  // Input: word ptr
  // Output: derefed word ptr and jmp to the corresponding path
  static void Deref3(u_int Ptr, jit_insn *ref[2]) {
    Assert(Ptr != JIT_R0);
    // Main deref loop
    jit_insn *loop = jit_get_label();
    jit_movr_ui(JIT_R0, Ptr);
    jit_andi_ui(JIT_R0, JIT_R0, TAGMASK);
    jit_insn *no_transient = jit_bnei_ui(jit_forward(), JIT_R0, TRTAG);
    jit_xori_ui(Ptr, Ptr, TRTAG);
    // R0 = V1->GetLabel();
    jit_ldr_ui(JIT_R0, Ptr);
    jit_andi_ui(JIT_R0, JIT_R0, TAG_MASK);
    u_int tag = REF_LABEL << TAG_SHIFT;
    ref[0] = jit_bnei_ui(jit_forward(), JIT_R0, tag); // Transient branch
    jit_ldxi_p(Ptr, Ptr, 1 * sizeof(word));
    drop_jit_jmpi(loop); // Continue deref
    jit_patch(no_transient);
    ref[1] = jit_beqi_ui(jit_forward(), JIT_R0, BLKTAG); // Block Branch
  }
  // Input: word ptr
  // Output: derefed word ptr and jmp to the corresponding path
  static void DerefItem(u_int Ptr, jit_insn *ref[2]) {
    Assert(Ptr != JIT_R0);
    // Main deref loop
    jit_insn *loop = jit_get_label();
    jit_movr_ui(JIT_R0, Ptr);
    jit_andi_ui(JIT_R0, JIT_R0, TAGMASK);
    ref[1] = jit_bnei_ui(jit_forward(), JIT_R0, TRTAG); // Item Branch
    jit_xori_ui(Ptr, Ptr, TRTAG);
    // R0 = Ptr->GetLabel();
    jit_ldr_ui(JIT_R0, Ptr);
    jit_andi_ui(JIT_R0, JIT_R0, TAG_MASK);
    u_int tag = REF_LABEL << TAG_SHIFT;
    ref[0] = jit_bnei_ui(jit_forward(), JIT_R0, tag); // Transient branch
    jit_ldxi_p(Ptr, Ptr, 1 * sizeof(word));
    drop_jit_jmpi(loop); // Continue deref
  }
  static void SetTransientTag(u_int Ptr) {
    jit_xori_ui(Ptr, Ptr, TRTAG);
  }
  //
  // Block Field Access
  //
  static void GetArg(u_int Dest, u_int Ptr, u_int Index) {
#if defined(JIT_STORE_DEBUG)
    LogRead(Dest, Ptr, Index);
    jit_ldxi_p(Dest, Ptr, (Index + 1) * sizeof(word));
    LogMesg("passed; Value is\n");
    LogReg(Dest);
#else
    jit_ldxi_p(Dest, Ptr, (Index + 1) * sizeof(word));
#endif
  }
  static void InitArg(u_int Ptr, u_int Index, u_int Value) {
#if defined(JIT_STORE_DEBUG)
    LogWrite(Ptr, Index, Value);
    jit_stxi_p((Index + 1) * sizeof(word), Ptr, Value);
    LogMesg("passed\n");
#else
    jit_stxi_p((Index + 1) * sizeof(word), Ptr, Value);
#endif
  }
  static void ReplaceArg(u_int Ptr, u_int Index, u_int Value) {
    if (STORE_GENERATION_NUM == 2) {
      jit_stxi_p((Index + 1) * sizeof(word), Ptr, Value);
    } 
    else {
#if 0
      jit_stxi_p((Index + 1) * sizeof(word), Ptr, Value);
      jit_ldr_p(JIT_R0, &Store::intgenPtr);
      jit_insn *can_store = jit_bnei_p(jit_forward(), JIT_R0, intgenMax);
      jit_movi_ui(JIT_R0, 1);
      jit_sti_ui(&Store::forceMajorGC, JIT_R0);
      jit_insn *skip_store = jit_jmpi(jit_forward());
      jit_patch(can_store);
      jit_str_p(JIT_R0, Ptr);
      jit_addi_p(JIT_R0, JIT_R0, sizeof(word));
      jit_sti_p(&Store::intgenPtr, JIT_R0);
      jit_patch(skip_store);
#else
      Prepare();
      jit_pushr_ui(Value);
      jit_pushr_ui(Ptr);
      jit_movi_ui(JIT_R0, Index);
      jit_pushr_ui(JIT_R0);
      Call(3, (void *) Store::JITReplaceArg);
      Finish();
#endif
    }
  }
  //
  // Store Allocation
  //
  // Output: Ptr = Allocated Block
  static void AllocBlock(u_int Ptr, BlockLabel label, u_int size) {
    size = HeaderOp::TranslateSize(size);
    u_int header = HeaderOp::EncodeHeader(label, size, 0);
    Alloc(Ptr, Store::BlockMemSize(size), header);
  }
  // Output: Ptr = chunk ptr
  static void AllocChunk(u_int Ptr, u_int size) {
    u_int ws = (1 + (((size + sizeof(u_int)) - 1) / sizeof(u_int)));
    AllocBlock(Ptr, CHUNK_LABEL, ws);
    jit_movi_ui(JIT_R0, PointerOp::EncodeInt(size));
    InitArg(Ptr, 0, JIT_R0);
  }
  // Output: Ptr = transient ptr
  static void AllocTransient(u_int Ptr, BlockLabel label) {
    AllocBlock(Ptr, label, 1);
  }
  //
  // Store Import/Export
  //
  // Dest = Store::IntToWord(Int);
  static void IntToWord(u_int Dest, u_int Int) {
    if (Dest != Int)
      jit_movr_ui(Dest, Int);
    jit_lshi_ui(Dest, Dest, 1);
    jit_ori_ui(Dest, Dest, INTTAG);
  }
  // Input: Ptr word ptr
  // Output: Dest integer
  static void DirectWordToInt(u_int Dest, u_int Ptr) {
    if (Dest != Ptr)
      jit_movr_ui(Dest, Ptr);
    jit_rshi_i(Dest, Dest, 1); // sign bit is propagated
  }
  // Input: Ptr = word ptr
  // Output: Dest = integer or INVALID_INT
  static void WordToInt(u_int Dest, u_int Ptr) {
    Deref(Ptr);
    jit_movr_ui(JIT_R0, Ptr);
    jit_andi_ui(JIT_R0, JIT_R0, TAGMASK);
    jit_insn *no_int = jit_bnei_ui(jit_forward(), JIT_R0, INTTAG);
    DirectWordToInt(Dest, Ptr);
    jit_insn *eob = jit_jmpi(jit_forward());
    jit_patch(no_int);
    jit_movi_ui(Dest, INVALID_INT);
    jit_patch(eob);
  }
  // Input: Ptr word ptr
  // Output: Dest block ptr
  static void DirectWordToBlock(u_int Dest, u_int Ptr) {
    if (Dest != Ptr)
      jit_movr_ui(Dest, Ptr);
  }
  // Input: Ptr = word ptr
  // Output: Dest = block ptr
  static void WordToBlock(u_int Dest, u_int Ptr) {
    Deref(Ptr);
    jit_movr_ui(JIT_R0, Ptr);
    jit_andi_ui(JIT_R0, JIT_R0, TAGMASK);
    jit_insn *no_block = jit_bnei_ui(jit_forward(), JIT_R0, BLKTAG);
    if (Dest != Ptr)
      jit_movr_ui(Dest, Ptr);
    jit_insn *eob = jit_jmpi(jit_forward());
    jit_patch(no_block);
    jit_movr_ui(Dest, INVALID_POINTER);
    jit_patch(eob);
  }
  // Input: Ptr = word ptr
  // Output: Dest = transient ptr
  static void DirectWordToTransient(u_int Dest, u_int Ptr) {
    if (Dest != Ptr)
      jit_movr_ui(Dest, Ptr);
    jit_xori_ui(Dest, Ptr, TRTAG);
  }
  // Intput: Ptr = word ptr
  // Output Dest = transient ptr
  static void WordToTransient(u_int Dest, u_int Ptr) {
    Deref(Ptr);
    jit_movr_ui(JIT_R0, Ptr);
    jit_andi_ui(JIT_R0, JIT_R0, TAGMASK);
    jit_insn *no_transient = jit_bnei_ui(jit_forward(), JIT_R0, TRTAG);
    if (Dest != Ptr)
      jit_movr_ui(Dest, Ptr);
    jit_xori_ui(Dest, Dest, TRTAG);
    jit_insn *eob = jit_jmpi(jit_forward());
    jit_patch(no_transient);
    jit_movr_ui(Dest, INVALID_POINTER);
    jit_patch(eob);
  }
  // Dest = Store::PointerToWord(Ptr);
  static void PointerToWord(u_int Dest, u_int Ptr) {
    if (Dest != Ptr)
      jit_movr_ui(Dest, Ptr);
    jit_lshi_ui(Dest, Dest, 1);
    jit_ori_ui(Dest, Dest, INTTAG);
  }
  // Dest = Store::DirectWordToPointer(Ptr);
  static void DirectWordToPointer(u_int Dest, u_int Ptr) {
    jit_rshi_ui(Dest, Ptr, 1);
  }
  // Machine Status
  static void LoadStatus(u_int Dest) {
    jit_ldi_ui(Dest, &StatusWord::status);
  }
  //
  // Store Values
  //
  class Block {
  public:
    // Dest = Ptr->GetSize()
    static void GetSize(u_int Dest, u_int Ptr) {
      // Compute Size
      jit_ldr_ui(Dest, Ptr);
      jit_movr_ui(JIT_FP, Dest);
      jit_andi_ui(Dest, Dest, SIZE_MASK);
      jit_rshi_ui(Dest, Dest, SIZE_SHIFT);
      jit_andi_ui(JIT_FP, JIT_FP, SIZESHIFT_MASK);
      jit_lshr_ui(Dest, Dest, JIT_FP);
    }
    // Dest = Ptr->GetLabel()
    static void GetLabel(u_int Dest, u_int Ptr) {
      jit_ldr_ui(Dest, Ptr);
      jit_andi_ui(Dest, Dest, TAG_MASK);
      jit_rshi_ui(Dest, Dest, TAG_SHIFT);
    }
  };

  class Chunk {
  public:
    // Dest = Ptr->GetSize() (in bytes)
    static void GetSize(u_int Dest, u_int Ptr) {
      GetArg(Dest, Ptr, 0);
    }
    // Dest = Ptr->GetBase();
    static void GetBase(u_int Dest, u_int Ptr) {
      jit_movr_ui(Dest, Ptr);
      jit_addi_ui(Dest, Dest, 2 * sizeof(word));
    }
  };
};

#endif