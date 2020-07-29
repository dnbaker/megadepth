#!/usr/bin/env bash

set -ex

xcross=$1

VER=1.6
TARGZ=${VER}.tar.gz
FN=libdeflate-${TARGZ}
DIR=libdeflate-${VER}
curl -L https://github.com/ebiggers/libdeflate/archive/v${TARGZ} > $FN
tar -zxvf $FN
rm -f ${FN}
pushd $DIR
if [[ "$xcross" == "windows" ]]; then
    #make CC=i686-w64-mingw32-gcc CFLAGS='-O3' libdeflatestatic.lib
    make CC=x86_64-w64-mingw32-gcc CFLAGS='-O3' libdeflatestatic.lib
    ln -fs libdeflatestatic.lib libdeflate.a
else
    #from https://github.com/samtools/htslib/issues/688
    make CFLAGS='-fPIC -O3' libdeflate.a
fi
popd
mv $DIR libdeflate
