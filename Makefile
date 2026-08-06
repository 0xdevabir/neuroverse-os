# Minimal host build for the starter scaffold.
# Mirrors README §9.8.

CXX      ?= clang++
# README §1 / §5 specifies C++20/23. We use C++23 because std::expected is a
# C++23 feature and is required by include/neuro/core/result.hpp. Coroutines
# are part of C++20 on clang 16+; the auto-detect below keeps older toolchains
# working with -fcoroutines-ts.
CXXSTD   ?= c++23
CXXFLAGS ?= -std=$(CXXSTD) -O2 -Wall -Wextra -Wpedantic -pthread
INCLUDES := -Iinclude

# Detect if -fcoroutines-ts is still required (clang < 16).
CORO_FLAG := $(shell $(CXX) -fcoroutines-ts -E -x c++ /dev/null >/dev/null 2>&1 && echo -fcoroutines-ts || true)
ifneq ($(CORO_FLAG),)
CXXFLAGS += $(CORO_FLAG)
endif

BIN := neuro_scratch

all: $(BIN)

$(BIN): src/scratch/main.cpp \
        include/neuro/core/result.hpp \
        include/neuro/core/capability.hpp \
        include/neuro/core/endpoint.hpp \
        include/neuro/sched/scheduler.hpp \
        include/neuro/net/channel.hpp \
        include/neuro/mem/arena.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)

.PHONY: all run clean