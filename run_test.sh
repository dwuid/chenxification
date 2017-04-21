#!/bin/bash

set -e

clang ./test/md5.c -o ./test/md5-orig
clang ./test/md5.c -emit-llvm -c -o ./test/md5.bc

opt -load ./build/chenxification/Chenxification.so \
    -loop-simplify -lowerswitch -reg2mem \
    -chenxify \
    -mem2reg -dot-cfg \
    ./test/md5.bc -o ./test/md5.bc

clang -O3 ./test/md5.bc -o ./test/md5

echo "foo" > ./test/foo

set +e
(cd ./test && diff <(./md5 -x) <(./md5-orig -x) >/dev/null);

RESULT=$?
if [ $RESULT -eq 0 ]; then
    echo -e "\nAll good."
else
    echo -e "\nTransformation is NOT semantics-preserving!"
fi

