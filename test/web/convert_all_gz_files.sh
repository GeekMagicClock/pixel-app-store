#!/bin/bash
file2hex="./file_to_hex_array.exe"
platform=$(uname)
if [[ "$platform" == "Darwin" ]]; then
     plat="mac"
    echo "Mac OS X"
elif [[ "$platform" =~ "CYGWIN" || "$platform" =~ "MSYS" || "$platform" =~ "MINGW" ]]; then
     plat="win"
    file2hex="./file_to_hex_array_win.exe"
    echo "Windows"
else
    echo "Other"
fi

# 检查 file_to_hex_array 工具是否存在
if ! [ -x "$(command -v $file2hex)" ]; then
  echo 'Error: file_to_hex_array is not executable or not found.' >&2
  exit 1
fi

# 在当前目录中查找所有 .html.gz 文件
for gz_file in ./*.html.gz; do
  # 去掉路径的 .html.gz 扩展名并将斜杠替换为下划线以创建数组名称
  file_basename="$(basename "${gz_file%.html.gz}")"
  array_name="${file_basename//\//_}_html"

  # 为输出文件创建一个 .h 扩展名，并添加 "html_" 前缀
  output_file="html_${file_basename}.h"

  # 使用 file_to_hex_array 工具进行转换
  $file2hex "$gz_file" "$array_name" "$output_file"

  echo "Converted: $gz_file -> $output_file"
done

cp *.h ../../src/lib/web/ && echo "copy html done!"

cd js/ && ./convert_all_gz_files.sh && 
cp *.h ../../../src/lib/web/ && echo "copy js done!"

cd ../css/ && ./convert_all_gz_files.sh && 
cp *.h ../../../src/lib/web/ && echo "copy css done!"

