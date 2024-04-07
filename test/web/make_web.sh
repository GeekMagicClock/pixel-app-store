#!/bin/bash

# 设置目标目录变量
target_dir="../../data"

# 获取目标目录的绝对路径
absolute_target_dir=$(realpath "$target_dir")

# 检查目标目录是否存在，如果不存在则报错并停止脚本执行
if [ ! -d "$absolute_target_dir" ]; then
    echo "目标目录 $absolute_target_dir 不存在，请先创建该目录。"
    exit 1
fi

mkdir -p "$target_dir/js"
mkdir -p "$target_dir/css"

echo $absolute_target_dir
# 压缩并移动css文件
echo "开始压缩和移动css文件..."
find ./css -name "*.css" -type f -exec sh -c 'gzip -9k -f "{}" && mv "{}.gz" "$1/css/$(basename "{}").gz" && echo "已处理: {}"' _ "$absolute_target_dir" \;
echo "css文件处理完成！"

# 压缩并移动js文件
echo "开始压缩和移动js文件..."
find ./js -name "*.js" -type f -exec sh -c 'gzip -9k -f "{}" && mv "{}.gz" "$1/js/$(basename "{}").gz" && echo "已处理: {}"' _ "$absolute_target_dir" \;
echo "js文件处理完成！"

# 压缩并移动html文件
echo "开始压缩和移动html文件..."
find ./ -name "*.html" -type f -exec sh -c 'gzip -9k -f "{}" && mv "{}.gz" "$1/$(basename "{}").gz" && echo "已处理: {}"' _ "$absolute_target_dir" \;
echo "html文件处理完成！"

rm $target_dir/bili.html.gz $target_dir/bitcoin.html.gz $target_dir/monitor.html.gz $target_dir/daytimer.html.gz $target_dir/stock.html.gz
