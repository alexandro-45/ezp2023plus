#!/usr/bin/env bash

set -e

THIS_FILE=$(basename "$0"); echo "SCRIPT: ${THIS_FILE}"
PROJECT_DIR=$(realpath "$(dirname "$0")"); echo "PROJECT DIR: ${PROJECT_DIR}"
PROJECT=$(basename $(readlink -f "${PROJECT_DIR}")); echo "PROJECT: ${PROJECT}"
BUILD_DIR="${PROJECT_DIR}/build"
APP_DIR="${BUILD_DIR}/${PROJECT}.AppDir"
BUILD_OUTPUT_DIR="${BUILD_DIR}/output"
APP_IMAGE_TOOL="appimagetool-x86_64.AppImage"
APP_IMAGE_SRC="https://github.com/AppImage/appimagetool/releases/download/continuous/${APP_IMAGE_TOOL}"
APP_IMAGE="${BUILD_DIR}"/"${APP_IMAGE_TOOL}"

meson setup --prefix /usr --buildtype release "${BUILD_DIR}" "${PROJECT_DIR}"
mkdir "${BUILD_OUTPUT_DIR}"

PROJECT_VERSION=$(<<< "$(meson introspect --projectinfo "${BUILD_DIR}")" jq -r ".version")

function appimage () {
  echo "Building an AppImage"
  if [ ! -e "$(command -v zenity)" ]; then
    echo "zenity is needed to build an AppImage."
    exit 1
  fi

  meson compile -C "${BUILD_DIR}"
  meson install -C "${BUILD_DIR}" --destdir "${APP_DIR}" --no-rebuild

  cp "$(command -v zenity)" "${APP_DIR}/usr/bin/"
  cp "${PROJECT_DIR}/dist/zenity_password" "${APP_DIR}/usr/bin/"

  if [[ ! -e "${APP_IMAGE}" ]]; then
    wget "${APP_IMAGE_SRC}" -P "${BUILD_DIR}"
    chmod u+x "${APP_IMAGE}"
  fi


  # Folder structure: https://stackoverflow.com/questions/78515281/how-to-resolve-desktop-file-not-found-when-creating-appimage-with-appstream-upst

  # removed deploy
  #"${APP_IMAGE}" -s deploy "${APP_DIR}/usr/share/applications/dev.alexandro45.ezp2023plus.desktop"
  ln -s "${APP_DIR}/usr/share/applications/dev.alexandro45.ezp2023plus.desktop" "${APP_DIR}/ezp2023plus.desktop"
  cp "${PROJECT_DIR}/dist/AppRun" "${APP_DIR}/AppRun"
  cp "${PROJECT_DIR}/misc/icon_attempt2.svg" "${APP_DIR}/dev.alexandro45.ezp2023plus.png"
  #cp "${PROJECT_DIR}/misc/EZP2023-+-Icon.png" "${APP_DIR}/dev.alexandro45.ezp2023plus.png"

  cp -r "/usr/share/icons/Adwaita/" "${APP_DIR}/usr/share/icons/Adwaita/"                    #
  #cp -r "/usr/share/icons/AdwaitaLegacy/" "${APP_DIR}/usr/share/icons/AdwaitaLegacy/"        # TODO: check if all this stuff is really necessary
  cp -r "/usr/share/icons/default/" "${APP_DIR}/usr/share/icons/default/"                    #
  cp "/usr/share/icons/hicolor/index.theme" "${APP_DIR}/usr/share/icons/hicolor/index.theme" #

  mkdir -p "${APP_DIR}/usr/share/glib-2.0/schemas/"
  cp "/usr/share/glib-2.0/schemas/org.gnome.desktop.enums.xml" "${APP_DIR}/usr/share/glib-2.0/schemas/"
  cp "/usr/share/glib-2.0/schemas/org.gnome.desktop.interface.gschema.xml" "${APP_DIR}/usr/share/glib-2.0/schemas/"
  cp "/usr/share/glib-2.0/schemas/org.gnome.settings-daemon.enums.xml" "${APP_DIR}/usr/share/glib-2.0/schemas/"
  cp "/usr/share/glib-2.0/schemas/org.gnome.settings-daemon.plugins.color.gschema.xml" "${APP_DIR}/usr/share/glib-2.0/schemas/"
  cp "/usr/share/glib-2.0/schemas/org.gnome.settings-daemon.plugins.gschema.xml" "${APP_DIR}/usr/share/glib-2.0/schemas/"
  cp "/usr/share/glib-2.0/schemas/org.gnome.settings-daemon.plugins.housekeeping.gschema.xml" "${APP_DIR}/usr/share/glib-2.0/schemas/"
  cp "/usr/share/glib-2.0/schemas/org.gnome.settings-daemon.plugins.media-keys.gschema.xml" "${APP_DIR}/usr/share/glib-2.0/schemas/"
  cp "/usr/share/glib-2.0/schemas/org.gnome.settings-daemon.plugins.power.gschema.xml" "${APP_DIR}/usr/share/glib-2.0/schemas/"
  cp "/usr/share/glib-2.0/schemas/org.gnome.settings-daemon.plugins.xsettings.gschema.xml" "${APP_DIR}/usr/share/glib-2.0/schemas/"
  glib-compile-schemas "${APP_DIR}/usr/share/glib-2.0/schemas/"

  mkdir "${APP_DIR}/usr/share/mime/"
  cp "${PROJECT_DIR}/dist/svg_mime.cache" "${APP_DIR}/usr/share/mime/mime.cache"

  pushd "${BUILD_OUTPUT_DIR}"
  VERSION=${PROJECT_VERSION} "${APP_IMAGE}" "${APP_DIR}"
  popd
}

function arch_pkgbuild () {
  echo "Generating PKGBUILD for ArchLinux"
  cp "${PROJECT_DIR}/dist/PKGBUILD" "${BUILD_OUTPUT_DIR}/PKGBUILD"
  sed -e "s/__pkg_ver__/${PROJECT_VERSION}/g" -i "${BUILD_OUTPUT_DIR}/PKGBUILD"
}

appimage
arch_pkgbuild