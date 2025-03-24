CXX = g++
CXXFLAGS = -g -lwheel -std=c++17 -fPIC -Wall -Wextra -O0 -DDISABLE_ASYNC -fsanitize=address
LDFLAGS = -shared

# Detect platform
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S), Linux)  # Linux
	TEST_CMD = rm -f test_*
	TARGET = libdiskmap.so
    INSTALL_CMD = cp libdiskmap.so /usr/local/lib/ && cp *.h /usr/local/include/ && ldconfig
else # macOS
	TEST_CMD = rm -rf test_*/
	TARGET = libdiskmap.dylib
    INSTALL_CMD = cp libdiskmap.dylib /usr/local/lib/ && cp *.h /usr/local/include/
endif

# Source files
SRCS = $(wildcard *.cpp)

# Object files
OBJS = $(SRCS:.cpp=.o)

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
	$(test_CMD)
	rm -f *.o
	rm -f $(OBJS) $(TARGET)

# Install
install: $(TARGET)
	$(INSTALL_CMD)

# Phony targets
.PHONY: all clean install