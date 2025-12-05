project('tcp_cpu', ['cpp'], default_options : ['buildtype=release', 'cpp_std=c++20'])

fmt_dep = dependency('fmt')
fstack = declare_dependency(
  link_args : ['/usr/local/lib/libfstack.a'],
  include_directories : include_directories('/usr/local/include')
)
syslibs = declare_dependency(
  link_args : [
    '-lrt',
    '-lm',
    '-ldl',
    '-lcrypto',
    '-pthread',
    '-lnuma',
    '-lcap'
  ]
)
dpdk_dep = dependency('libdpdk', method: 'pkg-config', required: true, static: true)
sources_m = files('net_bench.cpp')
executable('nb', sources_m, dependencies: [fmt_dep, fstack, dpdk_dep, syslibs])
