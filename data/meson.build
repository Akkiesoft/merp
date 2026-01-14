data = files (
    'merp.ui',
)

install_data(data, install_dir: resource_dir)
i18n.merge_file(input: 'merp.desktop.in',
      output: 'merp.desktop',
      type: 'desktop',
      po_dir: '../po',
      install: true,
      install_dir: desktop_dir
)
