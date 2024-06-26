Software for EZP2023+(and maybe other EZP*) programmer

**Attention:** Work in progress. Some basic features are still in development and there are some bugs :)

## Current features

* Test, read and write chips
* View and edit dump files. Undo and redo operations are available
* Open and save dump files
* Chips editor (only editing. add and delete isn't implemented yet)
* Chips database file format is the same as in original Chinese software  

## Dependencies

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

### Install

    $ sudo meson install -C buildDir

## TODO list
- chip search from main window
- delay override from main window
- flash size override from main window
- erase (not implemented in libezp2023plus)
- translations
- build for Windows
- make functions that return -1 or 0 return gboolean
- adding and deleting chips
- PKGBUILD for Arch Linux