#!/usr/bin/env python3
import os
import subprocess
import time

# 1001fonts.com上的pixel字体列表（免费可商用或个人使用）
fonts = [
    "pixel-operator",
    "visitor",
    "5x5-pixel",
    "04b-03",
    "pixelmix",
    "joystix",
    "smallest-pixel-7",
    "pixelated",
    "pocket-pixel",
    "mini-pixel-7",
    "pixelfj8",
    "pixeboy",
    "matchup-pro",
    "connection",
    "connection-ii",
    "connection-iii",
    "dogica-pixel",
    "dogicapixel",
    "dogicapixelbold",
    "pico-8",
    "upheaval",
    "pixelify-sans",
    "commodore-64-pixelized",
    "8-bit-operator",
    "determination-mono",
    "determination-sans",
]

def download_and_extract(font_name, target_dir):
    """下载并解压字体"""
    print(f"\n处理字体: {font_name}")
    
    # 临时文件名
    zip_file = os.path.join(target_dir, f"{font_name}.zip")
    temp_dir = os.path.join(target_dir, f"temp_{font_name}")
    
    # 下载
    url = f"https://www.1001fonts.com/download/{font_name}.zip"
    cmd = ["curl", "-L", "-o", zip_file, url]
    result = subprocess.run(cmd, capture_output=True)
    
    if result.returncode != 0:
        print(f"  下载失败: {font_name}")
        return False
    
    # 检查文件大小
    if os.path.exists(zip_file):
        size = os.path.getsize(zip_file)
        if size < 1000:  # 太小，可能是错误页面
            print(f"  下载文件太小，跳过: {size} bytes")
            os.remove(zip_file)
            return False
    
    # 解压
    os.makedirs(temp_dir, exist_ok=True)
    subprocess.run(["unzip", "-q", zip_file, "-d", temp_dir], capture_output=True)
    
    # 查找ttf文件
    ttf_files = []
    for root, dirs, files in os.walk(temp_dir):
        for file in files:
            if file.lower().endswith(('.ttf', '.otf')):
                ttf_files.append(os.path.join(root, file))
    
    if not ttf_files:
        print(f"  未找到字体文件")
        subprocess.run(["rm", "-rf", temp_dir, zip_file])
        return False
    
    # 移动第一个ttf文件
    src = ttf_files[0]
    basename = os.path.basename(src)
    # 清理文件名
    clean_name = basename.replace(" ", "").replace("-", "")
    dst = os.path.join(target_dir, clean_name)
    
    subprocess.run(["mv", src, dst])
    
    # 检查文件大小
    file_size = os.path.getsize(dst)
    print(f"  ✓ 成功: {clean_name} ({file_size} bytes)")
    
    # 清理
    subprocess.run(["rm", "-rf", temp_dir, zip_file])
    
    # 如果文件太大，删除
    if file_size > 150 * 1024:
        print(f"  文件过大 (>{150}KB)，删除")
        os.remove(dst)
        return False
    
    return True

def main():
    target_dir = "/Users/ifeng/develop/project/esp32-pixel/data_littlefs/fonts"
    os.chdir(target_dir)
    
    success_count = 0
    for font in fonts:
        if download_and_extract(font, target_dir):
            success_count += 1
        time.sleep(0.5)  # 避免请求太频繁
    
    print(f"\n\n=== 完成 ===")
    print(f"成功下载: {success_count}/{len(fonts)}")
    print(f"\n当前字体列表:")
    subprocess.run(["ls", "-lh", "*.ttf"])

if __name__ == "__main__":
    main()
