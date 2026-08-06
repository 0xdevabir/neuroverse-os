# meson.build — NeuroVerse OS top-level build
# Mirrors the README §5 toolchain: clang++ 17+, meson, mold.

project(
  'neuroverse',
  'cpp',
  version : '0.0.0',
  license : 'MIT/Apache-2.0',
  default_options : [
    'cpp_std=c++20',
    'warning_level=2',
    'b_ndebug=false',
    'buildtype=debugoptimized',
  ],
)

# C++ compiler:
# - clang++ 17+ is preferred (per README §5).
# - gcc 14+ is tested.
cc = meson.get_compiler('cpp')
if cc.get_id() != 'clang' and cc.get_id() != 'gcc'
  warning('Compiler is not clang or gcc; behavior is untested.')
endif

# Compiler flags.
add_project_arguments(
  cc.get_supported_arguments([
    '-Wall',
    '-Wextra',
    '-Wpedantic',
    '-Wshadow',
    '-Wnon-virtual-dtor',
    '-Wold-style-cast',
    '-Wcast-align',
    '-Wunused',
    '-Woverloaded-virtual',
    '-Wconversion',
    '-Wsign-conversion',
    '-Wnull-dereference',
    '-Wdouble-promotion',
    '-Wformat=2',
  ]),
  language : 'cpp',
)

# Threading.
thread_dep = dependency('threads')

# Include path.
incdir = include_directories('include')

# Subdirectories.
subdir('src')

# Tests (placeholder; populated when tests/ is added).
subdir('tests')
