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
NEURO_LIB_OBJS := neuro_thread.o neuro_process.o neuro_ws.o neuro_memfs.o neuro_ramfs.o neuro_overlayfs.o neuro_driver.o neuro_scene.o neuro_audio.o neuro_fabric.o neuro_pkg.o neuro_jit.o neuro_x86_64.o neuro_proof.o neuro_pulse.o neuro_learn.o neuro_bridge.o neuro_boot.o

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

neuro_ramfs.o: src/fs/ramfs.cpp \
              include/neuro/fs/ramfs.hpp \
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
             include/neuro/jit/engine.hpp \
             include/neuro/jit/x86_64.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

neuro_x86_64.o: src/jit/x86_64.cpp \
                include/neuro/jit/x86_64.hpp \
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

SECURITY_TESTS := $(TEST_DIR)/unit/sec/capability_test \
                  $(TEST_DIR)/unit/sec/cap_space_test \
                  $(TEST_DIR)/unit/sec/epoch_test \
                  $(TEST_DIR)/unit/sec/cap_ops_test
SECURITY_TESTS_BIN := $(addsuffix .bin,$(SECURITY_TESTS))

$(TEST_DIR)/unit/sec/capability_test.bin: $(TEST_DIR)/unit/sec/capability_test.cpp \
                                          include/neuro/core/capability.hpp \
                                          include/neuro/sec/cap_space.hpp \
                                          include/neuro/sec/epoch.hpp \
                                          include/neuro/sec/cap_ops.hpp \
                                          $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/sec/cap_space_test.bin: $(TEST_DIR)/unit/sec/cap_space_test.cpp \
                                          include/neuro/core/capability.hpp \
                                          include/neuro/sec/cap_space.hpp \
                                          $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/sec/epoch_test.bin: $(TEST_DIR)/unit/sec/epoch_test.cpp \
                                      include/neuro/sec/epoch.hpp \
                                      $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/sec/cap_ops_test.bin: $(TEST_DIR)/unit/sec/cap_ops_test.cpp \
                                       include/neuro/sec/cap_ops.hpp \
                                       include/neuro/sec/cap_space.hpp \
                                       include/neuro/sec/epoch.hpp \
                                       include/neuro/core/capability.hpp \
                                       $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

MEM_TESTS := $(TEST_DIR)/unit/mem/arena_test \
             $(TEST_DIR)/unit/mem/pool_test \
             $(TEST_DIR)/unit/mem/vma_tree_test \
             $(TEST_DIR)/unit/mem/page_table_test
MEM_TESTS_BIN := $(addsuffix .bin,$(MEM_TESTS))

$(MEM_TESTS_BIN): $(TEST_DIR)/test_framework.hpp

$(TEST_DIR)/unit/mem/arena_test.bin: $(TEST_DIR)/unit/mem/arena_test.cpp \
                                     include/neuro/mem/arena.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/mem/pool_test.bin: $(TEST_DIR)/unit/mem/pool_test.cpp \
                                    include/neuro/mem/pool.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/mem/vma_tree_test.bin: $(TEST_DIR)/unit/mem/vma_tree_test.cpp \
                                        include/neuro/mem/vma_tree.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/mem/page_table_test.bin: $(TEST_DIR)/unit/mem/page_table_test.cpp \
                                          include/neuro/mem/page_table.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

PKG_TESTS := $(TEST_DIR)/unit/pkg/sha3_test \
             $(TEST_DIR)/unit/pkg/sha3_family_test \
             $(TEST_DIR)/unit/pkg/manifest_test \
             $(TEST_DIR)/unit/pkg/digest_test
BOOT_TESTS := $(TEST_DIR)/unit/boot/protocol_test
PKG_TESTS_BIN := $(addsuffix .bin,$(PKG_TESTS))
BOOT_TESTS_BIN := $(addsuffix .bin,$(BOOT_TESTS))

# sha3_test is header-only (sha3.hpp is inline).
$(TEST_DIR)/unit/pkg/sha3_test.bin: $(TEST_DIR)/unit/pkg/sha3_test.cpp \
                                    include/neuro/pkg/sha3.hpp \
                                    include/neuro/pkg/store.hpp \
                                    $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

# sha3_family_test covers SHA3-224/256/384/512 + SHAKE128/256.
$(TEST_DIR)/unit/pkg/sha3_family_test.bin: $(TEST_DIR)/unit/pkg/sha3_family_test.cpp \
                                           include/neuro/pkg/sha3.hpp \
                                           include/neuro/pkg/digest.hpp \
                                           $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

# manifest_test exercises Manifest::canonicalise/content_root/verify;
# it links HostStore so we can do end-to-end ingest + verify.
$(TEST_DIR)/unit/pkg/manifest_test.bin: $(TEST_DIR)/unit/pkg/manifest_test.cpp \
                                        include/neuro/pkg/store.hpp \
                                        include/neuro/pkg/sha3.hpp \
                                        neuro_pkg.o \
                                        $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_pkg.o -o $@

# digest_test covers DigestN aliases, ordering and hex encoding.
$(TEST_DIR)/unit/pkg/digest_test.bin: $(TEST_DIR)/unit/pkg/digest_test.cpp \
                                      include/neuro/pkg/digest.hpp \
                                      $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

# boot signing/verification tests link the boot subsystem.
$(TEST_DIR)/unit/boot/protocol_test.bin: $(TEST_DIR)/unit/boot/protocol_test.cpp \
                                          include/neuro/boot/protocol.hpp \
                                          include/neuro/pkg/sha3.hpp \
                                          neuro_boot.o \
                                          $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_boot.o -o $@

# jit/x86_64 encoder tests verify byte-level encoding; they link
# the stub encoder implementation, which is a no-op on non-x86_64
# hosts but still provides the symbols.
JIT_TESTS := $(TEST_DIR)/unit/jit/x86_64_test \
             $(TEST_DIR)/unit/jit/engine_test
JIT_TESTS_BIN := $(addsuffix .bin,$(JIT_TESTS))

$(TEST_DIR)/unit/jit/x86_64_test.bin: $(TEST_DIR)/unit/jit/x86_64_test.cpp \
                                       include/neuro/jit/x86_64.hpp \
                                       include/neuro/jit/engine.hpp \
                                       neuro_x86_64.o \
                                       $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_x86_64.o -o $@

# engine_test exercises the full IR → codegen → (execute) pipeline.
# Links the engine + x86_64 backend objects so execute() works on
# x86_64 hosts; on other hosts the test only verifies bytes/disasm.
$(TEST_DIR)/unit/jit/engine_test.bin: $(TEST_DIR)/unit/jit/engine_test.cpp \
                                      include/neuro/jit/engine.hpp \
                                      include/neuro/jit/x86_64.hpp \
                                      neuro_x86_64.o neuro_jit.o \
                                      $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_x86_64.o neuro_jit.o -o $@

# learn/optimizer tests link the gradient-descent optimizer impl.
LEARN_TESTS := $(TEST_DIR)/unit/learn/optimizer_test
LEARN_TESTS_BIN := $(addsuffix .bin,$(LEARN_TESTS))

$(TEST_DIR)/unit/learn/optimizer_test.bin: $(TEST_DIR)/unit/learn/optimizer_test.cpp \
                                           include/neuro/learn/optimizer.hpp \
                                           neuro_learn.o \
                                           $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_learn.o -o $@

# pulse/metric tests link the registry + counter + gauge + histogram
# impls. Threaded tests need pthread (already in CXXFLAGS).
PULSE_TESTS := $(TEST_DIR)/unit/pulse/metric_test
PULSE_TESTS_BIN := $(addsuffix .bin,$(PULSE_TESTS))

$(TEST_DIR)/unit/pulse/metric_test.bin: $(TEST_DIR)/unit/pulse/metric_test.cpp \
                                        include/neuro/pulse/metric.hpp \
                                        neuro_pulse.o \
                                        $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_pulse.o -o $@

# fabric/membership tests link the SWIM-style gossip impl.
FABRIC_TESTS := $(TEST_DIR)/unit/fabric/membership_test
FABRIC_TESTS_BIN := $(addsuffix .bin,$(FABRIC_TESTS))

$(TEST_DIR)/unit/fabric/membership_test.bin: $(TEST_DIR)/unit/fabric/membership_test.cpp \
                                             include/neuro/fabric/membership.hpp \
                                             neuro_fabric.o \
                                             $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_fabric.o -o $@

# bridge/ffi tests use the symbol-table impl. Need neuro_bridge.o
# for the host_bridge() singleton.
BRIDGE_TESTS := $(TEST_DIR)/unit/bridge/ffi_test \
                $(TEST_DIR)/unit/bridge/bridge_test
BRIDGE_TESTS_BIN := $(addsuffix .bin,$(BRIDGE_TESTS))

$(TEST_DIR)/unit/bridge/ffi_test.bin: $(TEST_DIR)/unit/bridge/ffi_test.cpp \
                                      include/neuro/bridge/ffi.hpp \
                                      neuro_bridge.o \
                                      $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_bridge.o -o $@

$(TEST_DIR)/unit/bridge/bridge_test.bin: $(TEST_DIR)/unit/bridge/bridge_test.cpp \
                                        include/neuro/bridge/ffi.hpp \
                                        $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

# ui/scene tests link the flattener impl.
UI_TESTS := $(TEST_DIR)/unit/ui/scene_test
UI_TESTS_BIN := $(addsuffix .bin,$(UI_TESTS))

$(TEST_DIR)/unit/ui/scene_test.bin: $(TEST_DIR)/unit/ui/scene_test.cpp \
                                    include/neuro/ui/scene.hpp \
                                    neuro_scene.o \
                                    $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_scene.o -o $@

# audio/pipeline tests are header-only (Node/Graph are inline).
AUDIO_TESTS := $(TEST_DIR)/unit/audio/pipeline_test
AUDIO_TESTS_BIN := $(addsuffix .bin,$(AUDIO_TESTS))

$(TEST_DIR)/unit/audio/pipeline_test.bin: $(TEST_DIR)/unit/audio/pipeline_test.cpp \
                                          include/neuro/audio/pipeline.hpp \
                                          $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

# fs/vnode tests are header-only — MemVNode is defined inline in the
# test source itself.
FS_TESTS := $(TEST_DIR)/unit/fs/vnode_test \
            $(TEST_DIR)/unit/fs/vfs_test \
            $(TEST_DIR)/unit/fs/memfs_test \
            $(TEST_DIR)/unit/fs/ramfs_test \
            $(TEST_DIR)/unit/fs/overlayfs_test
FS_TESTS_BIN := $(addsuffix .bin,$(FS_TESTS))

$(TEST_DIR)/unit/fs/vnode_test.bin: $(TEST_DIR)/unit/fs/vnode_test.cpp \
                                    include/neuro/fs/vnode.hpp \
                                    include/neuro/core/result.hpp \
                                    $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

# vfs_test provides its own LinearVFS, so no separate fs/*.o is needed.
$(TEST_DIR)/unit/fs/vfs_test.bin: $(TEST_DIR)/unit/fs/vfs_test.cpp \
                                  include/neuro/fs/vfs.hpp \
                                  include/neuro/fs/vnode.hpp \
                                  include/neuro/core/result.hpp \
                                  $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/fs/memfs_test.bin: $(TEST_DIR)/unit/fs/memfs_test.cpp \
                                    include/neuro/fs/memfs.hpp \
                                    include/neuro/fs/vfs.hpp \
                                    include/neuro/fs/vnode.hpp \
                                    include/neuro/core/result.hpp \
                                    neuro_memfs.o \
                                    $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_memfs.o -o $@

$(TEST_DIR)/unit/fs/ramfs_test.bin: $(TEST_DIR)/unit/fs/ramfs_test.cpp \
                                   include/neuro/fs/ramfs.hpp \
                                   include/neuro/fs/vfs.hpp \
                                   include/neuro/fs/vnode.hpp \
                                   include/neuro/core/result.hpp \
                                   neuro_ramfs.o \
                                   $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_ramfs.o -o $@

$(TEST_DIR)/unit/fs/overlayfs_test.bin: $(TEST_DIR)/unit/fs/overlayfs_test.cpp \
                                        include/neuro/fs/overlayfs.hpp \
                                        include/neuro/fs/memfs.hpp \
                                        include/neuro/fs/vfs.hpp \
                                        include/neuro/fs/vnode.hpp \
                                        include/neuro/core/result.hpp \
                                        neuro_overlayfs.o neuro_memfs.o \
                                        $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_overlayfs.o neuro_memfs.o -o $@

# proc/thread tests link the Thread + Process implementations.
PROC_TESTS := $(TEST_DIR)/unit/proc/thread_test \
              $(TEST_DIR)/unit/proc/process_test \
              $(TEST_DIR)/unit/proc/process_id_test
PROC_TESTS_BIN := $(addsuffix .bin,$(PROC_TESTS))

$(TEST_DIR)/unit/proc/thread_test.bin: $(TEST_DIR)/unit/proc/thread_test.cpp \
                                       include/neuro/proc/thread.hpp \
                                       include/neuro/proc/process.hpp \
                                       neuro_thread.o neuro_process.o \
                                       $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_thread.o neuro_process.o -o $@

# proc/process tests link the Process implementation (the constructor
# body lives in src/proc/process.cpp).
$(TEST_DIR)/unit/proc/process_test.bin: $(TEST_DIR)/unit/proc/process_test.cpp \
                                       include/neuro/proc/process.hpp \
                                       neuro_process.o \
                                       $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_process.o neuro_thread.o -o $@

$(TEST_DIR)/unit/proc/process_id_test.bin: $(TEST_DIR)/unit/proc/process_id_test.cpp \
                                           include/neuro/proc/process.hpp \
                                           include/neuro/proc/thread.hpp \
                                           include/neuro/sec/cap_space.hpp \
                                           include/neuro/sec/epoch.hpp \
                                           include/neuro/mem/vma_tree.hpp \
                                           include/neuro/core/endpoint.hpp \
                                           include/neuro/core/kobject.hpp \
                                           neuro_process.o neuro_thread.o \
                                           $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_process.o neuro_thread.o -o $@

# core/io tests are header-only.
CORE_TESTS := $(TEST_DIR)/unit/core/io_test \
              $(TEST_DIR)/unit/core/span_test \
              $(TEST_DIR)/unit/core/irq_test \
              $(TEST_DIR)/unit/core/object_table_test \
              $(TEST_DIR)/unit/core/endpoint_test \
              $(TEST_DIR)/unit/core/result_test \
              $(TEST_DIR)/unit/core/kobject_test \
              $(TEST_DIR)/unit/core/capability_test \
              $(TEST_DIR)/unit/core/version_test
CORE_TESTS_BIN := $(addsuffix .bin,$(CORE_TESTS))

$(TEST_DIR)/unit/core/io_test.bin: $(TEST_DIR)/unit/core/io_test.cpp \
                                   include/neuro/core/io.hpp \
                                   include/neuro/core/capability.hpp \
                                   include/neuro/core/kobject.hpp \
                                   $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/core/span_test.bin: $(TEST_DIR)/unit/core/span_test.cpp \
                                    include/neuro/core/span.hpp \
                                    $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/core/irq_test.bin: $(TEST_DIR)/unit/core/irq_test.cpp \
                                   include/neuro/core/irq.hpp \
                                   include/neuro/core/kobject.hpp \
                                   $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/core/object_table_test.bin: $(TEST_DIR)/unit/core/object_table_test.cpp \
                                             include/neuro/core/object_table.hpp \
                                             include/neuro/core/kobject.hpp \
                                             $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/core/endpoint_test.bin: $(TEST_DIR)/unit/core/endpoint_test.cpp \
                                        include/neuro/core/endpoint.hpp \
                                        $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/core/result_test.bin: $(TEST_DIR)/unit/core/result_test.cpp \
                                      include/neuro/core/result.hpp \
                                      $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/core/kobject_test.bin: $(TEST_DIR)/unit/core/kobject_test.cpp \
                                       include/neuro/core/kobject.hpp \
                                       $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/core/version_test.bin: $(TEST_DIR)/unit/core/version_test.cpp \
                                             include/neuro/core/version.hpp \
                                             $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/core/capability_test.bin: $(TEST_DIR)/unit/core/capability_test.cpp \
                                          include/neuro/core/capability.hpp \
                                          $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

# Umbrella smoke test pulls in every subsystem header via neuro.hpp
# and exercises one tiny call from each. Heavy linking: every NEURO_LIB_OBJS
# is required because umbrella headers instantiate classes whose bodies
# live in src/.
UMBRELLA_TESTS := $(TEST_DIR)/unit/neuro_umbrella_test
UMBRELLA_TESTS_BIN := $(addsuffix .bin,$(UMBRELLA_TESTS))

$(TEST_DIR)/unit/neuro_umbrella_test.bin: $(TEST_DIR)/unit/neuro_umbrella_test.cpp \
                                          include/neuro/neuro.hpp \
                                          $(NEURO_LIB_OBJS) \
                                          $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< $(NEURO_LIB_OBJS) -o $@

# net/channel tests are header-only (Channel<T> is templated).
NET_TESTS := $(TEST_DIR)/unit/net/channel_test \
             $(TEST_DIR)/unit/net/address_buffer_dns_test \
             $(TEST_DIR)/unit/net/udp_socket_test \
             $(TEST_DIR)/unit/net/tcp_socket_test
NET_TESTS_BIN := $(addsuffix .bin,$(NET_TESTS))

$(TEST_DIR)/unit/net/channel_test.bin: $(TEST_DIR)/unit/net/channel_test.cpp \
                                        include/neuro/net/channel.hpp \
                                        $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/net/address_buffer_dns_test.bin: $(TEST_DIR)/unit/net/address_buffer_dns_test.cpp \
                                                  include/neuro/net/address.hpp \
                                                  include/neuro/net/buffer.hpp \
                                                  include/neuro/net/dns.hpp \
                                                  $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/net/udp_socket_test.bin: $(TEST_DIR)/unit/net/udp_socket_test.cpp \
                                          include/neuro/net/udp_socket.hpp \
                                          include/neuro/net/address.hpp \
                                          include/neuro/net/buffer.hpp \
                                          $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/net/tcp_socket_test.bin: $(TEST_DIR)/unit/net/tcp_socket_test.cpp \
                                          include/neuro/net/tcp_socket.hpp \
                                          include/neuro/net/address.hpp \
                                          include/neuro/net/buffer.hpp \
                                          $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

# proof/contract tests are header-only.
PROOF_TESTS := $(TEST_DIR)/unit/proof/contract_test
PROOF_TESTS_BIN := $(addsuffix .bin,$(PROOF_TESTS))

$(TEST_DIR)/unit/proof/contract_test.bin: $(TEST_DIR)/unit/proof/contract_test.cpp \
                                          include/neuro/proof/contract.hpp \
                                          $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

# dev/driver tests link the host Bus implementation.
DEV_TESTS := $(TEST_DIR)/unit/dev/driver_test
DEV_TESTS_BIN := $(addsuffix .bin,$(DEV_TESTS))

$(TEST_DIR)/unit/dev/driver_test.bin: $(TEST_DIR)/unit/dev/driver_test.cpp \
                                      include/neuro/dev/driver.hpp \
                                      neuro_driver.o \
                                      $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_driver.o -o $@

# ipc tests are header-only.
IPC_TESTS := $(TEST_DIR)/unit/ipc/endpoint_pair_test \
             $(TEST_DIR)/unit/ipc/message_test \
             $(TEST_DIR)/unit/ipc/endpoint_test
IPC_TESTS_BIN := $(addsuffix .bin,$(IPC_TESTS))

$(TEST_DIR)/unit/ipc/endpoint_pair_test.bin: $(TEST_DIR)/unit/ipc/endpoint_pair_test.cpp \
                                              include/neuro/ipc/endpoint_pair.hpp \
                                              include/neuro/ipc/message.hpp \
                                              $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/ipc/message_test.bin: $(TEST_DIR)/unit/ipc/message_test.cpp \
                                       include/neuro/ipc/message.hpp \
                                       $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/ipc/endpoint_test.bin: $(TEST_DIR)/unit/ipc/endpoint_test.cpp \
                                        include/neuro/ipc/endpoint.hpp \
                                        include/neuro/ipc/message.hpp \
                                        $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

# sched/work_stealing tests link the work-stealing scheduler impl.
SCHED_TESTS := $(TEST_DIR)/unit/sched/work_stealing_test \
               $(TEST_DIR)/unit/sched/multilevel_test \
               $(TEST_DIR)/unit/sched/scheduler_test \
               $(TEST_DIR)/unit/sched/deadline_test
SCHED_TESTS_BIN := $(addsuffix .bin,$(SCHED_TESTS))

$(TEST_DIR)/unit/sched/work_stealing_test.bin: $(TEST_DIR)/unit/sched/work_stealing_test.cpp \
                                               include/neuro/sched/work_stealing.hpp \
                                               neuro_ws.o \
                                               $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_ws.o -o $@

# multilevel_test exercises the multi-node facade. It uses
# neuro::sched::Scheduler (the per-worker-queue variant), which is
# fully header-only.
$(TEST_DIR)/unit/sched/multilevel_test.bin: $(TEST_DIR)/unit/sched/multilevel_test.cpp \
                                            include/neuro/sched/multilevel.hpp \
                                            $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/sched/scheduler_test.bin: $(TEST_DIR)/unit/sched/scheduler_test.cpp \
                                           include/neuro/sched/scheduler.hpp \
                                           $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

$(TEST_DIR)/unit/sched/deadline_test.bin: $(TEST_DIR)/unit/sched/deadline_test.cpp \
                                          include/neuro/sched/deadline.hpp \
                                          $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

INTEGRATION_TESTS := $(TEST_DIR)/integration/sched_steal \
                    $(TEST_DIR)/integration/ipc_pingpong \
                    $(TEST_DIR)/integration/ipc_backpressure \
                    $(TEST_DIR)/integration/net_echo \
                    $(TEST_DIR)/integration/fs_memfs \
                    $(TEST_DIR)/integration/cap_ipc \
                    $(TEST_DIR)/integration/endpoint_cap \
                    $(TEST_DIR)/integration/fs_cap \
                    $(TEST_DIR)/integration/net_cap \
                    $(TEST_DIR)/integration/sched_ipc \
                    $(TEST_DIR)/integration/dev_cap \
                    $(TEST_DIR)/integration/pulse_learn \
                    $(TEST_DIR)/integration/boot_sign_verify \
                    $(TEST_DIR)/integration/jit_bridge \
                    $(TEST_DIR)/integration/audio_region \
                    $(TEST_DIR)/integration/fabric_processes \
                    $(TEST_DIR)/integration/proof_ipc
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

# ipc_backpressure exercises the bounded queue + co_await recv +
# timed send/recv primitives. Also header-only.
$(TEST_DIR)/integration/ipc_backpressure.bin: $(TEST_DIR)/integration/ipc_backpressure.cpp \
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

# cap_ipc is header-only on the host — no NEURO_LIB_OBJS needed.
$(TEST_DIR)/integration/cap_ipc.bin: $(TEST_DIR)/integration/cap_ipc.cpp \
                                      include/neuro/ipc/endpoint.hpp \
                                      include/neuro/ipc/endpoint_pair.hpp \
                                      include/neuro/ipc/message.hpp \
                                      include/neuro/sec/cap_ops.hpp \
                                      include/neuro/sec/cap_space.hpp \
                                      include/neuro/sec/epoch.hpp \
                                      include/neuro/core/capability.hpp \
                                      $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

# endpoint_cap is header-only on the host — no NEURO_LIB_OBJS needed.
$(TEST_DIR)/integration/endpoint_cap.bin: $(TEST_DIR)/integration/endpoint_cap.cpp \
                                            include/neuro/ipc/endpoint.hpp \
                                            include/neuro/ipc/endpoint_pair.hpp \
                                            include/neuro/ipc/message.hpp \
                                            include/neuro/sec/cap_ops.hpp \
                                            include/neuro/sec/cap_space.hpp \
                                            include/neuro/sec/epoch.hpp \
                                            include/neuro/core/capability.hpp \
                                            $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

# fs_cap links the MemFS implementation object.
$(TEST_DIR)/integration/fs_cap.bin: $(TEST_DIR)/integration/fs_cap.cpp \
                                     include/neuro/fs/memfs.hpp \
                                     include/neuro/fs/vfs.hpp \
                                     include/neuro/fs/vnode.hpp \
                                     include/neuro/sec/cap_ops.hpp \
                                     include/neuro/sec/cap_space.hpp \
                                     include/neuro/sec/epoch.hpp \
                                     include/neuro/core/capability.hpp \
                                     include/neuro/core/result.hpp \
                                     neuro_memfs.o \
                                     $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_memfs.o -o $@

# net_cap is header-only on the host — no NEURO_LIB_OBJS needed.
$(TEST_DIR)/integration/net_cap.bin: $(TEST_DIR)/integration/net_cap.cpp \
                                       include/neuro/net/address.hpp \
                                       include/neuro/net/tcp_socket.hpp \
                                       include/neuro/sec/cap_ops.hpp \
                                       include/neuro/sec/cap_space.hpp \
                                       include/neuro/sec/epoch.hpp \
                                       include/neuro/core/capability.hpp \
                                       $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

# sched_ipc is header-only on the host — no NEURO_LIB_OBJS needed.
$(TEST_DIR)/integration/sched_ipc.bin: $(TEST_DIR)/integration/sched_ipc.cpp \
                                        include/neuro/ipc/endpoint.hpp \
                                        include/neuro/ipc/message.hpp \
                                        include/neuro/sched/scheduler.hpp \
                                        $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

# dev_cap links the driver implementation object.
$(TEST_DIR)/integration/dev_cap.bin: $(TEST_DIR)/integration/dev_cap.cpp \
                                      include/neuro/dev/driver.hpp \
                                      include/neuro/core/io.hpp \
                                      include/neuro/core/capability.hpp \
                                      include/neuro/core/kobject.hpp \
                                      neuro_driver.o \
                                      $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_driver.o -o $@

# pulse_learn links the learn implementation object.
$(TEST_DIR)/integration/pulse_learn.bin: $(TEST_DIR)/integration/pulse_learn.cpp \
                                           include/neuro/pulse/metric.hpp \
                                           include/neuro/learn/optimizer.hpp \
                                           neuro_learn.o \
                                           $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_learn.o -o $@

# boot_sign_verify links the boot implementation object.
$(TEST_DIR)/integration/boot_sign_verify.bin: $(TEST_DIR)/integration/boot_sign_verify.cpp \
                                                include/neuro/boot/protocol.hpp \
                                                include/neuro/core/capability.hpp \
                                                include/neuro/pkg/digest.hpp \
                                                neuro_boot.o neuro_pulse.o neuro_learn.o \
                                                $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_boot.o neuro_pulse.o neuro_learn.o -o $@

# jit_bridge links the JIT + Bridge implementation objects.
$(TEST_DIR)/integration/jit_bridge.bin: $(TEST_DIR)/integration/jit_bridge.cpp \
                                          include/neuro/jit/engine.hpp \
                                          include/neuro/jit/x86_64.hpp \
                                          include/neuro/bridge/ffi.hpp \
                                          neuro_jit.o neuro_x86_64.o \
                                          $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_jit.o neuro_x86_64.o -o $@

# audio_region links the audio implementation object.
$(TEST_DIR)/integration/audio_region.bin: $(TEST_DIR)/integration/audio_region.cpp \
                                            include/neuro/audio/pipeline.hpp \
                                            include/neuro/core/io.hpp \
                                            include/neuro/core/capability.hpp \
                                            neuro_audio.o \
                                            $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_audio.o -o $@

# fabric_processes exercises two Cluster instances end-to-end.
$(TEST_DIR)/integration/fabric_processes.bin: $(TEST_DIR)/integration/fabric_processes.cpp \
                                                include/neuro/fabric/membership.hpp \
                                                neuro_fabric.o \
                                                $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< neuro_fabric.o -o $@

# proof_ipc is header-only on the host — no NEURO_LIB_OBJS needed.
$(TEST_DIR)/integration/proof_ipc.bin: $(TEST_DIR)/integration/proof_ipc.cpp \
                                        include/neuro/ipc/message.hpp \
                                        include/neuro/proof/contract.hpp \
                                        $(TEST_DIR)/test_framework.hpp
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $< -o $@

test: $(SECURITY_TESTS_BIN) $(MEM_TESTS_BIN) $(PKG_TESTS_BIN) $(BOOT_TESTS_BIN) $(JIT_TESTS_BIN) $(LEARN_TESTS_BIN) $(PULSE_TESTS_BIN) $(FABRIC_TESTS_BIN) $(BRIDGE_TESTS_BIN) $(UI_TESTS_BIN) $(AUDIO_TESTS_BIN) $(PROC_TESTS_BIN) $(PROOF_TESTS_BIN) $(DEV_TESTS_BIN) $(FS_TESTS_BIN) $(SCHED_TESTS_BIN) $(CORE_TESTS_BIN) $(NET_TESTS_BIN) $(IPC_TESTS_BIN) $(UMBRELLA_TESTS_BIN) $(INTEGRATION_TESTS_BIN)
	@for t in $(SECURITY_TESTS_BIN) $(MEM_TESTS_BIN) $(PKG_TESTS_BIN) $(BOOT_TESTS_BIN) $(JIT_TESTS_BIN) $(LEARN_TESTS_BIN) $(PULSE_TESTS_BIN) $(FABRIC_TESTS_BIN) $(BRIDGE_TESTS_BIN) $(UI_TESTS_BIN) $(AUDIO_TESTS_BIN) $(PROC_TESTS_BIN) $(PROOF_TESTS_BIN) $(DEV_TESTS_BIN) $(FS_TESTS_BIN) $(SCHED_TESTS_BIN) $(CORE_TESTS_BIN) $(NET_TESTS_BIN) $(IPC_TESTS_BIN) $(UMBRELLA_TESTS_BIN) $(INTEGRATION_TESTS_BIN); do \
	    echo "==> $$t"; \
	    ./$$t || exit $$?; \
	done

clean:
	rm -f $(BIN) $(UMBRELLA_BIN)
	rm -f $(NEURO_LIB_OBJS)
	rm -f $(SECURITY_TESTS_BIN) $(MEM_TESTS_BIN) $(PKG_TESTS_BIN) $(BOOT_TESTS_BIN) $(JIT_TESTS_BIN) $(LEARN_TESTS_BIN) $(PULSE_TESTS_BIN) $(FABRIC_TESTS_BIN) $(BRIDGE_TESTS_BIN) $(UI_TESTS_BIN) $(AUDIO_TESTS_BIN) $(PROC_TESTS_BIN) $(PROOF_TESTS_BIN) $(DEV_TESTS_BIN) $(FS_TESTS_BIN) $(SCHED_TESTS_BIN) $(CORE_TESTS_BIN) $(NET_TESTS_BIN) $(IPC_TESTS_BIN) $(UMBRELLA_TESTS_BIN) $(INTEGRATION_TESTS_BIN)

.PHONY: all run test clean