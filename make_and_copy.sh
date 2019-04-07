BARREL_PATH=~/barrelfish
BUILD_DIR=build
MOUNT_PATH=/mnt/shared_folder
SBIN_DIR=armv8/sbin
MENU_DIR=hake/menu.lst.armv8_zynqmp

# build barrelfish
cd $BUILD_DIR
make ZynqmpZCU104 -j4

# mount sdcard
if [ ! -d "/mnt/shared_folder" ]; then
    sudo mkdir $MOUNT_PATH
fi

sudo mount -t vboxsf shared_folder $MOUNT_PATH

# cp sbin
echo "####: waiting for copy.\n"
sudo rm -r $MOUNT_PATH/sbin
sudo cp -r $BARREL_PATH/$BUILD_DIR/$SBIN_DIR $MOUNT_PATH/sbin
sudo cp $BARREL_PATH/$MENU_DIR $MOUNT_PATH
echo "####: copy done.\n"

#umount sdcard
sudo umount $MOUNT_PATH

echo "All is OK!"
