#!/usr/bin/env bash

set -e
bc=megadepth_static

#.e.g x86_64-w64-mingw32 or i686-w64-mingw32
compiler=$1
arch=$2
export CC=${compiler}-gcc
export CXX=${compiler}-g++
export AR=${compiler}-ar
export RANLIB=${compiler}-ranlib

if [[ ! -s mingw-std-threads ]]; then
    git clone https://github.com/meganz/mingw-std-threads.git
fi

CURL_VER=7.71.1
ARCH=$arch #32 #64 as of 2020-07-31, 32bit doesn't work due to recv not being linked properly in 32bit mingw libws2_32
if [[ ! -s libcurl_windows ]] ; then
    for f in curl-${CURL_VER}-win${ARCH}-mingw.zip libssh2-1.9.0-win${ARCH}-mingw.zip openssl-1.1.1g-win${ARCH}-mingw.zip nghttp2-1.41.0-win${ARCH}-mingw.zip brotli-1.0.7-win${ARCH}-mingw.zip zlib-1.2.11-win${ARCH}-mingw.zip; do
        wget  "https://curl.haxx.se/windows/dl-${CURL_VER}/${f}" -O $f
        unzip $f
    done
    ln -fs curl-${CURL_VER}-win${ARCH}-mingw libcurl_windows
    ln -fs libssh2-1.9.0-win${ARCH}-mingw libssh2
    ln -fs openssl-1.1.1g-win${ARCH}-mingw openssl
    ln -fs nghttp2-1.41.0-win${ARCH}-mingw nghttp2
    ln -fs brotli-1.0.7-win${ARCH}-mingw brotli
    ln -fs zlib-1.2.11-win${ARCH}-mingw zlib_windows
fi

if [[ ! -s libdeflate_windows ]] ; then
    ./get_libdeflate.sh $compiler windows
fi

if [[ ! -s htslib_windows ]] ; then
    export CFLAGS="-I../libdeflate_windows -I../zlib_windows"
    export LDFLAGS="-L../libdeflate_windows -L../zlib_windows -lz -ldeflate"
    
    ./get_htslib.sh $compiler windows 

    export CFLAGS=
    export LDFLAGS=
fi

if [[ ! -s libBigWig_windows ]] ; then
    ./get_libBigWig.sh
    pushd libBigWig_windows
    export CFLAGS="-I../libcurl_windows/include -I../zlib_windows -g -Wall -O3 -Wsign-compare -DCURL_STATICLIB"
    make clean
    make CC=${compiler}-gcc AR=$AR RANLIB=$RANLIB -f Makefile.fpic lib-static
    export CFLAGS=
    popd
fi

set -x

DR=build-release-temp
mkdir -p ${DR}
pushd ${DR}
cmake CC=$CC CXX=$CXX AR=$AR RANLIB=$RANLIB -DCMAKE_BUILD_TYPE=Release ..
make ${bc}.exe
popd
cp ${DR}/${bc}.exe ./megadepth.exe
rm -rf ${DR}
