Software for EZP2023+(and maybe other EZP*) programmer

**Attention:** Work in progress. Some basic features are still in development and there are some bugs :)

## Current features

* Test, read and write chips
* View and edit dump files. Undo and redo operations are available
* Open and save dump files
* Chips database editor
* Chips database file format is the same as in original Chinese software  

## Dependencies

### Build
   (The following were required on newly installed Debian13)
   apt install libxml2-utils
   apt install libadwaita-1-dev
   apt install libgusb-dev
   apt install libglib2.0-dev
   apt install gettext
   apt install build-essential
   apt install cmake
   apt install meson

### Directly used in code

    gio
    glib2
    glibc
    cairo
    gtk4
    libadwaita
    libusb
    meson (make)

### On clean Arch Linux setup

    libusb (just to make sure. it already installed with package 'pacman')
    libadwaita (other gtk packages will be installed as dependencies)
    meson (make)

## Setup

### Clone

    $ git clone https://github.com/alexandro-45/ezp2023plus.git
    $ cd ezp2023plus

### Build

    $ meson setup buildDir .
    $ meson compile -C buildDir

    or

    $ make build

    or

    $ make appImage

    or

    $ make PREFIX=~/.local build

### Install

    $ sudo meson install -C buildDir

    or, if you can't sudo

    $ meson setup --prefix ~/.local buildDir .
    $ meson compile -C buildDir
    $ meson install -C buildDir

    or, using make (and optionally an install prefix)

    $ make [ PREFIX=~/.local ] install
