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
#pragma implementation "generic/Primitive.hh"
#endif

#include <cstdio>
#include "generic/Closure.hh"
#include "generic/ConcreteCode.hh"
#include "generic/Scheduler.hh"
#include "generic/RootSet.hh"
#include "generic/StackFrame.hh"
#include "generic/Transform.hh"
#include "generic/Primitive.hh"

// Primitive Frame
class PrimitiveFrame: private StackFrame {
private:
  enum { SIZE };
public:
  // PrimitiveFrame Constructor
  static PrimitiveFrame *New(Worker *worker) {
    NEW_STACK_FRAME(frame, worker, SIZE);
    return static_cast<PrimitiveFrame *>(frame);
  }
  u_int GetSize() {
    return StackFrame::GetSize() + SIZE;
  }
};

// PrimitiveInterpreter: An interpreter that runs primitives
class PrimitiveInterpreter: public Interpreter {
private:
  const char *name;
  Interpreter::function function;
  u_int arity;
public:
  PrimitiveInterpreter(const char *_name, Interpreter::function _function,
		       u_int _arity):
    name(_name), function(_function), arity(_arity) {}

  virtual Transform *GetAbstractRepresentation(ConcreteRepresentation *);

  virtual u_int GetFrameSize(StackFrame *sFrame);
  virtual Result Run(StackFrame *sFrame);
  virtual void PushCall(Closure *closure);
  virtual const char *Identify();
  virtual void DumpFrame(StackFrame *sFrame);

  virtual u_int GetInArity(ConcreteCode *concreteCode);
  virtual Interpreter::function GetCFunction();

  static Result Run(PrimitiveInterpreter *interpreter);

  Interpreter::function GetFunction() {
    return function;
  }
};

//
// PrimitiveInterpreter Functions
//
inline Worker::Result
PrimitiveInterpreter::Run(PrimitiveInterpreter *interpreter) {
  switch (interpreter->arity) {
  case 0:
    if (Scheduler::nArgs == Scheduler::ONE_ARG) {
      Transient *t = Store::WordToTransient(Scheduler::currentArgs[0]);
      if (t == INVALID_POINTER) { // is determined
	Scheduler::nArgs = 0;
	return interpreter->function();
      } else { // need to request
	Scheduler::currentData = Scheduler::currentArgs[0];
	return Worker::REQUEST;
      }
    } else {
      Assert(Scheduler::nArgs == 0);
      return interpreter->function();
    }
  case 1:
    Construct();
    return interpreter->function();
  default:
    if (Deconstruct()) {
      // Deconstruct has set Scheduler::currentData as a side-effect
      return Worker::REQUEST;
    } else {
      Assert(Scheduler::nArgs == interpreter->arity);
      return interpreter->function();
    }
  }
}

Transform *
PrimitiveInterpreter::GetAbstractRepresentation(ConcreteRepresentation *b) {
  ConcreteCode *concreteCode = static_cast<ConcreteCode *>(b);
  word wAbstract = concreteCode->Get(0);
  if (wAbstract == Store::IntToWord(0))
    return INVALID_POINTER;
  else
    return Transform::FromWordDirect(wAbstract);
}

void PrimitiveInterpreter::PushCall(Closure *closure) {
  Assert(ConcreteCode::FromWord(closure->GetConcreteCode())->
	 GetInterpreter() == this); closure = closure;
  PrimitiveFrame::New(static_cast<Worker *>(this));
}

u_int PrimitiveInterpreter::GetFrameSize(StackFrame *sFrame) {
  PrimitiveFrame *frame = static_cast<PrimitiveFrame *>(sFrame);
  Assert(sFrame->GetWorker() == this);
  return frame->GetSize();
}

Worker::Result PrimitiveInterpreter::Run(StackFrame *) {
  return Run(this);
}

const char *PrimitiveInterpreter::Identify() {
  return name;
}

void PrimitiveInterpreter::DumpFrame(StackFrame *) {
  std::fprintf(stderr, "%s\n", name);
}

u_int PrimitiveInterpreter::GetInArity(ConcreteCode *) {
  return arity == 1? Scheduler::ONE_ARG: arity;
}

Interpreter::function PrimitiveInterpreter::GetCFunction() {
  return GetFunction();
}

//
// Primitive Functions
//
word Primitive::MakeFunction(const char *name, Interpreter::function function,
			     u_int arity, Transform *abstract) {
  PrimitiveInterpreter *interpreter =
    new PrimitiveInterpreter(name, function, arity);
  ConcreteCode *concreteCode = ConcreteCode::New(interpreter, 1);
  if (abstract == INVALID_POINTER)
    concreteCode->Init(0, Store::IntToWord(0));
  else
    concreteCode->Init(0, abstract->ToWord());
  return concreteCode->ToWord();
}

Worker::Result Primitive::Execute(Interpreter *interpreter) {
  PrimitiveFrame::New(static_cast<Worker *>(interpreter));
  Interpreter::function function = static_cast<PrimitiveInterpreter *>
    (interpreter)->GetFunction();
  return function();
}
