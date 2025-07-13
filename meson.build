project(
    'doca-stdexec',
    'cpp',
    version: '0.1.0',
    default_options: ['cpp_std=c++23', 'cpp_args=-DDOCA_ALLOW_EXPERIMENTAL_API'],
)

doca_dependencies = [
    dependency('doca-argp'),
    dependency('doca-aes-gcm'),
    dependency('doca-comch'),
    dependency('doca-common'),
    dependency('doca-dma'),
    dependency('doca-rdma'),
    dependency('doca-sha'),
]

inc = [include_directories('stdexec/include'), include_directories('/usr/lib/gcc/x86_64-linux-gnu/12/include/')]

app_dep = [
]

app_dep += doca_dependencies

subdir('include')
subdir('test')

doca_stdexec_dep = declare_dependency(
    include_directories: inc,
    dependencies: app_dep,
)