#!/bin/sh

set -e

SUDO=$([ "$(id -u)" -eq 0 ] || echo "sudo")

if test -e build; then
    echo "build dir already exists; rm -rf build and re-run"
    rm -rf build
fi

mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=1 $@ ..
make -j8
cd ..

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

MOUNT_HELPER_FILE="mount.mfs"
# mount.mfs存放位置
MOUNT_HELPER_DIR="/sbin"

$SUDO cp "${SCRIPT_DIR}/mount_util/$MOUNT_HELPER_FILE" $MOUNT_HELPER_DIR
$SUDO chmod +x $MOUNT_HELPER_DIR/$MOUNT_HELPER_FILE
$SUDO mkdir -p /var/log/mfs
$SUDO chown -R $(id -un) /var/log/mfs
$SUDO mkdir -p /tmp/mfs
$SUDO chown -R $(id -un) /tmp/mfs

# 定义 fuse.conf 的路径
FUSE_CONF="/usr/local/etc/fuse.conf"
# 检查 fuse.conf 是否存在
if [ ! -f "$FUSE_CONF" ]; then
    echo "$FUSE_CONF 文件不存在，请确保 FUSE 已正确安装。"
    exit 1
fi
# 检查是否已经设置了 user_allow_other
if grep -q '^user_allow_other' "$FUSE_CONF"; then
    echo "fuse.conf 已设置 user_allow_other。"
else
    # 检查是否有被注释的 user_allow_other
    if grep -q '^#\s*user_allow_other' "$FUSE_CONF"; then
        # 取消注释 user_allow_other
        $SUDO sed -i 's/^#\s*\(user_allow_other\)/\1/' "$FUSE_CONF"
        echo "已取消注释 fuse.conf 中的 user_allow_other。"
    else
        # 添加 user_allow_other 到文件末尾
        echo "user_allow_other" | $SUDO tee -a "$FUSE_CONF" >/dev/null
        echo "已在 fuse.conf 中添加 user_allow_other。"
    fi
fi

mount -t mfs /dev/null /tmp/mfs
