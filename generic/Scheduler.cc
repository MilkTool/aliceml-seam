//
// Authors:
//   Thorsten Brunklaus <brunklaus@ps.uni-sb.de>
//   Leif Kornstaedt <kornstae@ps.uni-sb.de>
//
// Copyright:
//   Thorsten Brunklaus, 2002
//   Leif Kornstaedt, 2000-2002
//
// Last Change:
//   $Date$ by $Author$
//   $Revision$
//

#if defined(INTERFACE)
#pragma implementation "generic/Scheduler.hh"
#endif

#include <cstdio>
#include "generic/RootSet.hh"
#include "generic/ConcreteCode.hh"
#include "generic/Closure.hh"
#include "generic/Transients.hh"
#include "generic/StackFrame.hh"
#include "generic/BindFutureWorker.hh"
#include "generic/PushCallWorker.hh"
#include "generic/IOHandler.hh"
#include "generic/SignalHandler.hh"

#if PROFILE
#include "generic/Profiler.hh"
#endif

word Scheduler::root;
ThreadQueue *Scheduler::threadQueue;
Thread *Scheduler::currentThread;
TaskStack *Scheduler::currentTaskStack;
u_int Scheduler::nFrames;

u_int Scheduler::nArgs;
word Scheduler::currentArgs[Scheduler::maxArgs];
word Scheduler::currentData;
Backtrace *Scheduler::currentBacktrace;

void Scheduler::Timer() {
  StatusWord::SetStatus(PreemptStatus());
}

void Scheduler::Init() {
  threadQueue = ThreadQueue::New();
  root = Store::IntToWord(0);
  RootSet::Add(root);
}

inline void Scheduler::SwitchToThread() {
  // Precondition: currentThread initialized
  Assert(currentThread->GetState() == Thread::RUNNABLE);
  Assert(!currentThread->IsSuspended());
  currentTaskStack = currentThread->GetTaskStack(nFrames);
  word args = currentThread->GetArgs(nArgs);
  Assert(nArgs == ONE_ARG || nArgs < maxArgs);
  switch (nArgs) {
  case 0:
    break;
  case Scheduler::ONE_ARG:
    currentArgs[0] = args;
    break;
  default:
    Block *b = Store::DirectWordToBlock(args);
    Assert(b->GetLabel() == ARGS_LABEL);
    for (u_int i = nArgs; i--; )
      currentArgs[i] = b->GetArg(i);
    break;
  }
}

inline void Scheduler::FlushThread() {
  currentThread->SetTaskStack(currentTaskStack, nFrames);
  Assert(nArgs == ONE_ARG || nArgs < maxArgs);
  switch (nArgs) {
  case 0:
    currentThread->SetArgs(0, Store::IntToWord(0));
    break;
  case Scheduler::ONE_ARG:
    currentThread->SetArgs(ONE_ARG, currentArgs[0]);
    break;
  default:
    Block *b = Store::AllocBlock(ARGS_LABEL, nArgs);
    for (u_int i = nArgs; i--; )
      b->InitArg(i, currentArgs[i]);
    currentThread->SetArgs(nArgs, b->ToWord());
    break;
  }
}

int Scheduler::Run() {
  while (true) {
    while ((currentThread = threadQueue->Dequeue()) != INVALID_POINTER) {
      SwitchToThread();
      for (bool nextThread = false; !nextThread; ) {
#if PROFILE
	Profiler::SampleHeap();
#endif
	StackFrame *frame = StackFrame::FromWordDirect(GetFrame());
	Worker *worker = frame->GetWorker();
	Worker::Result result = worker->Run();
#if PROFILE
	Profiler::AddHeap(frame);
#endif
      interpretResult:
	switch (result) {
	case Worker::CONTINUE:
	  Assert(nArgs == ONE_ARG || nArgs < maxArgs);
	  break;
	case Worker::PREEMPT:
	  FlushThread();
	  threadQueue->Enqueue(currentThread);
	  nextThread = true;
	  break;
	case Worker::SUSPEND:
	  FlushThread();
	  currentThread->Purge();
	  currentThread->Suspend();
	  nextThread = true;
	  break;
	case Worker::RAISE:
	  {
	  raise:
#if PROFILE
	    Profiler::SampleHeap();
#endif
	    frame = StackFrame::FromWordDirect(GetFrame());
	    worker = frame->GetWorker();
	    result = worker->Handle();
#if PROFILE
	    Profiler::AddHeap(frame);
#endif
	    goto interpretResult;
	  }
	case Worker::REQUEST:
	  {
	    Transient *transient = Store::WordToTransient(currentData);
	    Assert(transient != INVALID_POINTER);
	    switch (transient->GetLabel()) {
	    case HOLE_LABEL:
	      currentData = Hole::holeExn;
	      goto raise;
	    case FUTURE_LABEL:
	      {
		FlushThread();
		currentThread->Purge();
		Future *future = static_cast<Future *>(transient);
		future->AddToWaitQueue(currentThread);
		currentThread->BlockOn(transient->ToWord());
		nextThread = true;
	      }
	      break;
	    case CANCELLED_LABEL:
	      currentData = transient->GetArg();
	      goto raise;
	    case BYNEED_LABEL:
	      {
		Thread *newThread = NewThread(0, Store::IntToWord(0));
		BindFutureWorker::PushFrame(newThread, transient);
		PushCallWorker::PushFrame(newThread, transient->GetArg());
		// The future's argument is an empty wait queue:
		transient->Become(FUTURE_LABEL, Store::IntToWord(0));
		FlushThread();
		currentThread->Purge();
		Future *future = static_cast<Future *>(transient);
		future->AddToWaitQueue(currentThread);
		currentThread->BlockOn(transient->ToWord());
		nextThread = true;
	      }
	      break;
	    default:
	      Error("Scheduler::Run: invalid transient label");
	      break;
	    }
	  }
	  break;
	case Worker::TERMINATE:
	  FlushThread();
	  currentThread->SetTerminated();
	  nextThread = true;
	  break;
	case Worker::EXIT:
	  return Store::DirectWordToInt(currentData);
	}
      }
      if (Store::NeedGC()) {
	threadQueue->Purge();
	IOHandler::Purge();
	root = threadQueue->ToWord();
	RootSet::DoGarbageCollection();
	threadQueue = ThreadQueue::FromWordDirect(root);
      }
      if (SignalHandler::GetSignalStatus())
	SignalHandler::HandlePendingSignals();
      IOHandler::Poll();
      StatusWord::ClearStatus();
    }
    // Check for both incoming signals and I/O
    IOHandler::Block();
    SignalHandler::HandlePendingSignals();
  }
}

Worker::Result Scheduler::PushCall(word wClosure) {
  Assert(Store::WordToInt(wClosure) == INVALID_INT);
  Transient *transient = Store::WordToTransient(wClosure);
  if (transient == INVALID_POINTER) { // Closure is determined
    Closure *closure = Closure::FromWord(wClosure);
    Assert(closure != INVALID_POINTER);
    word concreteCodeWord = closure->GetConcreteCode();
    transient = Store::WordToTransient(concreteCodeWord);
    if (transient == INVALID_POINTER) { // ConcreteCode is determined
      ConcreteCode *concreteCode = ConcreteCode::FromWord(concreteCodeWord);
      Assert(concreteCode != INVALID_POINTER);
      concreteCode->GetInterpreter()->PushCall(closure);
#if PROFILE
      StackFrame *frame = StackFrame::FromWordDirect(GetFrame());
      Profiler::IncCalls(frame);
#endif
      if (StatusWord::GetStatus() != 0)
	return Worker::PREEMPT;
      else
	return Worker::CONTINUE;
    } else { // Request ConcreteCode
      PushCallWorker::PushFrame(wClosure);
      currentData = transient->ToWord();
      return Worker::REQUEST;
    }
  } else { // Request Closure
    PushCallWorker::PushFrame(wClosure);
    currentData = transient->ToWord();
    return Worker::REQUEST;
  }
}
