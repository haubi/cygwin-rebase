#! /bin/bash

# $Id$

# vim: tabstop=4

# parse arguments
version=$1
port=$2

# constants
pkg=rebase

# dir variables
Prefix=/usr
TmpDir=/tmp/$pkg.$$
InstallPrefix=$TmpDir$Prefix

# configure
# not currently necessary

# make
make

# make install
make DESTDIR=$TmpDir install

# strip executables
find $InstallPrefix -name '*.exe' | xargs strip

# a few tweaks: use uninstalled peflags to mark
# installed executables
./peflags -t -t1 $TmpDir$Prefix/bin/peflags.exe
./peflags -t -t1 $TmpDir$Prefix/bin/rebase.exe

# create package
tar -C $TmpDir -cjf $pkg-$version-$port.tar.bz2 usr

# remove temporary directory
rm -fr $TmpDir
