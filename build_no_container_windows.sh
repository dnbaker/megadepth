#!/usr/bin/env bash

set -e
bc=megadepth_static

if [[ ! -s mingw-std-threads ]]; then
    git clone https://github.com/meganz/mingw-std-threads.git
fi

#if [[ ! -s zlib-1.2.5-bin-x64 ]] ; then
#    wget "https://downloads.sourceforge.net/project/mingw-w64/External%20binary%20packages%20%28Win64%20hosted%29/Binaries%20%2864-bit%29/zlib-1.2.5-bin-x64.zip?r=https%3A%2F%2Fsourceforge.net%2Fprojects%2Fmingw-w64%2Ffiles%2FExternal%2520binary%2520packages%2520%2528Win64%2520hosted%2529%2FBinaries%2520%252864-bit%2529%2Fzlib-1.2.5-bin-x64.zip%2Fdownload&ts=1596067650" -O zlib-1.2.5-bin-x64.zip
#    mkdir zlib-1.2.5-bin-x64
#    pushd zlib-1.2.5-bin-x64
#    unzip ../zlib-1.2.5-bin-x64.zip
#    mv zlib/* ./
#    rm -rf zlib
#    popd
#fi

if [[ ! -s curl-7.71.1-win64-mingw ]] ; then
    #https://curl.haxx.se/windows/dl-7.71.1/zlib-1.2.11-win64-mingw.zip
    for f in curl-7.71.1-win64-mingw.zip libssh2-1.9.0-win64-mingw.zip openssl-1.1.1g-win64-mingw.zip nghttp2-1.41.0-win64-mingw.zip brotli-1.0.7-win64-mingw.zip zlib-1.2.11-win64-mingw.zip; do
        wget  "https://curl.haxx.se/windows/${f}" -O $f
        unzip $f
    done
    ln -fs zlib-1.2.11-win64-mingw zlib
    #wget "https://curl.haxx.se/windows/dl-7.71.1/curl-7.71.1-win64-mingw.zip" -O curl-7.71.1-win64-mingw.zip
    #unzip curl-7.71.1-win64-mingw.zip
    #wget "https://curl.haxx.se/windows/dl-7.71.1/openssl-1.1.1g-win64-mingw.zip" -O openssl-1.1.1g-win64-mingw.zip
fi

if [[ ! -s libdeflate ]] ; then
    ./get_libdeflate.sh windows
fi

if [[ ! -s htslib ]] ; then
    export CFLAGS="-I../libdeflate -I../zlib-1.2.11-win64-mingw/include"
    #export CFLAGS="-I../libdeflate -I../zlib-1.2.5-bin-x64/include"
    #export LDFLAGS="-L../libdeflate -L../zlib-1.2.5-bin-x64/lib -ldeflate"
    
    #export CPPFLAGS="-I../libdeflate -I../curl-7.71.1-win64-mingw/include -I../zlib-1.2.5-bin-x64/include"
    #export CFLAGS="-I../libdeflate -I../curl-7.71.1-win64-mingw/include -I../zlib-1.2.5-bin-x64/include"
    #export LDFLAGS="-L../libdeflate -L../zlib-1.2.5-bin-x64/lib -L../curl-7.71.1-win64-mingw/lib -ldeflate -lcurl"
    
    #export CPPFLAGS="-I../libdeflate_i686 -I../curl-7.71.1-win32-mingw/include -I../libz-1.2.7-1_mingw32-dev/include"
    #export CFLAGS="-I../libdeflate -I../curl-7.71.1-win32-mingw/include -I../libz-1.2.7-1_mingw32-dev/include"
    #export LDFLAGS="-L../libdeflate_i686 -L../libz-1.2.7-1_mingw32-dev/lib -L../curl-7.71.1-win32-mingw/lib -ldeflate -lcurl"
    
    ./get_htslib.sh windows
    export CPPFLAGS=
    export CFLAGS=
    export LDFLAGS=
fi

if [[ ! -s libBigWig ]] ; then
    ./get_libBigWig.sh
    pushd libBigWig
    export CFLAGS="-I../curl-7.71.1-win64-mingw/include -I../zlib-1.2.11-win64-mingw/include -g -Wall -O3 -Wsign-compare -DCURL_STATICLIB"
    #export CFLAGS="-I../curl-7.71.1-win64-mingw/include -I../zlib-1.2.5-bin-x64/include -g -Wall -O3 -Wsign-compare -DCURL_STATICLIB"
    #export LDFLAGS="-L../zlib-1.2.5-bin-x64/lib -L../curl-7.71.1-win64-mingw/lib"
    export AR=x86_64-w64-mingw32-ar
    export RANDLIB=x86_64-w64-mingw32-ranlib
    #export CFLAGS="-I../zlib-1.2.5-bin-x64/include -g -Wall -O3 -Wsign-compare -DNOCURL"
    #export LDFLAGS="-L../zlib-1.2.5-bin-x64/lib -lz"
    make clean
    #make CC=x86_64-w64-mingw32-gcc -f Makefile.nocurl lib-static
    make CC=x86_64-w64-mingw32-gcc -f Makefile.fpic lib-static
    export CPPFLAGS=
    export CFLAGS=
    export LDFLAGS=
    popd
fi

set -x

DR=build-release-temp
mkdir -p ${DR}
pushd ${DR}
cmake -DCMAKE_BUILD_TYPE=Release ..
make ${bc}
popd
cp ${DR}/${bc}.exe ./megadepth.exe
rm -rf ${DR}
