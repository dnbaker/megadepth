#before running this you need to have the following:
#1) compiled and built bamcount_static
#2) linked the latest exons.bed file for the annotation into the parent dir
#3) run create_virtual_python_env.sh

if [[ ! -s ../bamcount_static ]] ; then
    pushd ../
    /bin/bash -x ./build_no_container.sh static
    popd
fi

EXONS_BED=exons.bed
if [[ ! -s $EXONS_BED ]] ; then
    EXONS_BED=$1
fi

if [[ ! -d python_local ]] ; then
    /bin/bash -x ./create_virtual_python_env.sh
fi

zip="$(pwd)"/lambda_bamcount.zip
rm -rf $zip
cat <(echo "#!/usr/bin/python") <(tail -n+2 python_local/bin/aws) > ./aws
chmod a+x aws
zip -r9 $zip function.sh bootstrap $EXONS_BED ../bamcount_static aws
if [[ "$EXONS_BED" != "exons.bed" ]] ; then
    ln -fs $EXONS_BED exons.bed
    $zip --symlinks -r $zip exons.bed
fi
pushd python_local/lib/python2.7/site-packages
zip -r9 $zip .
popd
