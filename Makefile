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
UMBRELLA_BIN := neuro_lib_smoke

# Neuro static library (grows as more subsystems land .cpp impls).
# Each .cpp compiles into its own .o; we link them all into the
# executables. This keeps the rule list flat as we add subsystems.
NEURO_LIB_OBJS := neuro_thread.o neuro_process.o neuro_ws.o neuro_memfs.o neuro_overlayfs.o neuro_driver.o neuro_scene.o neuro_audio.o neuro_fabric.o neuro_pkg.o neuro_jit.o neuro_proof.o neuro_pulse.o neuro_learn.o neuro_bridge.o neuro_boot.o

neuro_thread.o: src/proc/thread.cpp \
                include/neuro/proc/thread.hpp \
                include/neuro/proc/process.hpp \
                include/neuro/sec/cap_space.hpp \
                include/neuro/core/kobject.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

neuro_process.o: src/proc/process.cpp \
                 include/neuro/proc/process.hpp \
                 include/neuro/sec/cap_space.hpp \
                 include/neuro/sec/epoch.hpp \
                 include/neuro/core/kobject.hpp \
                 include/neuro/core/endpoint.hpp \
                 include/neuro/mem/vma_tree.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

neuro_ws.o: src/sched/work_stealing.cpp \
           include/neuro/sched/work_stealing.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

neuro_memfs.o: src/fs/memfs.cpp \
               include/neuro/fs/memfs.hpp \
               include/neuro/fs/vfs.hpp \
               include/neuro/fs/vnode.hpp \
               include/neuro/core/result.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

neuro_overlayfs.o: src/fs/overlayfs.cpp \
                   include/neuro/fs/overlayfs.hpp \
                   include/neuro/fs/vfs.hpp \
                   include/neuro/fs/vnode.hpp \
                   include/neuro/core/result.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

neuro_driver.o: src/dev/driver.cpp \
                include/neuro/dev/driver.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

neuro_scene.o: src/ui/scene.cpp \
               include/neuro/ui/scene.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

neuro_audio.o: src/audio/pipeline.cpp \
               include/neuro/audio/pipeline.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

neuro_fabric.o: src/fabric/membership.cpp \
                include/neuro/fabric/membership.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

neuro_pkg.o: src/pkg/store.cpp \
             include/neuro/pkg/store.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

neuro_jit.o: src/jit/engine.cpp \
             include/neuro/jit/engine.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

neuro_proof.o: src/proof/contract.cpp \
               include/neuro/proof/contract.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

neuro_pulse.o: src/pulse/metric.cpp \
               include/neuro/pulse/metric.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

neuro_learn.o: src/learn/optimizer.cpp \
               include/neuro/learn/optimizer.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

neuro_bridge.o: src/bridge/ffi.cpp \
                include/neuro/bridge/ffi.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

neuro_boot.o: src/boot/protocol.cpp \
              include/neuro/boot/protocol.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

all: $(BIN) $(UMBRELLA_BIN)

$(BIN): src/scratch/main.cpp \
        $(NEURO_LIB_OBJS) \
        include/neuro/core/result.hpp \
        include/neuro/core/capability.hpp \
        include/neuro/core/endpoint.hpp \
        include/neuro/sched/scheduler.hpp \
        include/neuro/net/channel.hpp \
        include/neuro/mem/arena.hpp \
        include/neuro/proc/thread.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(NEURO_LIB_OBJS) -o $@

# Smoke test for the NeuroLib umbrella header — exercises one call
# from every subsystem. Built alongside the main demo.
$(UMBRELLA_BIN): src/scratch/neuro_lib_smoke.cpp \
                  $(NEURO_LIB_OBJS) \
                  include/neuro/neuro.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(NEURO_LIB_OBJS) -o $@

run: $(BIN)
	./$(BIN)

run-umbrella: $(UMBRELLA_BIN)
	./$(UMBRELLA_BIN)

# ---- Tests --------------------------------------------------------------
# Each .cpp file under tests/unit/<subsystem>/ is a standalone test binary
# that links only against the public headers under include/. As subsystems
# grow, add their tests here with the same pattern.

TEST_DIR := tests
TEST_INCLUDES := $(INCLUDES) -I.

SECURITY_TESTS := $(TEST_DIR)/unit/sec/capability_test
SECURITY_TESTS_BIN := $(addsuffix .bin,$(SECURITY_TESTS))

$(SECURITY_TESTS_BIN): $(SECURITY_TESTS).cpp \
                       include/neuro/core/capability.hpp \
                       include/neuro/sec/cap_space.hpp \
                       include/neuro/sec/epoch.hpp \
                       include/neuro/sec/cap_ops.hpp \
                       $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

MEM_TESTS := $(TEST_DIR)/unit/mem/arena_test \
             $(TEST_DIR)/unit/mem/pool_test
MEM_TESTS_BIN := $(addsuffix .bin,$(MEM_TESTS))

$(MEM_TESTS_BIN): $(TEST_DIR)/test_framework.hpp

$(TEST_DIR)/unit/mem/arena_test.bin: $(TEST_DIR)/unit/mem/arena_test.cpp \
                                     include/neuro/mem/arena.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/mem/pool_test.bin: $(TEST_DIR)/unit/mem/pool_test.cpp \
                                    include/neuro/mem/pool.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

INTEGRATION_TESTS := $(TEST_DIR)/integration/sched_steal \
                    $(TEST_DIR)/integration/ipc_pingpong \
                    $(TEST_DIR)/integration/net_echo \
                    $(TEST_DIR)/integration/fs_memfs
INTEGRATION_TESTS_BIN := $(addsuffix .bin,$(INTEGRATION_TESTS))

$(TEST_DIR)/integration/sched_steal.bin: $(TEST_DIR)/integration/sched_steal.cpp \
                                          include/neuro/sched/work_stealing.hpp \
                                          $(NEURO_LIB_OBJS) \
                                          $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< $(NEURO_LIB_OBJS) -o $@

# ipc_pingpong is header-only on the host — no NEURO_LIB_OBJS needed.
$(TEST_DIR)/integration/ipc_pingpong.bin: $(TEST_DIR)/integration/ipc_pingpong.cpp \
                                           include/neuro/ipc/endpoint.hpp \
                                           include/neuro/ipc/endpoint_pair.hpp \
                                           include/neuro/ipc/message.hpp \
                                           $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

# net_echo is header-only on the host — no NEURO_LIB_OBJS needed.
$(TEST_DIR)/integration/net_echo.bin: $(TEST_DIR)/integration/net_echo.cpp \
                                       include/neuro/net/address.hpp \
                                       include/neuro/net/buffer.hpp \
                                       include/neuro/net/dns.hpp \
                                       include/neuro/net/tcp_socket.hpp \
                                       include/neuro/net/udp_socket.hpp \
                                       $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

# fs_memfs links the MemFS + OverlayFS implementation objects.
$(TEST_DIR)/integration/fs_memfs.bin: $(TEST_DIR)/integration/fs_memfs.cpp \
                                       include/neuro/fs/memfs.hpp \
                                       include/neuro/fs/overlayfs.hpp \
                                       include/neuro/fs/vfs.hpp \
                                       include/neuro/fs/vnode.hpp \
                                       include/neuro/core/result.hpp \
                                       neuro_memfs.o neuro_overlayfs.o \
                                       $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_memfs.o neuro_overlayfs.o -o $@

test: $(SECURITY_TESTS_BIN) $(MEM_TESTS_BIN) $(INTEGRATION_TESTS_BIN)
	@for t in $(SECURITY_TESTS_BIN) $(MEM_TESTS_BIN) $(INTEGRATION_TESTS_BIN); do \
	    echo "==> $$t"; \
	    ./$$t || exit $$?; \
	done

clean:
	rm -f $(BIN) $(UMBRELLA_BIN)
	rm -f $(NEURO_LIB_OBJS)
	rm -f $(SECURITY_TESTS_BIN) $(MEM_TESTS_BIN) $(INTEGRATION_TESTS_BIN)

.PHONY: all run test clean