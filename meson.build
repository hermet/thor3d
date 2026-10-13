project('thor3d',
        'cpp',
        version : '0.1.0',
        default_options : ['cpp_std=c++17', 'buildtype=debugoptimized'])

toolkit_dep = dependency('thorvg-toolkit')

executable(meson.project_name(),
           'main.cpp',
           dependencies : toolkit_dep)
