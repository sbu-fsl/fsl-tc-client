#!/bin/bash
rm -rf /vfs0/tc_nfs4_test/serializable-rw/

mkdir -p /vfs0/tc_nfs4_test/serializable-rw/
# create some dummy file in the test directory
tc/tc_lock_test -skip_test_setup -nreader_threads=0 -writer_threads=0 --gtest_filter="*SerializabilityRW*"
tc/tc_lock_test -skip_test_setup --gtest_filter="*SerializabilityRW*" &
tc/tc_lock_test -skip_test_setup --gtest_filter="*SerializabilityRW*" &
tc/tc_lock_test -skip_test_setup --gtest_filter="*SerializabilityRW*" &
tc/tc_lock_test -skip_test_setup --gtest_filter="*SerializabilityRW*" &

wait

rm -rf /vfs0/tc_nfs4_test/serializable-rw/

