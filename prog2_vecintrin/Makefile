# VW overrides VECTOR_WIDTH from the command line, e.g. `make clean && make VW=8`.
# With VW unset the build is exactly as shipped and the width comes from the
# #define in CS149intrin.h. Every object must see the same width, so VWFLAG is
# applied to all three compilation units.
VW ?=
VWFLAG = $(if $(VW),-DVECTOR_WIDTH=$(VW))

all: myexp

logger.o: logger.cpp logger.h CS149intrin.h CS149intrin.cpp
	g++ $(VWFLAG) -c logger.cpp

CS149intrin.o: CS149intrin.cpp CS149intrin.h logger.cpp logger.h
	g++ $(VWFLAG) -c CS149intrin.cpp

myexp: CS149intrin.o logger.o main.cpp
	g++ $(VWFLAG) -I../common logger.o CS149intrin.o main.cpp -o myexp

clean:
	rm -f *.o myexp *~
