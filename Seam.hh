//
// Author:
//   Leif Kornstaedt <kornstae@ps.uni-sb.de>
//
// Copyright:
//   Leif Kornstaedt, 2002-2003
//
// Last Change:
//   $Date$ by $Author$
//   $Revision$
//

#ifndef __SEAM_HH__
#define __SEAM_HH__

#define SEAM_FOREIGN

#include "generic/SignalHandler.hh"
#include "store/Store.hh"
#include "store/Map.hh"
#include "store/WeakMap.hh"
#include "adt/IntMap.hh"
#include "adt/ChunkMap.hh"
#include "adt/Queue.hh"
#include "adt/Stack.hh"
#include "generic/FinalizationSet.hh"
#include "generic/Transform.hh"
#include "generic/ConcreteRepresentationHandler.hh"
#include "generic/ConcreteRepresentation.hh"
#include "generic/ConcreteCode.hh"
#include "generic/Closure.hh"
#include "generic/Thread.hh"
#include "generic/ThreadQueue.hh"
#include "generic/Tuple.hh"
#include "generic/String.hh"
#include "generic/StackFrame.hh"
#include "generic/Backtrace.hh"
#include "generic/Pickle.hh"
#include "generic/Float.hh"
#include "generic/Double.hh"
#include "generic/Debug.hh"
#include "generic/RootSet.hh"
#include "generic/UniqueString.hh"
#include "generic/TaskStack.hh"
#include "generic/IOHandler.hh"
#include "generic/IODesc.hh"
#include "generic/Scheduler.hh"
#include "generic/Transients.hh"
#include "generic/Worker.hh"
#include "generic/Interpreter.hh"
#include "generic/Primitive.hh"
#include "generic/PushCallWorker.hh"
#include "generic/BindFutureWorker.hh"
#include "generic/Unpickler.hh"
#include "generic/Pickler.hh"
#include "generic/Profiler.hh"
#include "generic/Authoring.hh"

#if defined(__MINGW32__) || defined(_MSC_VER)
// This cannot use the DllExport macro
// (DllExport sometimes expands to __declspec(dllimport) when it shoudln't)
extern "C" __declspec(dllexport) word InitComponent();
#else
extern "C" word InitComponent();
#endif

#endif
