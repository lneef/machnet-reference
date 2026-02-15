project('dpdk-perf', 'cpp',
  version: '0.1.0',
  default_options: ['warning_level=2', 'cpp_std=c++23', 'buildtype=debugoptimized'])

cmake = import('cmake')
tlx_proj = cmake.subproject('tlx')
tlx_dep = tlx_proj.dependency('tlx')
opt = cmake.subproject_options()
opt.add_cmake_defines({'CMAKE_POLICY_VERSION_MINIMUM': '3.5'})
machnet_proj = cmake.subproject('machnet', options: opt)
machnet_dep = machnet_proj.dependency('machnet_shim')
gflags_dep = dependency('gflags', required: true)

executable('client_machnet', 'client.cc', dependencies: [machnet_dep, tlx_dep, gflags_dep])
executable('server_machnet', 'server.cc', dependencies: [machnet_dep, tlx_dep, gflags_dep])

