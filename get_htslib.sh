#!/usr/bin/env bash

set -ex

#macos:
#   x86_64-apple-darwin12-gcc (CC=o64-gcc)
#OR
#   i386-apple-darwin1 (CC=o32-gcc)
#windows:
#   x86_64-w64-mingw32 (64bit)
#OR
#   i686-w64-mingw32 (32bit)

compiler=$1
#"macos" or "windows" (or nothing for normal linux build)
platform=$2

#VER=1.9
VER=1.10.2
AR=htslib-${VER}.tar.bz2
if [[ ! -s htslib ]] ; then
    curl -OL https://github.com/samtools/htslib/releases/download/${VER}/${AR}
    bzip2 -dc ${AR} | tar xvf - 
    rm -f ${AR}
    pushd htslib-${VER}
    MOVE=1
else
    pushd htslib
fi

autoheader
autoconf
make clean

if [[ -z $compiler ]]; then
    ./configure --disable-bz2 --disable-lzma --disable-libcurl --with-libdeflate
    make
else
    ./configure --disable-bz2 --disable-lzma --disable-libcurl --with-libdeflate --host=$compiler
    export CC=gcc
    if [[ "$xcross" == "macos" ]]; then
        #only make static lib for cross-compilation for now
        export CC=/opt/osxcross/target/bin/${compiler}-gcc
        export AR=/opt/osxcross/target/bin/${compiler}-ar
        export RANLIB=/opt/osxcross/target/bin/${compiler}-ranlib
        #export AR=/opt/osxcross/target/bin/x86_64-apple-darwin12-ar
        #export RANLIB=/opt/osxcross/target/bin/x86_64-apple-darwin12-ranlib
        #cp Makefile Makefile.orig
        #cat Makefile.orig | perl -ne 'chomp; $f=$_; $f=~s!^(AR\s*=\s*ar\s*)$!AR='$AR'!; $f=~s!^(RANLIB\s*=\s*ar\s*)$!AR='$RANLIB'!; print "$f\n";' > Makefile
        #make CC=$CC libhts.a
    else # windows
        #export CC=i686-w64-mingw32-gcc
        #export CC=x86_64-w64-mingw32-gcc
        export CC=${compiler}-gcc
        export AR=${compiler}-ar
        export RANLIB=${compiler}-ranlib
        #make libhts.a
        #./configure --disable-bz2 --disable-lzma --disable-libcurl --with-libdeflate --host=i686-w64-mingw32
        #./configure --disable-bz2 --disable-lzma --disable-libcurl --with-libdeflate --host=x86_64-w64-mingw32
        #make CC=i686-w64-mingw32-gcc libhts.a
        #make CC=x86_64-w64-mingw32-gcc libhts.a
        #set cross_compiling=yes in htslib/configure to turn off mmap
    fi
    make CC=$CC AR=$AR RANLIB=$RANLIB libhts.a
fi

popd

if [[ -n "$MOVE" ]]; then
    mv htslib-${VER} htslib
fi
