
Debian
====================
This directory contains files used to package frenchd/french-qt
for Debian-based Linux systems. If you compile frenchd/french-qt yourself, there are some useful files here.

## french: URI support ##


french-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install french-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your frenchqt binary to `/usr/bin`
and the `../../share/pixmaps/french128.png` to `/usr/share/pixmaps`

french-qt.protocol (KDE)

