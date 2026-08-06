# src/meson.build — library and executables under src/

# Public library target (headers under include/neuro/ are the public API).
# Implementations live under src/<subsystem>/. Eventually each subsystem
# becomes its own static library linked into the OS image.
neuro_lib = static_library(
  'neuro',
  include_directories : incdir,
  sources : [
    'proc/thread.cpp',
    'proc/process.cpp',
    'sched/work_stealing.cpp',
  ],
  gnu_symbol_visibility : 'hidden',
)

# Scratch / demo executable — exercises every starter subsystem.
neuro_scratch_exe = executable(
  'neuro_scratch',
  'scratch/main.cpp',
  include_directories : incdir,
  dependencies : [thread_dep],
  link_with : neuro_lib,
  install : false,
)
