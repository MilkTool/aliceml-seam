//
// Authors:
//   Thorsten Brunklaus <brunklaus@ps.uni-sb.de>
//
// Copyright:
//   Thorsten Brunklaus, 2002
//
// Last Change:
//   $Date$ by $Author$
//   $Revision$
//

#ifndef __GENERIC__JITTER_GENERIC_DATA_HH__
#define __GENERIC__JITTER_GENERIC_DATA_HH__

#if defined(INTERFACE)
#pragma interface "generic/JitterGenericData.hh"
#endif

#include "store/JITStore.hh"
#include "generic/Scheduler.hh"
#include "alice/JitterImmediateEnv.hh"

namespace Generic {
  class Scheduler {
  protected:
    static void Sel(void *addr, u_int Dest) {
      jit_ldi_p(Dest, addr);
    }
    static void Put(void *addr, u_int Value) {
      jit_sti_p(addr, Value);
    }
  public:
    static void GetNArgs(u_int Dest) {
      Sel(&::Scheduler::nArgs, Dest);
    }
    static void PutNArgs(u_int Value) {
      Put(&::Scheduler::nArgs, Value);
    }
    static void GetCurrentArgs(u_int Ptr) {
      jit_movi_p(Ptr, &::Scheduler::currentArgs);
    }
    static void SelArg(u_int Dest, u_int Ptr, u_int pos) {
      jit_ldxi_p(Dest, Ptr, pos * sizeof(word));
    }
    static void PutArg(u_int Ptr, u_int pos, u_int Value) {
      JITStore::LogSetArg(pos, Value);
      jit_stxi_p(pos * sizeof(word), Ptr, Value);
    }
    static void GetZeroArg(u_int Dest) {
      Sel(&::Scheduler::currentArgs[0], Dest);
    }
    static void GetOneArg(u_int Dest) {
      Sel(&::Scheduler::currentArgs[1], Dest);
    }
    static void PutZeroArg(u_int Value) {
      Put(&::Scheduler::currentArgs[0], Value);
    }
    static void GetCurrentData(u_int Dest) {
      Sel(&::Scheduler::currentData, Dest);
    }
    static void SetCurrentData(u_int Value) {
      Put(&::Scheduler::currentData, Value);
    }
    static void GetCurrentBacktrace(u_int Dest) {
      Sel(&::Scheduler::currentBacktrace, Dest);
    }
    static void SetCurrentBacktrace(u_int Value) {
      Put(&::Scheduler::currentBacktrace, Value);
    }
    // Side Effect: Scratches JIT_R0, JIT_FP
    static void PushFrame(u_int Dest, u_int size) {
#if defined(JIT_ASSERT_INDEX)
      JITStore::SaveAllRegs();
      jit_movi_ui(JIT_R0, size);
      jit_pushr_ui(JIT_R0);
      JITStore::Call(1, (void *) ::Scheduler::PushFrame);
      jit_sti_p(&JITStore::loadedWord, JIT_RET);
      JITStore::RestoreAllRegs();
      jit_ldi_p(Dest, &JITStore::loadedWord);
#else
      Assert(Dest != JIT_R0);
      Assert(Dest != JIT_FP);
      jit_insn *loop = jit_get_label();
      jit_ldi_p(JIT_R0, &::Scheduler::stackTop);
      jit_addi_p(JIT_R0, JIT_R0, size * sizeof(word));
      jit_ldi_p(JIT_FP, &::Scheduler::stackMax);
      jit_insn *succeeded = jit_bltr_p(jit_forward(), JIT_R0, JIT_FP);
      JITStore::Prepare();
      JITStore::Call(0, (void *) ::Scheduler::EnlargeTaskStack);
      JITStore::Finish();
      drop_jit_jmpi(loop);
      jit_patch(succeeded);
      jit_sti_p(&::Scheduler::stackTop, JIT_R0);
      jit_movr_p(Dest, JIT_R0);
      jit_subi_p(Dest, Dest, sizeof(word));
#endif
    }
    // Side Effect: Scratches JIT_R0
    static void PushFrameNoCheck(u_int Dest, u_int size) {
#if defined(JIT_ASSERT_INDEX)
      JITStore::SaveAllRegs();
      jit_movi_ui(JIT_R0, size);
      jit_pushr_ui(JIT_R0);
      JITStore::Call(1, (void *) ::Scheduler::PushFrame);
      jit_sti_p(&JITStore::loadedWord, JIT_RET);
      JITStore::RestoreAllRegs();
      jit_ldi_p(Dest, &JITStore::loadedWord);
#else
      Assert(Dest != JIT_R0);
      jit_ldi_p(JIT_R0, &::Scheduler::stackTop);
      jit_addi_p(JIT_R0, JIT_R0, size * sizeof(word));
      jit_sti_p(&::Scheduler::stackTop, JIT_R0);
      jit_movr_p(Dest, JIT_R0);
      jit_subi_p(Dest, Dest, sizeof(word));
#endif
    }
    // Side-Effect: Scratches JIT_R0, JIT_FP
    static void GetFrame(u_int This) {
#if defined(JIT_ASSERT_INDEX)
      JITStore::SaveAllRegs();
      JITStore::Call(0, (void *) ::Scheduler::GetFrame);
      jit_sti_p(&JITStore::loadedWord, JIT_RET);
      JITStore::RestoreAllRegs();
      jit_ldi_p(This, &JITStore::loadedWord);
#else
      jit_ldi_p(This, &::Scheduler::stackTop);
      jit_subi_p(This, This, sizeof(word));
#endif
    }
    static void PopFrame(u_int size) {
      jit_ldi_p(JIT_R0, &::Scheduler::stackTop);
      jit_subi_p(JIT_R0, JIT_R0, size * sizeof(word));
      jit_sti_p(&::Scheduler::stackTop, JIT_R0);
    }
  };

  class StackFrame : public ::StackFrame {
  protected:
    static void InitArg(u_int This, u_int index, u_int Value) {
      s_int offset = (0 - (s_int) index) * sizeof(word);
      jit_stxi_p(offset, This, Value);
    }
    static void GetArg(u_int Dest, u_int This, u_int index) {
      s_int offset = (0 - (s_int) index) * sizeof(word);
      jit_ldxi_p(Dest, This, offset);
    }
  public:
    // Side Effect: Scratches JIT_R0, JIT_FP
    static void New(u_int This, u_int size, Worker *worker) {
      u_int frSize = BASE_SIZE + size;
      Scheduler::PushFrame(This, frSize);
      jit_movi_p(JIT_R0, Store::UnmanagedPointerToWord(worker));
      InitArg(This, WORKER_POS, JIT_R0);
    }
    // Side Effect: Scratches JIT_R0
    static void NewNoCheck(u_int This, u_int size, Worker *worker) {
      u_int frSize = BASE_SIZE + size;
      Scheduler::PushFrameNoCheck(This, frSize);
      jit_movi_p(JIT_R0, Store::UnmanagedPointerToWord(worker));
      InitArg(This, WORKER_POS, JIT_R0);
    }
    static void Sel(u_int Dest, u_int This, u_int pos) {
      GetArg(Dest, This, BASE_SIZE + pos);
    }
    static void Put(u_int This, u_int pos, u_int Value) {
      InitArg(This, BASE_SIZE + pos, Value);
    }
    static void Replace(u_int This, u_int pos, u_int Value) {
      InitArg(This, BASE_SIZE + pos, Value);
    }
  };


  class Transform : public ::Transform {
  public:
    // Side-Effect: Scratches JIT_R0, JIT_FP
    static void New(u_int This) {
      JITStore::AllocBlock(This, TRANSFORM_LABEL, SIZE);
    }
    static void PutName(u_int This, u_int Value) {
      JITStore::InitArg(This, NAME_POS, Value);
    }
    static void PutArgument(u_int This, u_int Value) {
      JITStore::InitArg(This, ARGUMENT_POS, Value);
    }
  };

  class Tuple {
  public:
    // Side-Effect: Scratches JIT_R0, JIT_FP
    static void New(u_int This, u_int size) {
      JITStore::AllocBlock(This, TUPLE_LABEL, size);
    }
    static void Sel(u_int Dest, u_int This, u_int pos) {
      JITStore::GetArg(Dest, This, pos);
    }
    static void Put(u_int This, u_int pos, u_int Value) {
      JITStore::InitArg(This, pos, Value);
    }
    static void IndexSel(u_int Dest, u_int This, u_int Pos) {
      jit_addi_ui(Pos, Pos, 1);
      // to be done: compute log_2 sizeof(word)
      jit_lshi_ui(Pos, Pos, 2);
      jit_ldxr_p(Dest, This, Pos);
    }
  };

  class Closure : public ::Closure {
  public:
    // Side-Effect: Scratches JIT_R0, JIT_FP
    static void New(u_int This, u_int size) {
      JITStore::AllocBlock(This, CLOSURE_LABEL, BASE_SIZE + size);
    }
    static void InitConcreteCode(u_int This, u_int Value) {
      JITStore::InitArg(This, CONCRETE_CODE_POS, Value);
    }
    static void Sel(u_int This, u_int Dest, u_int pos) {
      JITStore::GetArg(Dest, This, BASE_SIZE + pos);
    }
    static void Put(u_int This, u_int pos, u_int Value) {
      JITStore::InitArg(This, BASE_SIZE + pos, Value);
    }
  };

  class ConcreteCode : public ::ConcreteRepresentation {
  public:
    // Side-Effect: Scratches JIT_R0, JIT_FP
    static void New(u_int This, Interpreter *interpreter, u_int size) {
      JITStore::AllocBlock(This, CONCRETE_LABEL, BASE_SIZE + size);
      ConcreteRepresentationHandler *handler = interpreter;
      jit_movi_p(JIT_R0, Store::UnmanagedPointerToWord(handler));
      JITStore::InitArg(This, HANDLER_POS, JIT_R0);
    }
    static void Sel(u_int Dest, u_int This, u_int pos) {
      JITStore::GetArg(Dest, This, BASE_SIZE + pos);
    }
    static void Put(u_int This, u_int pos, u_int Value) {
      JITStore::InitArg(This, BASE_SIZE + pos, Value);
    }
  };

  class Byneed : public ::Transient {
  public:
    // Side-Effect: Scratches JIT_R0, JIT_FP
    static void New(u_int This) {
      JITStore::AllocTransient(This, BYNEED_LABEL);
    }
    static void InitClosure(u_int This, u_int Closure) {
      JITStore::InitArg(This, REF_POS, Closure);
    }
  };

  class Primitive {
  public:
    // Side-Effect: Scratches JIT_R0
    static void Return1(u_int Value) {
      Scheduler::PutZeroArg(Value);
      jit_movi_ui(JIT_R0, ::Scheduler::ONE_ARG);
      Scheduler::PutNArgs(JIT_R0);
      jit_movi_ui(JIT_R0, Worker::CONTINUE);
      jit_ret();
    }
  };
};

#endif
