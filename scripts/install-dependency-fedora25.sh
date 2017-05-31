#!/bin/bash -
#
# Setup txn-compound on CentOS7
#
# by Garima Gehlot, garima.gehlot@stonybrook.edu
# by Ming Chen, mchen@cs.stonybrook.edu
#=============================================================================

set -o nounset                          # treat unset variables as an error
set -o errexit                          # stop script if command fail
export PATH="/bin:/usr/bin:/sbin:/usr/local/bin"
IFS=$' \t\n'                            # reset IFS
unset -f unalias                        # make sure unalias is not a function
\unalias -a                             # unset all aliases
ulimit -H -c 0                          # disable core dump
hash -r                                 # clear the command path hash

DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )

# got to the root of this git repo
cd $DIR/../

# NFS-ganesha specific
git submodule update --init --recursive

dnf install -y cmake
dnf install -y glog-devel gflags-devel
#libgssglue-devel
dnf install -y openssl-devel
dnf install -y libnfsidmap-devel
dnf install -y doxygen
dnf install -y gperftools-devel gperftools-libs  # for tcmalloc
dnf install -y protobuf-devel leveldb-devel snappy-devel opencv-devel boost-devel hdf5-devel
dnf install -y lmdb-devel jemalloc-devel tbb-devel libaio-devel cryptopp-devel
dnf -y groupinstall "Development Tools"
dnf install -y glibc-headers
dnf install -y gcc-c++
dnf install -y bison flex
dnf install -y libcurl-devel boost-system boost-filesystem boost-regex
dnf install -y boost-static
dnf install -y glib2-devel glib-devel
dnf install -y automake autoconf libtool
dnf install -y libcap-devel libwbclient-devel libuuid-devel libblkid-devel

# To resolve https://github.com/nfs-ganesha/nfs-ganesha/issues/67
dnf install -y libtirpc
dnf install -y poco-devel poco-util

mkdir -p /opt
cd /opt

# setup gmock and gtest
if [ ! -d gmock-1.7.0 ]; then
  wget https://github.com/google/googlemock/archive/release-1.7.0.zip
  unzip release-1.7.0.zip
  mv googlemock-release-1.7.0 gmock-1.7.0

  cd gmock-1.7.0
  wget https://github.com/google/googletest/archive/release-1.7.0.zip
  unzip release-1.7.0.zip
  mv googletest-release-1.7.0 gtest

  autoreconf -fvi
  ./configure
  make
fi

# install poco
POCO_VER='1.7.8'
wget https://pocoproject.org/releases/poco-${POCO_VER}/poco-${POCO_VER}-all.tar.gz
tar -xzf poco-${POCO_VER}-all.tar.gz
cd poco-${POCO_VER}-all
./configure --omit=Data/ODBC,Data/MySQL
make -j2
make install
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH


echo "GOOGLE_MOCK is installed at /opt/gmock-1.7.0"
echo "GOOGLE_TEST is installed at /opt/gmock-1.7.0/gtest"
