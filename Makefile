##
## Author:
##   Leif Kornstaedt <kornstae@ps.uni-sb.de>
## 
## Copyright:
##   Leif Kornstaedt, 2000-2003
## 
## Last change:
##   $Date$ by $Author$
##   $Revision$
## 

TOPDIR = .

include $(TOPDIR)/Makefile.vars
include $(TOPDIR)/Makefile.rules

SUBDIRS = generic adt store

SOURCES = Base.cc InitSeam.cc SeamMain.cc
OBJS = $(SOURCES:%.cc=%.o)

##
## Enumerate seam.dll files
##
ifdef LIGHTNING
STORE_LIGHTNING_SOURCES = JITStore.cc
else
STORE_LIGHTNING_SOURCES =
endif
STORE_SOURCES = Map.cc WeakMap.cc Heap.cc Store.cc $(STORE_LIGHTNING_SOURCES)
STORE_OBJS = $(STORE_SOURCES:%.cc=store/%.o)
ADT_SOURCES = ChunkMap.cc Outline.cc
ADT_OBJS = $(ADT_SOURCES:%.cc=adt/%.o)
GENERIC_SOURCES = \
	Outline.cc Debug.cc RootSet.cc UniqueString.cc \
	StackFrame.cc TaskStack.cc IOHandler.cc IODesc.cc SignalHandler.cc \
	Scheduler.cc Transients.cc Worker.cc Interpreter.cc \
	Primitive.cc PushCallWorker.cc BindFutureWorker.cc \
	Unpickler.cc Pickler.cc Profiler.cc Broker.cc
GENERIC_OBJS = $(GENERIC_SOURCES:%.cc=generic/%.o)
SEAM_OBJS = $(STORE_OBJS) $(ADT_OBJS) $(GENERIC_OBJS)
##
## Done
##

LIBS = -L$(ZLIBDIR)/lib $(EXTRA_LIBS) -lz

.PHONY: $(SUBDIRS) clean-local veryclean-local distclean-local install

all: seam.exe

$(SUBDIRS): %:
	(cd $@ && $(MAKE) all)

seam.dll: Base.o InitSeam.o store adt generic
	$(LD) $(LDFLAGS) -shared -o $@ Base.o InitSeam.o $(SEAM_OBJS) $(LIBS)

seam.exe: SeamMain.o seam.dll
	$(LD) $(LDFLAGS) -o $@ $< seam.dll

clean: clean-local
	for i in $(SUBDIRS); do (cd $$i && $(MAKE) clean) || exit 1; done
veryclean: veryclean-local
	for i in $(SUBDIRS); do (cd $$i && $(MAKE) veryclean) || exit 1; done
distclean: distclean-local
	for i in $(SUBDIRS); do (cd $$i && $(MAKE) distclean) || exit 1; done

clean-local:
	rm -f $(OBJS)
veryclean-local: clean-local
	rm -f seam.dll
distclean-local: veryclean-local
	rm -f Makefile.depend

Makefile.depend: Makefile $(SOURCES) store/StoreConfig.hh
	$(MAKEDEPEND) $(SOURCES) > Makefile.depend

store/StoreConfig.hh:
	cd store && make StoreConfig.hh

include Makefile.depend
