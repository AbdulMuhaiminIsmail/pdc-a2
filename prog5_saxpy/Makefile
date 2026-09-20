CXX=g++ -m64
CXXFLAGS=-I../common -Iobjs/ -O2 -Wall 
ISPC=ispc
# note: requires AVX2
ISPCFLAGS=-O3 --target=avx2-i32x8 --arch=x86-64 --pic

# Optional task-count override for the granularity sweep (default 64 as shipped).
TASKS ?=
TASKFLAG = $(if $(TASKS),-DSAXPY_TASKS=$(TASKS))
ISPCFLAGS += $(TASKFLAG)

APP_NAME=saxpy
OBJDIR=objs
COMMONDIR=../common

TASKSYS_CXX=$(COMMONDIR)/tasksys.cpp
TASKSYS_LIB=-lpthread
TASKSYS_OBJ=$(addprefix $(OBJDIR)/, $(subst $(COMMONDIR)/,, $(TASKSYS_CXX:.cpp=.o)))

default: $(APP_NAME)

.PHONY: dirs clean

dirs:
		/bin/mkdir -p $(OBJDIR)/

clean:
		/bin/rm -rf $(OBJDIR) *.ppm *~ $(APP_NAME) saxpy_experiments

OBJS=$(OBJDIR)/main.o $(OBJDIR)/saxpySerial.o $(OBJDIR)/saxpy_ispc.o $(TASKSYS_OBJ)

$(APP_NAME): dirs $(OBJS)
		$(CXX) $(CXXFLAGS) -o $@ $(OBJS) -lm $(TASKSYS_LIB)

$(OBJDIR)/%.o: %.cpp
		$(CXX) $< $(CXXFLAGS) -c -o $@

$(OBJDIR)/%.o: $(COMMONDIR)/%.cpp
	$(CXX) $< $(CXXFLAGS) -c -o $@

$(OBJDIR)/main.o: $(OBJDIR)/$(APP_NAME)_ispc.h $(COMMONDIR)/CycleTimer.h

$(OBJDIR)/%_ispc.h $(OBJDIR)//%_ispc.o: %.ispc
		$(ISPC) $(ISPCFLAGS) $< -o $(OBJDIR)/$*_ispc.o -h $(OBJDIR)/$*_ispc.h


# Bandwidth study: thread sweep and non-temporal-store variant (extra credit).
# Reuses the ISPC object so the ISPC numbers come from the same code path the
# shipped program measures.
saxpy_experiments: dirs $(OBJDIR)/saxpy_ispc.o $(TASKSYS_OBJ) saxpy_experiments.cpp saxpySerial.cpp
		$(CXX) saxpy_experiments.cpp saxpySerial.cpp $(OBJDIR)/saxpy_ispc.o $(TASKSYS_OBJ) \
		  $(CXXFLAGS) -march=native -std=c++11 -o $@ -lm $(TASKSYS_LIB)
