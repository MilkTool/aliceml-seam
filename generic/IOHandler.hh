//
// Author:
//   Leif Kornstaedt <kornstae@ps.uni-sb.de>
//
// Copyright:
//   Leif Kornstaedt, 2002
//
// Last Change:
//   $Date$ by $Author$
//   $Revision$
//

#ifndef __GENERIC__IO_HANDLER_HH__
#define __GENERIC__IO_HANDLER_HH__

#if defined(INTERFACE)
#pragma interface "generic/IOHandler.hh"
#endif

#include "Base.hh"

class Future;

class DllExport IOHandler {
protected:
  static int defaultFD;
public:
  static void Init();
  static void SetDefaultBlockFD(int fd) {
    defaultFD = fd;
  }
  static void Poll();
  static void Block();
  static void Purge();

  // These return INVALID_POINTER if the fd is already readable/writable:
  static Future *CheckReadable(int fd);
  static Future *CheckWritable(int fd);
};

#endif
