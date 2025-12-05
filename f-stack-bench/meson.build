project('tcp_cpu', ['cpp'], default_options : ['buildtype=release', 'cpp_std=c++20'])

fmt_dep = dependency('fmt')
fstack = dependency('fstack')

sources_m = files('net_bench.cpp')
executable('nb', sources_m, dependencies: [fmt_dep, fstack])
