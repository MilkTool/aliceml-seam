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

#if defined(INTERFACE)
#pragma implementation "generic/IODesc.hh"
#endif

#if defined(__MINGW32__) || defined(_MSC_VER)
#include <windows.h>
#include <winsock.h>
#define GetLastSocketError() WSAGetLastError()
#define Interruptible(res, call) int res = call; res = res;
#define closesocket(s) closesocket(s)
#else
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#define GetLastSocketError() errno
#define GetLastError() errno
#define Interruptible(res, call)			\
  int res;						\
  do {							\
    res = call;						\
  } while (res < 0 && GetLastSocketError() == EINTR);
#define closesocket(s) close(s)
#endif

#include "generic/Transients.hh"
#include "generic/IOHandler.hh"
#include "generic/IODesc.hh"
#include "generic/Scheduler.hh"

//
// Windows: Forwarding between file handles and sockets
//
#if defined(__MINGW32__) || defined(_MSC_VER)
class IOForwarder {
private:
  class InOut {
  public:
    SOCKET socket;
    HANDLE handle;
    InOut(SOCKET s, HANDLE h): socket(s), handle(h) {}
  };

  static const u_int BUFFER_SIZE = 1024;
  static DWORD __stdcall ReaderThread(void *p);
  static DWORD __stdcall WriterThread(void *p);
public:
  static int SocketPair(int type, int *sv) {
    int newsock = socket(AF_INET, type, 0);
    if (newsock < 0) return -1;
    // bind the socket to any unused port
    struct sockaddr_in sock_in;
    sock_in.sin_family = AF_INET;
    sock_in.sin_port = 0;
    sock_in.sin_addr.s_addr = INADDR_ANY;
    if (bind(newsock, (struct sockaddr *) &sock_in, sizeof(sock_in)) < 0)
      return -1;
    int len = sizeof(sock_in);
    if (getsockname(newsock, (struct sockaddr *) &sock_in, &len) < 0) {
      closesocket(newsock);
      return -1;
    }
    listen(newsock, 2);
    // create a connecting socket
    int outsock = socket(AF_INET, type, 0);
    if (outsock < 0) {
      closesocket(newsock);
      return  -1;
    }
    sock_in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    // Do a connect and accept the connection
    if (connect(outsock, (struct sockaddr *) &sock_in, sizeof(sock_in)) < 0) {
      closesocket(newsock);
      closesocket(outsock);
      return -1;
    }
    int insock = accept(newsock, (struct sockaddr *) &sock_in, &len);
    if (insock < 0) {
      closesocket(newsock);
      closesocket(outsock);
      return -1;
    }
    closesocket(newsock);
    sv[0] = insock;
    sv[1] = outsock;
    return 0;
  }
  static void CreateReader(SOCKET s, HANDLE h) {
    DWORD threadId;
    HANDLE hThread =
      CreateThread(NULL, 1024, &ReaderThread, new InOut(s, h), 0, &threadId);
    CloseHandle(hThread);
  }
  static void CreateWriter(SOCKET s, HANDLE h) {
    DWORD threadId;
    HANDLE hThread =
      CreateThread(NULL, 1024, &WriterThread, new InOut(s, h), 0, &threadId);
    CloseHandle(hThread);
  }
};

DWORD __stdcall IOForwarder::ReaderThread(void *p) {
  InOut *io = static_cast<InOut *>(p);
  SOCKET out = io->socket;
  HANDLE in = io->handle;
  delete io;

  // This one solves a problem with W2K SP2.
  // ReaderThread cause the system to freeze if we
  // don't call gethostname() (?load ws2_32.dll? changed with SP2)
  // before ReadFile().
  char dummyBuf[1024];
  gethostname(dummyBuf, sizeof(dummyBuf));

  char buf[BUFFER_SIZE];
  while (true) {
    DWORD count;
    if (ReadFile(in, buf, BUFFER_SIZE, &count, NULL) == FALSE) {
      if (GetLastError() != ERROR_BROKEN_PIPE)
	std::fprintf(stderr, "ReadFile failed: %ld\n", GetLastError());
      break;
    }
    if (count == 0)
      break;
    u_int totalSent = 0;
  loop:
    Interruptible(sent, send(out, &buf[totalSent], count, 0));
    if (sent == SOCKET_ERROR) {
      std::fprintf(stderr, "send(%d) failed: %d\n", out, GetLastSocketError());
      break;
    }
    count -= sent;
    totalSent += sent;
    if (count > 0)
      goto loop;
  }
  CloseHandle(in);
  closesocket(out);
  return 0;
}

DWORD __stdcall IOForwarder::WriterThread(void *p) {
  InOut *io = static_cast<InOut *>(p);
  SOCKET in = io->socket;
  HANDLE out = io->handle;
  delete io;

  char buf[BUFFER_SIZE];
  while (true) {
    Interruptible(got, recv(in, buf, BUFFER_SIZE, 0));
    if (got == SOCKET_ERROR) {
      std::fprintf(stderr, "recv(%d) failed: %d\n", in, GetLastSocketError());
      break;
    }
    if (got == 0)
      break;
    u_int totalWritten = 0;
  loop:
    DWORD count;
    if (WriteFile(out, &buf[totalWritten], got, &count, 0) == FALSE) {
      std::fprintf(stderr, "WriteFile failed: %ld\n", GetLastError());
      break;
    }
    totalWritten += count;
    got -= count;
    if (got > 0)
      goto loop;
  }
  closesocket(in);
  CloseHandle(out);
  return 0;
}
#endif

//
// IODescFinalizationSet Implementation
//
void IODescFinalizationSet::Finalize(word value) {
  IODesc::FromWordDirect(value)->Close(); // ignore result
}

//
// IODesc Implementation
//
IODescFinalizationSet *IODesc::finalizationSet;

void IODesc::Init() {
  finalizationSet = new IODescFinalizationSet();
#if defined(DEBUG_CHECK) && (defined(__MINGW32__) || defined(_MSC_VER))
  FreeConsole();
  AllocConsole();
#endif
}

IODesc *IODesc::NewClosed(String *name) {
  Block *p = Store::AllocBlock(IODESC_LABEL, SIZE);
  p->InitArg(FLAGS_POS, TYPE_CLOSED);
  p->InitArg(NAME_POS, name->ToWord());
  return static_cast<IODesc *>(p);
}

IODesc *IODesc::NewFromFD(u_int dir, String *name, int fd) {
  Block *p = Store::AllocBlock(IODESC_LABEL, SIZE);
  p->InitArg(FLAGS_POS, TYPE_FD|dir);
  p->InitArg(NAME_POS, name->ToWord());
  p->InitArg(FD_POS, fd);
  p->InitArg(FINALIZATION_KEY_POS, finalizationSet->Register(p->ToWord()));
  return static_cast<IODesc *>(p);
}

#if defined(__MINGW32__) || defined(_MSC_VER)
IODesc *IODesc::NewFromHandle(u_int dir, String *name, HANDLE handle) {
  Block *p = Store::AllocBlock(IODESC_LABEL, SIZE);
  p->InitArg(FLAGS_POS, TYPE_HANDLE|dir);
  p->InitArg(NAME_POS, name->ToWord());
  Chunk *c = Store::AllocChunk(sizeof(HANDLE));
  ((HANDLE *) c->GetBase())[0] = handle;
  p->InitArg(HANDLE_POS, c->ToWord());
  p->InitArg(FINALIZATION_KEY_POS, finalizationSet->Register(p->ToWord()));
  return static_cast<IODesc *>(p);
}

IODesc *IODesc::NewForwarded(u_int dir, String *name, HANDLE handle) {
  int fd;
  // Forward from the handle to a socket, so that we can use select():
  int sv[2];
  if (IOForwarder::SocketPair(SOCK_STREAM, sv) == -1)
    Error("socketpair failed");
  switch (dir) {
  case DIR_READER:
    IOForwarder::CreateReader(sv[0], handle);
    fd = sv[1];
    break;
  case DIR_WRITER:
    IOForwarder::CreateWriter(sv[1], handle);
    fd = sv[0];
    break;
  default:
    Error("invalid direction");
  }
  unsigned long arg = true;
  ioctlsocket(fd, FIONBIO, &arg);
  Block *p = Store::AllocBlock(IODESC_LABEL, SIZE);
  p->InitArg(FLAGS_POS, TYPE_FORWARDED | dir);
  p->InitArg(NAME_POS, name->ToWord());
  p->InitArg(FD_POS, fd);
  Chunk *c = Store::AllocChunk(sizeof(HANDLE));
  ((HANDLE *) c->GetBase())[0] = handle;
  p->InitArg(HANDLE_POS, c->ToWord());
  p->InitArg(FINALIZATION_KEY_POS, finalizationSet->Register(p->ToWord()));
  return static_cast<IODesc *>(p);
}
#endif

IODesc::kind IODesc::GetKind() {
  switch (GetType()) {
  case TYPE_CLOSED: return CLOSED;
#if defined(__MINGW32__) || defined(_MSC_VER)
  case TYPE_FD: return SOCKET;
  case TYPE_HANDLE:
    switch (GetFileType(GetHandle())) {
    case FILE_TYPE_DISK: return FILE;
    case FILE_TYPE_CHAR: return TTY;
    case FILE_TYPE_PIPE: return PIPE;
    case FILE_TYPE_UNKNOWN:
    default:
      return UNKNOWN;
    }
  case TYPE_FORWARDED: return PIPE;
#else
  case TYPE_FD:
    {
      struct stat info;
      Interruptible(res, fstat(GetFD(), &info));
      if (res) return UNKNOWN;
      if (S_ISREG(info.st_mode)) return FILE;
      else if (S_ISDIR(info.st_mode)) return DIR;
      else if (S_ISCHR(info.st_mode)) return TTY;
      else if (S_ISBLK(info.st_mode)) return DEVICE;
      else if (S_ISFIFO(info.st_mode)) return PIPE;
      else if (S_ISLNK(info.st_mode)) return SYMLINK;
      else if (S_ISSOCK(info.st_mode)) return SOCKET;
      else return UNKNOWN;
    }
#endif
  default:
    Error("invalid type");
  }
}

u_int IODesc::GetChunkSize() {
  switch (GetKind()) {
  case FILE:
  case DIR:
  case SYMLINK:
  case DEVICE:
    //--** use GetDiskFreeSpace/ioctl to determine cluster size?
    return 512;
  case TTY:
  case PIPE:
  case SOCKET:
    return 1;
  case CLOSED:
    return 2;
  case UNKNOWN:
    return 1;
  default:
    Error("invalid kind");
  }
}

IODesc::result IODesc::Close() {
  u_int flags = GetFlags();
  result res = result_ok;
  switch (flags & TYPE_MASK) {
  case TYPE_CLOSED: break;
  case TYPE_FD:
    {
      Interruptible(res0, closesocket(GetFD()));
      if (res0) res = result_socket_error;
      break;
    }
#if defined(__MINGW32__) || defined(_MSC_VER)
  case TYPE_HANDLE:
    if (CloseHandle(GetHandle()) == FALSE) res = result_system_error;
    break;
  case TYPE_FORWARDED:
    if (closesocket(GetFD())) res = result_socket_error;
    if (CloseHandle(GetHandle()) == FALSE) res = result_system_error;
    break;
#endif
  }
  InitArg(FLAGS_POS, (flags & ~TYPE_MASK) | TYPE_CLOSED);
  u_int key = Store::DirectWordToInt(GetArg(FINALIZATION_KEY_POS));
  finalizationSet->Unregister(key);
  return res;
}

bool IODesc::SupportsDoBlock() {
  switch (GetType()) {
  case TYPE_CLOSED:
    return false;
  case TYPE_FD:
    return true;
#if defined(__MINGW32__) || defined(_MSC_VER)
  case TYPE_HANDLE:
    return false;
  case TYPE_FORWARDED:
    return true;
#endif
  default:
    Error("invalid type");
  }
}

IODesc::result IODesc::DoBlock() {
  switch (GetType()) {
  case TYPE_CLOSED:
    return result_closed;
#if defined(__MINGW32__) || defined(_MSC_VER)
  case TYPE_HANDLE:
    Error("DoBlock not supported for files");
  case TYPE_FORWARDED:
#endif
  case TYPE_FD:
    {
      Future *future = GetDir() == DIR_READER?
	IOHandler::WaitReadable(GetFD()):
	IOHandler::WaitWritable(GetFD());
      Scheduler::nArgs = 0;
      if (future == INVALID_POINTER)
	return result_ok;
      else {
	Scheduler::currentData = future->ToWord();
	return result_request;
      }
    }
  default:
    Error("invalid type");
  }
}

bool IODesc::IsFile() {
  switch (GetType()) {
  case TYPE_CLOSED:
    return false;
#if defined(__MINGW32__) || defined(_MSC_VER)
  case TYPE_HANDLE:
    return true;
  case TYPE_FORWARDED:
  case TYPE_FD:
    return false;
#else
  case TYPE_FD:
    {
      struct stat info;
      Interruptible(res, fstat(GetFD(), &info));
      return !res && S_ISREG(info.st_mode);
    }
#endif
  default:
    Error("invalid type");
  }
}

bool IODesc::SupportsGetPos() {
  return IsFile();
}

IODesc::result IODesc::GetPos(u_int &out) {
  //--** should support 64-bit positions
  switch (GetType()) {
  case TYPE_CLOSED:
    return result_closed;
#if defined(__MINGW32__) || defined(_MSC_VER)
  case TYPE_HANDLE:
    out = SetFilePointer(GetHandle(), 0, NULL, FILE_CURRENT);
    return out == INVALID_SET_FILE_POINTER? result_system_error: result_ok;
  case TYPE_FORWARDED:
  case TYPE_FD:
    Error("GetPos not supported for sockets or pipes");
#else
  case TYPE_FD:
    out = lseek(GetFD(), 0, SEEK_CUR);
    return out == static_cast<u_int>(static_cast<off_t>(-1))?
      result_system_error: result_ok;
#endif
  default:
    Error("invalid type");
  }
}

bool IODesc::SupportsSetPos() {
  return IsFile();
}

IODesc::result IODesc::SetPos(u_int pos) {
  //--** should support 64-bit positions
  switch (GetType()) {
  case TYPE_CLOSED:
    return result_closed;
#if defined(__MINGW32__) || defined(_MSC_VER)
  case TYPE_HANDLE:
    pos = SetFilePointer(GetHandle(), pos, NULL, FILE_BEGIN);
    return pos == INVALID_SET_FILE_POINTER? result_system_error: result_ok;
  case TYPE_FORWARDED:
  case TYPE_FD:
    Error("SetPos not supported for sockets or pipes");
#else
  case TYPE_FD:
    pos = lseek(GetFD(), pos, SEEK_SET);
    return pos == static_cast<u_int>(static_cast<off_t>(-1))?
      result_system_error: result_ok;
#endif
  default:
    Error("invalid type");
  }
}

bool IODesc::SupportsEndPos() {
  return IsFile();
}

IODesc::result IODesc::EndPos(u_int &out) {
  //--** should support 64-bit positions
  switch (GetType()) {
  case TYPE_CLOSED:
    return result_closed;
#if defined(__MINGW32__) || defined(_MSC_VER)
  case TYPE_HANDLE:
    out = GetFileSize(GetHandle(), NULL);
    return out == INVALID_FILE_SIZE? result_system_error: result_ok;
  case TYPE_FORWARDED:
  case TYPE_FD:
    Error("EndPos not supported for sockets or pipes");
#else
  case TYPE_FD:
    {
      struct stat info;
      Interruptible(res, fstat(GetFD(), &info));
      if (res) return result_system_error;
      out = info.st_size;
      return result_ok;
    }
#endif
  default:
    Error("invalid type");
  }
}

IODesc::result IODesc::GetNumberOfAvailableBytes(int &out) {
  switch (GetType()) {
  case TYPE_CLOSED:
    return result_closed;
#if defined(__MINGW32__) || defined(_MSC_VER)
  case TYPE_HANDLE:
    out = -1;
    return result_ok;
  case TYPE_FD:
  case TYPE_FORWARDED:
    {
      unsigned long arg;
      out = ioctlsocket(GetFD(), FIONREAD, &arg)? -1: static_cast<int>(out);
      return result_ok;
    }
#else
  case TYPE_FD:
    {
      unsigned long arg;
      out = ioctl(GetFD(), FIONREAD, &arg)? -1: static_cast<int>(out);
      return result_ok;
    }
#endif
  default:
    Error("invalid type");
  }
}

IODesc::result IODesc::Read(u_char *buf, int n, int &out) {
  char *sys_buf = (char *) buf;
  Assert(n > 0);
  switch (GetType()) {
  case TYPE_CLOSED:
    return result_closed;
#if defined(__MINGW32__) || defined(_MSC_VER)
  case TYPE_FD:
  case TYPE_FORWARDED:
    {
      int sock = GetFD();
    retry:
      out = recv(sock, sys_buf, n, 0);
      if (out == SOCKET_ERROR)
	if (WSAGetLastError() == WSAEWOULDBLOCK) {
	  Future *future = IOHandler::WaitReadable(sock);
	  if (future != INVALID_POINTER) {
	    Scheduler::currentData = future->ToWord();
	    return result_request;
	  } else
	    goto retry;
	} else return result_socket_error;
      return result_ok;
    }
  case TYPE_HANDLE:
    {
      DWORD nRead;
      if (ReadFile(GetHandle(), sys_buf, n, &nRead, NULL) == FALSE)
	return result_system_error;
      out = nRead;
      return result_ok;
    }
#else
  case TYPE_FD:
    {
      int fd = GetFD();
    retry:
      Interruptible(res, read(fd, sys_buf, n));
      out = res;
      if (res == -1)
	if (errno == EWOULDBLOCK) {
	  Future *future = IOHandler::WaitReadable(fd);
	  if (future != INVALID_POINTER) {
	    Scheduler::currentData = future->ToWord();
	    return result_request;
	  } else
	    goto retry;
	} else return result_system_error;
      return result_ok;
    }
#endif
  default:
    Error("invalid type");
  }
}

IODesc::result IODesc::Write(const u_char *buf, int n, int &out) {
  const char *sys_buf = (const char *) buf;
  Assert(n > 0);
  switch (GetType()) {
  case TYPE_CLOSED:
    return result_closed;
#if defined(__MINGW32__) || defined(_MSC_VER)
  case TYPE_FD:
  case TYPE_FORWARDED:
    {
      int sock = GetFD();
    retry:
      out = send(sock, sys_buf, n, 0);
      if (out == SOCKET_ERROR)
	if (WSAGetLastError() == WSAEWOULDBLOCK) {
	  Future *future = IOHandler::WaitWritable(sock);
	  if (future != INVALID_POINTER) {
	    Scheduler::currentData = future->ToWord();
	    return result_request;
	  } else
	    goto retry;
	} else return result_socket_error;
      return result_ok;
    }
  case TYPE_HANDLE:
    {
      DWORD nWritten;
      if (WriteFile(GetHandle(), sys_buf, n, &nWritten, NULL) == FALSE)
	return result_system_error;
      out = nWritten;
      return result_ok;
    }
#else
  case TYPE_FD:
    {
      int fd = GetFD();
    retry:
      Interruptible(res, write(fd, sys_buf, n));
      out = res;
      if (res == -1)
	if (errno == EWOULDBLOCK) {
	  Future *future = IOHandler::WaitWritable(fd);
	  if (future != INVALID_POINTER) {
	    Scheduler::currentData = future->ToWord();
	    return result_request;
	  } else
	    goto retry;
	} else return result_system_error;
      return result_ok;
    }
#endif
  default:
    Error("invalid type");
  }
}

bool IODesc::SupportsNonblocking() {
  switch (GetType()) {
  case TYPE_CLOSED:
    return false;
#if defined(__MINGW32__) || defined(_MSC_VER)
  case TYPE_HANDLE:
    return false;
  case TYPE_FD:
  case TYPE_FORWARDED:
    return true;
#else
  case TYPE_FD:
    {
      Interruptible(flags, fcntl(GetFD(), F_GETFL, 0));
      if (flags == -1)
	return false;
      else
	return (flags & O_NONBLOCK) != 0;
    }
#endif
  default:
    Error("invalid type");
  }
}

IODesc::result IODesc::CanInput(bool &out) {
  switch (GetType()) {
  case TYPE_CLOSED:
    return result_closed;
#if defined(__MINGW32__) || defined(_MSC_VER)
  case TYPE_HANDLE:
    out = true;
    return result_ok;
  case TYPE_FORWARDED:
#endif
  case TYPE_FD:
    out = IOHandler::IsReadable(GetFD());
    return result_ok;
  default:
    Error("invalid type");
  }
}

IODesc::result IODesc::CanOutput(bool &out) {
  switch (GetType()) {
  case TYPE_CLOSED:
    return result_closed;
#if defined(__MINGW32__) || defined(_MSC_VER)
  case TYPE_HANDLE:
    out = true;
    return result_ok;
  case TYPE_FORWARDED:
#endif
  case TYPE_FD:
    out = IOHandler::IsWritable(GetFD());
    return result_ok;
  default:
    Error("invalid type");
  }
}

IODesc::result IODesc::ReadNonblocking(u_char *buf, int n, int &out) {
  char *sys_buf = (char *) buf;
  Assert(n > 0);
  switch (GetType()) {
  case TYPE_CLOSED:
    return result_closed;
#if defined(__MINGW32__) || defined(_MSC_VER)
  case TYPE_FD:
  case TYPE_FORWARDED:
    out = recv(GetFD(), sys_buf, n, 0);
    return out == SOCKET_ERROR?
      (WSAGetLastError() == WSAEWOULDBLOCK?
       result_would_block: result_socket_error): result_ok;
  case TYPE_HANDLE:
    Error("non-blocking reads not supported for files");
#else
  case TYPE_FD:
    Interruptible(res, read(GetFD(), sys_buf, n));
    out = res;
    return res == -1?
      (errno == EWOULDBLOCK?
       result_would_block: result_system_error): result_ok;
#endif
  default:
    Error("invalid type");
  }
}

IODesc::result IODesc::WriteNonblocking(const u_char *buf, int n, int &out) {
  const char *sys_buf = (const char *) buf;
  Assert(n > 0);
  switch (GetType()) {
  case TYPE_CLOSED:
    return result_closed;
#if defined(__MINGW32__) || defined(_MSC_VER)
  case TYPE_FD:
  case TYPE_FORWARDED:
    out = send(GetFD(), sys_buf, n, 0);
    return out == SOCKET_ERROR?
      (WSAGetLastError() == WSAEWOULDBLOCK?
       result_would_block: result_socket_error): result_ok;
  case TYPE_HANDLE:
    Error("non-blocking writes not supported for files");
#else
  case TYPE_FD:
    Interruptible(res, write(GetFD(), sys_buf, n));
    out = res;
    return res == -1?
      (errno == EWOULDBLOCK?
       result_would_block: result_system_error): result_ok;
#endif
  default:
    Error("invalid type");
  }
}

//
// Initialization of IODescs for Standard Handles
//
#if defined(__MINGW32__) || defined(_MSC_VER)
static IODesc *MakeStdIODesc(const char *name, DWORD nStdHandle, u_int dir) {
  HANDLE hStd = GetStdHandle(nStdHandle);
  if (hStd == INVALID_HANDLE_VALUE) // assume that it has been closed
    return IODesc::NewClosed(String::New(name));
  return IODesc::NewForwarded(dir, String::New(name), hStd);
}

IODesc *IODesc::NewFromStdIn() {
  IODesc *ioDesc =
    MakeStdIODesc("stdIn", STD_INPUT_HANDLE, IODesc::DIR_READER);
  if (ioDesc->GetType() != IODesc::TYPE_CLOSED)
    IOHandler::SetDefaultBlockFD(ioDesc->GetFD());
  return ioDesc;
}
IODesc *IODesc::NewFromStdOut() {
  return MakeStdIODesc("stdOut", STD_OUTPUT_HANDLE, IODesc::DIR_WRITER);
}
IODesc *IODesc::NewFromStdErr() {
  return MakeStdIODesc("stdErr", STD_OUTPUT_HANDLE, IODesc::DIR_WRITER);
}
#else
static IODesc *MakeStdIODesc(const char *name, int fd, u_int dir) {
  // Try to make the file descriptor nonblocking:
  Interruptible(flags, fcntl(fd, F_GETFL, 0));
  if (flags == -1) {
    Assert(errno == EBADF);
    return IODesc::NewClosed(String::New(name));
  } else {
    Interruptible(res, fcntl(fd, F_SETFL, flags|O_NONBLOCK));
    res = res; // ignore result
    return IODesc::NewFromFD(dir, String::New(name), fd);
  }
}

IODesc *IODesc::NewFromStdIn() {
  IODesc *ioDesc = MakeStdIODesc("stdIn", 0, IODesc::DIR_READER);
  if (ioDesc->GetType() != IODesc::TYPE_CLOSED)
    IOHandler::SetDefaultBlockFD(ioDesc->GetFD());
  return ioDesc;
}
IODesc *IODesc::NewFromStdOut() {
  return MakeStdIODesc("stdOut", 1, IODesc::DIR_WRITER);
}
IODesc *IODesc::NewFromStdErr() {
  return MakeStdIODesc("stdErr", 2, IODesc::DIR_WRITER);
}
#endif
