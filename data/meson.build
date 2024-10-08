desktop_file = i18n.merge_file(
    input : 'dev.alexandro45.ezp2023plus.desktop.in',
    output : 'dev.alexandro45.ezp2023plus.desktop',
    type : 'desktop',
    po_dir : '../po',
    install : true,
    install_dir : datadir / 'applications'
)

desktop_utils = find_program('desktop-file-validate', required : false)
if desktop_utils.found()
    test('Validate desktop file', desktop_utils, args : [desktop_file])
endif

today = 'unknown'
date = find_program('date', required : false)
sh = find_program('sh')
if date.found()
    time_epoch = run_command(sh, '-c', 'echo "$SOURCE_DATE_EPOCH"', check : true).stdout().strip()
    if time_epoch != ''
        r = run_command(date, '-I', '-u', '-d', '@' + time_epoch, check : false)
    else
        r = run_command(date, '-I', check : false)
    endif
    if r.returncode() == 0
        today = r.stdout().strip()
    endif
endif

appdata_config = configuration_data()
appdata_config.set('BUILD_VERSION', meson.project_version())
appdata_config.set('BUILD_DATE', today)
appdata_config.set('TRANSLATION_DOMAIN', translation_domain)

appstream_file = i18n.merge_file(
    input : configure_file(
        input : 'dev.alexandro45.ezp2023plus.metainfo.xml.in.in',
        output : 'dev.alexandro45.ezp2023plus.metainfo.xml.in',
        configuration : appdata_config
    ),
    output : 'dev.alexandro45.ezp2023plus.metainfo.xml',
    po_dir : '../po',
    install : true,
    install_dir : datadir / 'metainfo'
)

appstreamcli = find_program('appstreamcli', required : false)
if appstreamcli.found()
    test('Validate appstream file', appstreamcli, args : ['validate', '--no-net', '--explain', appstream_file])
endif

install_data(
    'dev.alexandro45.ezp2023plus.svg',
    install_dir : datadir / 'icons' / 'hicolor' / 'scalable' / 'apps'
)

install_data(
    'dev.alexandro45.ezp2023plus-symbolic.svg',
    install_dir : datadir / 'icons' / 'hicolor' / 'symbolic' / 'apps'
)

install_data(
    'chips.dat',
    install_dir : datafilesdir
)

install_data(
    '45-ezp2023.rules',
    install_dir : sysconfdir / 'udev' / 'rules.d'
)