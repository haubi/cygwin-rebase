#! /bin/bash -e

# $Id$

# vim: tabstop=4

# parse arguments
port=$1


tp1=${0%/*}
tp2=${tp1:-.}
srcdir=$(cd $tp2 && pwd)

# constants
pkg=rebase
version=$(cat ${srcdir}/configure.ac |\
	grep AC_INIT | tr '[](),' ' ' | awk '{print $3}')

# dir variables
Prefix=/usr
TmpDir=/tmp/$pkg.$$

case `uname -s` in
  CYGWIN*) CXXFLAGS="-static -static-libgcc -static-libstdc++" 
           CFLAGS="-static -static-libgcc"
	   confargs="--prefix=$Prefix"
  ;;
  MINGW*)  CXXFLAGS="-static -static-libgcc"
	   Prefix=/mingw
           confargs="--with-dash --prefix=`cd $Prefix && pwd -W`"
  ;;
  MSYS*)   confargs="--with-dash --prefix=$Prefix"
  ;;
esac

# configure
${srcdir}/configure ${confargs} \
	CXXFLAGS="${CXXFLAGS}" CFLAGS="${CFLAGS}"

# make
make

# make install
case `uname -s` in
  MINGW* ) make prefix=$TmpDir$Prefix install
           # the scripts won't work at all without MSYS,
	   # but even they, they can't interoperate with
	   # MinGW versions of the executables. So, punt:
  	   rm -f $TmpDir$Prefix/bin/rebaseall
  	   rm -f $TmpDir$Prefix/bin/peflagsall
  ;;
  * ) make DESTDIR=$TmpDir install
  ;;
esac

# strip executables
find $TmpDir$Prefix -name '*.exe' | xargs strip

# a few tweaks: use uninstalled peflags to mark
# installed executables
./peflags -t -t1 $TmpDir$Prefix/bin/peflags.exe
./peflags -t -t1 $TmpDir$Prefix/bin/rebase.exe

# create package
PrefixNoSlash=$(echo $Prefix | sed -e 's|^/*||')
tar -C $TmpDir -cjf $pkg-$version-$port.tar.bz2 $PrefixNoSlash

# remove temporary directory
rm -fr $TmpDir
