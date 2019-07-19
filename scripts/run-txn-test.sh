#!/bin/bash

if [ -z "$TC_BASE" ]; then
    TC_BASE="$HOME";
fi
echo "TC_BASE: $TC_BASE";

TC_EXPORT_DIR="/tcserver"
echo "TC_EXPORT_DIR: $TC_EXPORT_DIR"

if ! [ -d "$TC_EXPORT_DIR" ]; then
    echo "Please ensure dir '$TC_EXPORT_DIR' exist.";
    exit 1;
fi
sudo rm -rf $TC_EXPORT_DIR/tc_nfs4_test

if [ -z "$TC_MNT_DIR" ]; then
    TC_MNT_DIR="/vfs0";
fi
echo "TC_MNT_DIR: $TC_MNT_DIR"

if ! [ -d "$TC_MNT_DIR" ]; then
    echo "Please ensure dir '$TC_MNT_DIR' exist.";
    exit 1;
fi

if [ -z "$TC_SERVER_REMOTE" ]; then
    TC_SERVER_REMOTE="git@github.com:sbu-fsl/fsl-tc-server.git";
fi
echo "TC_SERVER_REMOTE: $TC_SERVER_REMOTE"

if [ -z "$TC_SERVER_REPO" ]; then
    TC_SERVER_REPO="tc-server";
fi
echo "TC_SERVER_REPO: $TC_SERVER_REPO"

if [ -z "$TC_SERVER_BRANCH" ]; then
    TC_SERVER_BRANCH="suwei/tc-txn-test";
fi
echo "TC_SERVER_BRANCH: $TC_SERVER_BRANCH";

if [ -z "$TC_BUILD_TYPE" ]; then
    TC_BUILD_TYPE="Debug";
fi
echo "TC_BUILD_TYPE: $TC_BUILD_TYPE";

if [ -z "$TC_LOG_LEVEL" ]; then
    if [ "$TC_BUILD_TYPE" = "Release" ]; then
        TC_LOG_LEVEL="INFO"
    else
        TC_LOG_LEVEL="DEBUG"
    fi
fi
echo "TC_LOG_LEVEL: $TC_LOG_LEVEL";

# Clone the tc-server if it doesn't exist
if ! [ -e $TC_BASE/$TC_SERVER_REPO ]; then
    git clone -b $TC_SERVER_BRANCH $TC_SERVER_REMOTE $TC_BASE/$TC_SERVER_REPO
fi
# go to tc-server and install dependencies
pushd $TC_BASE/$TC_SERVER_REPO
if ! [ -e $TC_BASE/$TC_SERVER_REPO/build/MainNFSD/ganesha.nfsd ]; then
    pushd tc
    sudo ./install-dependency-ubuntu.sh
    popd
fi
# launch the server and mount the filesystem
echo "The server has been installed. Now let's launch and mount."
sudo ./build/MainNFSD/ganesha.nfsd -f $(pwd)/config/tcserver.ganesha.conf -L /var/log/tcserver.log -N $TC_LOG_LEVEL
if [ -z "$(ps aux | grep '[g]anesha.nfsd')" ]; then
    echo "The server did not start up... Please check the log."
    exit 2;
fi
sleep 5;
sudo mount -t nfs localhost:/vfs0 $TC_MNT_DIR
echo "Are you OK?" >> /tmp/hello
sudo cp /tmp/hello $TC_MNT_DIR/
echo "Server online."
# go back to the client dir, and build the client project
popd
pushd ../tc_client
mkdir -p build
pushd build
if ! [ -e tc/tc_test ]; then
    cmake -DCMAKE_BUILD_TYPE=$TC_BUILD_TYPE ..
    make -j$(nproc --all)
fi
# Run tests
sudo tc/tc_test
sudo tc/tc_txn_test
popd
popd
# Unmount and kill server process
sudo umount -f $TC_MNT_DIR
sudo kill $(ps aux | grep '[g]anesha.nfsd' | awk '{print $2}')
