
cd $APP_DIR
export HEADERS=$PWD/usr/include
mkdir -p $HEADERS
for package in glibc-headers glibc-devel alsa-lib-devel mesa-libGL-devel libxkbcommon-x11-devel kernel-headers ; do
  (
    cd /usr/include
    rsync -ar $(rpm -ql $package | grep '/usr/include' | grep '\.h' | cut -c 14-  | sort | uniq) "$HEADERS/"
  )
done

source common.sh

export LLVM_VER=$(ls /opt/score-sdk/llvm/lib/clang/)
mkdir -p $PWD/usr/lib/clang/$LLVM_VER/include
rsync -ar $OSSIA_SDK/llvm/lib/clang/$LLVM_VER/include/ $PWD/usr/lib/clang/$LLVM_VER/include/

