//
// Authors:
//   Thorsten Brunklaus <brunklaus@ps.uni-sb.de>
//   Leif Kornstaedt <kornstae@ps.uni-sb.de>
//
// Copyright:
//   Thorsten Brunklaus, 2002
//   Leif Kornstaedt, 2002
//
// Last Change:
//   $Date$ by $Author$
//   $Revision$
//

#if defined(INTERFACE)
#pragma implementation "generic/BindFutureWorker.hh"
#endif

#include <cstdio>
#include "generic/Backtrace.hh"
#include "generic/Scheduler.hh"
#include "generic/Transients.hh"
#include "generic/BindFutureWorker.hh"

// BindFutureFrame
class BindFutureFrame: private StackFrame {
private:
  enum { FUTURE_POS, SIZE };
public:
  // BindFutureFrame Constructor
  static BindFutureFrame *New(Thread *thread,
			      Worker *worker, Transient *future) {
    NEW_THREAD_STACK_FRAME(frame, thread, worker, SIZE);
    frame->InitArg(FUTURE_POS, future->ToWord());
    return static_cast<BindFutureFrame *>(frame);
  }
  // BindFutureFrame Accessors
  u_int GetSize() {
    return StackFrame::GetSize() + SIZE;
  }
  Future *GetFuture() {
    Transient *transient =
      Store::WordToTransient(StackFrame::GetArg(FUTURE_POS));
    Assert(transient != INVALID_POINTER &&
	   transient->GetLabel() == FUTURE_LABEL);
    return static_cast<Future *>(transient);
  }
};

//
// BindFutureWorker Functions
//
BindFutureWorker *BindFutureWorker::self;

void BindFutureWorker::PushFrame(Thread *thread, Transient *future) {
  BindFutureFrame::New(thread, self, future);
  thread->PushHandler(Store::IntToWord(0));
}

static inline bool IsCyclic(word x, Future *future) {
  return static_cast<Future *>(Store::WordToTransient(x)) == future;
}

u_int BindFutureWorker::GetFrameSize(StackFrame *sFrame) {
  BindFutureFrame *frame = static_cast<BindFutureFrame *>(sFrame);
  Assert(sFrame->GetWorker() == this);
  return frame->GetSize();
}

Worker::Result BindFutureWorker::Run(StackFrame *sFrame) {
  BindFutureFrame *frame = static_cast<BindFutureFrame *>(sFrame);
  Assert(sFrame->GetWorker() == this);
  Scheduler::PopHandler();
  Future *future = frame->GetFuture();
  Scheduler::PopFrame(frame->GetSize());
  future->ScheduleWaitingThreads();
  Construct();
  word arg = Scheduler::currentArgs[0];
  if (IsCyclic(arg, future)) { // cancel future with Cyclic exception
    future->Become(CANCELLED_LABEL, Hole::cyclicExn);
    Assert(Scheduler::nArgs == Scheduler::ONE_ARG);
    Scheduler::currentArgs[0] = future->ToWord();
    return Worker::CONTINUE;
  } else { // actually bind the future
    future->Become(REF_LABEL, arg);
    Assert(Scheduler::nArgs == Scheduler::ONE_ARG);
    // Scheduler::currentArgs[0] is already set to `arg'
    return Worker::CONTINUE;
  }
}

Worker::Result BindFutureWorker::Handle(word) {
  StackFrame *sFrame = Scheduler::GetFrame();
  BindFutureFrame *frame = static_cast<BindFutureFrame *>(sFrame);
  Assert(sFrame->GetWorker() == this);
  Future *future = frame->GetFuture();
  Scheduler::PopFrame(frame->GetSize());
  future->ScheduleWaitingThreads();
  future->Become(CANCELLED_LABEL, Scheduler::currentData);
  Scheduler::nArgs = Scheduler::ONE_ARG;
  Scheduler::currentArgs[0] = future->ToWord();
  return Worker::CONTINUE;
}

const char *BindFutureWorker::Identify() {
  return "BindFutureWorker";
}

void BindFutureWorker::DumpFrame(StackFrame *) {
  std::fprintf(stderr, "Bind future\n");
}
