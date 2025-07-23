CXX = g++
CXXFLAGS = -O2 -std=c++17 \
  -I/Applications/CPLEX_Studio2211/concert/include \
  -I/Applications/CPLEX_Studio2211/cplex/include \
  -I/Applications/CPLEX_Studio2211/cpoptimizer/include

LDFLAGS = \
  -L/Applications/CPLEX_Studio2211/concert/lib/x86-64_osx/static_pic \
  -L/Applications/CPLEX_Studio2211/cplex/lib/x86-64_osx/static_pic \
  -L/Applications/CPLEX_Studio2211/cpoptimizer/lib/x86-64_osx/static_pic \
  -lconcert -lcplex -lilocplex -lcp

SOURCES = src/main.cpp
TARGET = solver

all:
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

clean:
	rm -f $(TARGET)
