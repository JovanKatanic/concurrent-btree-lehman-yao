MAKEFLAGS += -j$(nproc)
CXX = g++
CC = gcc
BASE_CXXFLAGS = -Wall -Wextra -std=c++20 -Iinclude -Itest -march=native #-flto #move to link flags
LDFLAGS = -lxxhash -lfmt -ljemalloc -lutf8proc 

BUILD ?= release

ifeq ($(BUILD),debug)
    CXXFLAGS = $(BASE_CXXFLAGS) -g -O0 -DDB7_DEBUG_FLAG  -fno-inline -fsanitize=thread
else
    CXXFLAGS = $(BASE_CXXFLAGS) -O2 -DNDB7_DEBUG_FLAG 
endif

BIN_DIR := bin
OBJ_DIR := obj
TARGET := $(BIN_DIR)/app

# Auto-discover all .cpp files under PATH_SRC/
PATH_SRC := test 
CXX_SRCS := $(shell find $(PATH_SRC) -name '*.cpp'  -not -path '*/third_party/*')
CXX_OBJS := $(patsubst $(PATH_SRC)/%.cpp,$(OBJ_DIR)/%.o,$(CXX_SRCS))

OBJS := $(CXX_OBJS)

all: $(TARGET)

$(OBJ_DIR)/%.o: $(PATH_SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

clean-force:
	rm -rf $(OBJ_DIR) $(CACHE_OBJ_DIR) $(BIN_DIR)
	
install:
	sudo apt install -y liburing-dev libxxhash-dev libfmt-dev build-essential libjemalloc-dev libutf8proc-dev

.PHONY: all clean run install