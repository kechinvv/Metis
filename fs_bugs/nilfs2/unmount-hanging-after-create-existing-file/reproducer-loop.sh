#!/bin/bash

MNTPNT="/mnt/test-nilfs2"
IMGFILE="./nilfs2-dev-88-unmount-hanging.img"
DEVFILE=""

# Create a temporary file
TMPFILE=$(mktemp)

# Create a 1028KiB file (2*514K blocks)
dd if=/dev/zero of="$TMPFILE" bs=1K count=1028

# Set up the loop device
LOOPDEV=$(sudo losetup --find --show "$TMPFILE")

# Check if the loop device was successfully created
if [ -n "$LOOPDEV" ]; then
    # echo "Loop device created: $LOOPDEV"
    DEVFILE="$LOOPDEV"
else
    echo "Failed to create loop device."
    exit 1
fi

if test -n "$(mount | grep $MNTPNT)" ; then
    umount $MNTPNT
fi

if test -d $MNTPNT ; then
    rm -rf $MNTPNT
fi
mkdir -p $MNTPNT 

dd if=$IMGFILE of=$DEVFILE bs=4k status=none

mount $DEVFILE $MNTPNT

# Create an already-existing file to reproduce the unmount hanging bug
cd $MNTPNT/d-00
touch f-02
cd -

echo "Start Unmounting..."

# Unmounting to reproduce hanging
umount $MNTPNT

echo "Reproducer finished (not supposed to see this message due to hanging)."
