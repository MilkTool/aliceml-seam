//
// Author:
//   Leif Kornstaedt <kornstae@ps.uni-sb.de>
//
// Copyright:
//   Leif Kornstaedt, 2000-2002
//
// Last Change:
//   $Date$ by $Author$
//   $Revision$
//

#ifndef __GENERIC__THREAD_HH__
#define __GENERIC__THREAD_HH__

#if defined(INTERFACE)
#pragma interface "generic/Thread.hh"
#endif

#include "store/Store.hh"
#include "generic/TaskStack.hh"

class DllExport Thread: private Block {
  friend class Scheduler;
public:
  enum priority { HIGH, NORMAL, LOW };
  enum state { BLOCKED, RUNNABLE, TERMINATED };
private:
  enum {
    PRIORITY_POS, STATE_POS, IS_SUSPENDED_POS,
    TASK_STACK_POS, NFRAMES_POS, NARGS_POS, ARGS_POS,
    FUTURE_POS, SIZE
  };

  void SetState(state s) {
    ReplaceArg(STATE_POS, s);
    ReplaceArg(FUTURE_POS, 0);
  }
  void Suspend() {
    ReplaceArg(IS_SUSPENDED_POS, true);
  }
  void Resume() {
    ReplaceArg(IS_SUSPENDED_POS, false);
  }
public:
  using Block::ToWord;

  // Thread Constructor
  static Thread *New(u_int nArgs, word args) {
    Block *b = Store::AllocBlock(THREAD_LABEL, SIZE);
    b->InitArg(PRIORITY_POS, NORMAL);
    b->InitArg(STATE_POS, RUNNABLE);
    b->InitArg(IS_SUSPENDED_POS, false);
    b->InitArg(NFRAMES_POS, TaskStack::initialNumberOfFrames);
    b->InitArg(TASK_STACK_POS, TaskStack::New()->ToWord());
    b->InitArg(NARGS_POS, nArgs);
    b->InitArg(ARGS_POS, args);
    b->InitArg(FUTURE_POS, 0);
    return static_cast<Thread *>(b);
  }
  // Thread Untagging
  static Thread *FromWord(word x) {
    Block *b = Store::WordToBlock(x);
    Assert(b == INVALID_POINTER || b->GetLabel() == THREAD_LABEL);
    return static_cast<Thread *>(b);
  }
  static Thread *FromWordDirect(word x) {
    Block *b = Store::DirectWordToBlock(x);
    Assert(b->GetLabel() == THREAD_LABEL);
    return static_cast<Thread *>(b);
  }

  // Thread Accessors
  priority GetPriority() {
    return static_cast<priority>(Store::DirectWordToInt(GetArg(PRIORITY_POS)));
  }
  state GetState() {
    return static_cast<state>(Store::DirectWordToInt(GetArg(STATE_POS)));
  }
  bool IsSuspended() {
    return Store::DirectWordToInt(GetArg(IS_SUSPENDED_POS));
  }
  TaskStack *GetTaskStack(u_int &nFrames) {
    nFrames = Store::DirectWordToInt(GetArg(NFRAMES_POS));
    TaskStack *taskStack = TaskStack::FromWordDirect(GetArg(TASK_STACK_POS));
#ifdef DEBUG_CHECK
    ReplaceArg(TASK_STACK_POS, 0);
#endif
    return taskStack;
  }
  void SetTaskStack(TaskStack *taskStack, u_int nFrames) {
    Assert(GetArg(TASK_STACK_POS) == Store::IntToWord(0));
    ReplaceArg(NFRAMES_POS, nFrames);
    ReplaceArg(TASK_STACK_POS, taskStack->ToWord());
  }
  word GetArgs(u_int &nArgs) {
    nArgs = Store::DirectWordToInt(GetArg(NARGS_POS));
    return GetArg(ARGS_POS);
  }
  void SetArgs(u_int nArgs, word args) {
    ReplaceArg(NARGS_POS, nArgs);
    ReplaceArg(ARGS_POS, args);
  }
  void SetTerminated() {
    SetArgs(0, Store::IntToWord(0));
    SetState(TERMINATED);
  }
  void BlockOn(word future) {
    // Store the future we're blocking on, for unregistering:
    SetState(BLOCKED);
    ReplaceArg(FUTURE_POS, future);
  }
  word GetFuture() {
    return GetArg(FUTURE_POS);
  }
  void Wakeup() {
    ReplaceArg(FUTURE_POS, 0);
    SetState(RUNNABLE);
  }

  // Task Stack Operations
  void PushFrame(word frame) {
    u_int nFrames = Store::DirectWordToInt(GetArg(NFRAMES_POS));
    TaskStack *taskStack = TaskStack::FromWordDirect(GetArg(TASK_STACK_POS));
    if (nFrames == taskStack->GetSize()) {
      taskStack = taskStack->Enlarge();
      ReplaceArg(TASK_STACK_POS, taskStack->ToWord());
    }
    Assert(nFrames < taskStack->GetSize());
    taskStack->ReplaceArg(nFrames++, frame);
    ReplaceArg(NFRAMES_POS, nFrames);
  }
  void Purge() {
    u_int nFrames = Store::DirectWordToInt(GetArg(NFRAMES_POS));
    TaskStack::FromWordDirect(GetArg(TASK_STACK_POS))->Purge(nFrames);
  }
};

#endif
