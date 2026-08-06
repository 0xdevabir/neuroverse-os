// neuro/neuro.hpp
//
// NeuroLib umbrella header.
//
// One #include that pulls in every public Neuro<Name> subsystem
// header. Intended for users who want the entire surface in scope
// (tests, demo programs, prototyping). Production code should
// continue to include only the headers it needs.
//
// Subsystem list mirrors README §4.

#pragma once

// ---- NeuroCore ----------------------------------------------------------
#include "neuro/core/capability.hpp"
#include "neuro/core/endpoint.hpp"
#include "neuro/core/result.hpp"

// ---- NeuroSec -----------------------------------------------------------
#include "neuro/sec/cap_ops.hpp"
#include "neuro/sec/cap_space.hpp"
#include "neuro/sec/epoch.hpp"

// ---- NeuroMem -----------------------------------------------------------
#include "neuro/mem/arena.hpp"
#include "neuro/mem/pool.hpp"
#include "neuro/mem/vma_tree.hpp"

// ---- NeuroProc / NeuroSched --------------------------------------------
#include "neuro/proc/process.hpp"
#include "neuro/proc/thread.hpp"
#include "neuro/sched/multilevel.hpp"
#include "neuro/sched/scheduler.hpp"
#include "neuro/sched/work_stealing.hpp"

// ---- NeuroIPC -----------------------------------------------------------
#include "neuro/ipc/endpoint.hpp"
#include "neuro/ipc/endpoint_pair.hpp"
#include "neuro/ipc/message.hpp"

// ---- NeuroNet -----------------------------------------------------------
#include "neuro/net/address.hpp"
#include "neuro/net/buffer.hpp"
#include "neuro/net/channel.hpp"
#include "neuro/net/dns.hpp"
#include "neuro/net/tcp_socket.hpp"
#include "neuro/net/udp_socket.hpp"

// ---- NeuroFS ------------------------------------------------------------
#include "neuro/fs/memfs.hpp"
#include "neuro/fs/overlayfs.hpp"
#include "neuro/fs/vfs.hpp"
#include "neuro/fs/vnode.hpp"

// ---- NeuroDev -----------------------------------------------------------
#include "neuro/dev/driver.hpp"

// ---- NeuroUI ------------------------------------------------------------
#include "neuro/ui/scene.hpp"

// ---- NeuroAudio ---------------------------------------------------------
#include "neuro/audio/pipeline.hpp"

// ---- NeuroFabric --------------------------------------------------------
#include "neuro/fabric/membership.hpp"

// ---- NeuroPkg -----------------------------------------------------------
#include "neuro/pkg/store.hpp"

// ---- NeuroJIT -----------------------------------------------------------
#include "neuro/jit/engine.hpp"

// ---- NeuroProof ---------------------------------------------------------
#include "neuro/proof/contract.hpp"

// ---- NeuroPulse ---------------------------------------------------------
#include "neuro/pulse/metric.hpp"

// ---- NeuroLearn ---------------------------------------------------------
#include "neuro/learn/optimizer.hpp"

// ---- NeuroBridge --------------------------------------------------------
#include "neuro/bridge/ffi.hpp"

// ---- NeuroBoot ----------------------------------------------------------
#include "neuro/boot/protocol.hpp"
