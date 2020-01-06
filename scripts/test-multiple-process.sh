#!/bin/bash

TC_CLIENT_ROOT=$HOME/fsl-tc-client/

rm -rf /vfs0/tc_nfs4_test/serializable-rw/
mkdir -p /vfs0/tc_nfs4_test/serializable-rw/

# check if gtest-parallel is installed
export PATH="$HOME/gtest-parallel":$PATH
if ! type gtest-parallel &> /dev/null; then
  echo "installing gtest-parallel in $HOME"
  git clone https://github.com/google/gtest-parallel $HOME/gtest-parallel
fi

# create some dummy file in the test directory
$TC_CLIENT_ROOT/tc_client/build/tc/tc_lock_test -skip_test_setup=true -nreader_threads=0 -nwriter_threads=0 --gtest_filter="*SerializabilityRW*"

# run serializability tests on 5 workers (or processes)
gtest-parallel --repeat=1 --workers=20 --gtest_filter="*SerializabilityRW*" $TC_CLIENT_ROOT/tc_client/build/tc/tc_lock_test -- -skip_test_setup=true

rm -rf /vfs0/tc_nfs4_test/serializable-rw/

