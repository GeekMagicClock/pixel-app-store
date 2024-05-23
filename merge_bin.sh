#!/bin/bash

# 设置环境变量和项目路径
YOUR_ENVIRONMENT="adafruit_feather_esp32_v2"
PROJECT_PATH="./"

# 定义要查找的字符串和文件路径
SEARCH_STRING="#define SW_VERSION"
FILE_PATH="$PROJECT_PATH/include/my_debug.h"

# 检查文件是否存在，如果不存在则显示错误消息并退出
if [ ! -f "$FILE_PATH" ]; then
    echo "common.h 文件不存在。"
    exit 1
fi

# 查找版本号
VERSION=$(grep "$SEARCH_STRING" $FILE_PATH | awk '{print $3}' | tr -d '"')
echo $VERSION

# 检查是否找到版本号
if [ -z "$VERSION" ]; then
    echo "未找到版本号，请检查文件路径和搜索字符串是否正确。"
    exit 1
fi

# 定义要合并的文件路径
BOOTLOADER_PATH="$PROJECT_PATH/.pio/build/$YOUR_ENVIRONMENT/bootloader.bin"
PARTITION_TABLE_PATH="$PROJECT_PATH/.pio/build/$YOUR_ENVIRONMENT/partitions.bin"
FIRMWARE_PATH="$PROJECT_PATH/.pio/build/$YOUR_ENVIRONMENT/firmware.bin"
LITTLEFS_PATH="$PROJECT_PATH/.pio/build/$YOUR_ENVIRONMENT/littlefs.bin"
MERGED_FIRMWARE_PATH="$PROJECT_PATH/烧录8M_Pixel_$VERSION.bin"

# 检查文件是否存在，如果不存在则显示错误消息并退出
if [ ! -f "$BOOTLOADER_PATH" ]; then
    echo "bootloader.bin 文件不存在。"
    exit 1
fi

if [ ! -f "$PARTITION_TABLE_PATH" ]; then
    echo "partitions.bin 文件不存在。"
    exit 1
fi

if [ ! -f "$FIRMWARE_PATH" ]; then
    echo "firmware.bin 文件不存在。"
    exit 1
fi

cp $FIRMWARE_PATH firmware_Pixel_$VERSION.bin

if [ ! -f "$LITTLEFS_PATH" ]; then
    echo "littlefs.bin 文件不存在。"
    exit 1
fi

# 创建一个空的二进制文件
dd if=/dev/zero bs=1024 count=$(( (0x450000 + $(stat -c%s $LITTLEFS_PATH) + 1024 - 1) / 1024 )) of=$MERGED_FIRMWARE_PATH

# 合并二进制文件
dd if=$BOOTLOADER_PATH of=$MERGED_FIRMWARE_PATH conv=notrunc seek=$((0x1000 / 1024)) bs=1024
dd if=$PARTITION_TABLE_PATH of=$MERGED_FIRMWARE_PATH conv=notrunc seek=$((0x8000 / 1024)) bs=1024
dd if=$FIRMWARE_PATH of=$MERGED_FIRMWARE_PATH conv=notrunc seek=$((0x10000 / 1024)) bs=1024
dd if=$LITTLEFS_PATH of=$MERGED_FIRMWARE_PATH conv=notrunc seek=$((0x450000 / 1024)) bs=1024

echo "文件合并完成！文件名：$MERGED_FIRMWARE_PATH，版本号：$VERSION"
DIR=PIXEL_$VERSION
mkdir -p $DIR 
mv firmware_pixel_$VERSION.bin $MERGED_FIRMWARE_PATH $DIR 
cp $LITTLEFS_PATH $DIR
cp update\ history.txt $DIR 
echo "打包完成"
