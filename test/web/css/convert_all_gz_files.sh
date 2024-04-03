#!/bin/bash
gzip -9k *.css -f
# 检查 file_to_hex_array 工具是否存在
if ! [ -x "$(command -v ./file_to_hex_array)" ]; then
  echo 'Error: file_to_hex_array is not executable or not found.' >&2
  exit 1
fi
# 在当前目录中查找所有 .html.gz 文件
for gz_file in ./*.css.gz; do
  # 去掉路径的 .html.gz 扩展名并将斜杠替换为下划线以创建数组名称
  file_basename="$(basename "${gz_file%.css.gz}")"
  array_name="${file_basename//\//_}_css"

  # 为输出文件创建一个 .h 扩展名，并添加 "js_" 前缀
  output_file="css_${file_basename}.h"

  # 使用 file_to_hex_array 工具进行转换
  ./file_to_hex_array "$gz_file" "$array_name" "$output_file"

  echo "Converted: $gz_file -> $output_file"
done

