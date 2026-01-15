sources = files (
    'menuedit.c',
    'properties.c'
)

add_global_arguments('-Wno-unused-result', language : 'c')

gtk = dependency ('gtk+-3.0')
menucache = dependency('libmenu-cache')
libxml = dependency('libxml-2.0')
deps = [ gtk, menucache, libxml ]

executable (meson.project_name(), sources, dependencies: deps, install: true)
