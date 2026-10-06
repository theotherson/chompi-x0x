# Desktop builds of the x0x core (code/src/x0x): tests, and WAV renders for
# listening. Nothing here runs on the CHOMPI.
#
#   make -C host && host/tests && host/render host/out
CXX ?= clang++
CXXFLAGS ?= -std=c++14 -O2 -Wall -Wextra -Wno-unused-parameter
CORE := $(wildcard ../code/src/x0x/*.h)

all: tests render

tests: tests.cpp $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ tests.cpp

render: render.cpp wav.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ render.cpp

clean:
	rm -f tests render
