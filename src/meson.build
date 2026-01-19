sources = files (
    'menuedit.c',
    'properties.c'
)

add_global_arguments('-Wno-unused-result', language : 'c')

gtk = dependency ('gtk+-3.0')
menucache = dependency('libmenu-cache')
libxml = dependency('libxml-2.0')
deps = [ gtk, menucache, libxml ]

if build_plugin
  shared_module(plugin_name, sources, dependencies: deps, install: true,
    install_dir: get_option('libdir') / 'rpcc',
    c_args : [ '-DPACKAGE_DATA_DIR="' + presource_dir + '"', '-DGETTEXT_PACKAGE="' + plugin_name + '"', '-DPLUGIN_NAME="' + plugin_name + '"' ]
  )
endif

if build_standalone
  executable (meson.project_name(), sources, dependencies: deps, install: true,
    c_args : [ '-DPACKAGE_DATA_DIR="' + resource_dir + '"', '-DGETTEXT_PACKAGE="' + meson.project_name() + '"' ]
  )
endif
