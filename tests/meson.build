gtest_dep = dependency('gtest', main: true, disabler: true, required: false)
gmock_dep = dependency('gmock', disabler: true, required: false)
if not gtest_dep.found() or not gmock_dep.found()
    gtest_proj = import('cmake').subproject('googletest', required: false)
    if gtest_proj.found()
        gtest_dep = declare_dependency(
            dependencies: [
                dependency('threads'),
                gtest_proj.dependency('gtest'),
                gtest_proj.dependency('gtest_main'),
            ]
        )
        gmock_dep = gtest_proj.dependency('gmock')
    else
        assert(
            not get_option('tests').enabled(),
            'Googletest is required if tests are enabled'
        )
    endif
endif

ut_deps_list = [
    gtest_dep,
    gmock_dep,
]

ut_deps_list += default_deps

test(
    'test_utils',
    executable(
        'test_utils',
        'test_Utils.cpp',
        '../src/Utils.cpp',
        dependencies: ut_deps_list,
        implicit_include_directories: false,
        include_directories: '../src',
    )
)

if run_command('/usr/bin/bash', '-c', 'busctl tree xyz.openbmc_project.ObjectMapper').returncode() == 0
    test(
        'test_nvme',
        executable(
            'test_nvme',
            'test_nvme_mi.cpp',
            '../src/NVMeSensor.cpp',
            '../src/NVMeSubsys.cpp',
            '../src/NVMeController.cpp',
            cpp_args: uring_args + ['-UBOOST_ASIO_NO_DEPRECATED', '-UBOOST_ASIO_DISABLE_THREADS', '-UBOOST_ASIO_HAS_IO_URING'],
            dependencies: [ut_deps_list, nvme_deps],
            implicit_include_directories: false,
            include_directories: '../src',
        )
    )
endif
