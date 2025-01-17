CXX = g++
CXXFLAGS = -g -lwheel -std=c++17 -fPIC -Wall -Wextra -O2 -DDISABLE_ASYNC
LDFLAGS = -shared

# Source files
SRCS = $(wildcard *.cpp)

# Object files
OBJS = $(SRCS:.cpp=.o)

# Target library
TARGET = libdiskmap.so

# Default target
all: $(TARGET)

test: $(patsubst tests/%.cpp,%,$(wildcard tests/*.cpp)) $(OBJS)

%: tests/%.cpp $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(OBJS)

# Rule to create shared library
$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

# Rule to compile source files to object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up
clean:
	rm -f test_*
	rm -f *.o
	rm -f $(OBJS) $(TARGET)

# Install
install:
	cp libdiskmap.so /usr/local/lib/
	cp *.h /usr/local/include/
	ldconfig

# Phony targets
.PHONY: all clean install