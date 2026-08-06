// src/proc/process.cpp
//
// Process implementation.

#include "neuro/proc/process.hpp"

#include <utility>

namespace neuro::proc {

Process::Process(ProcessInit init)
    : core::KObject(core::KObjectKind::Untyped),  // no dedicated kind yet
      init_(std::move(init)) {}

}  // namespace neuro::proc