sources = files (
    'menuedit.c'
)

add_global_arguments('-Wno-unused-result', language : 'c')

gtk = dependency ('gtk+-3.0')
menucache = dependency('libmenu-cache')
deps = [ gtk, menucache ]

executable (meson.project_name(), sources, dependencies: deps, install: true)
