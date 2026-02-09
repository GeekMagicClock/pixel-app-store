#!/usr/bin/env python3
"""
字体预览工具 - 模拟 64x32px LED 屏幕效果
快速筛选适合小尺寸显示的字体
"""

import os
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

# 配置
SCREEN_WIDTH = 64
SCREEN_HEIGHT = 32
FONT_SIZE = 8  # 像素
TEST_TEXT = "ABCDE\nabcde\n01234"
BG_COLOR = (0, 0, 0)  # 黑色背景
FG_COLOR = (255, 255, 255)  # 白色前景

def get_font_files(directory):
    """获取目录下所有 TTF 字体文件及其大小"""
    fonts = []
    for font_path in Path(directory).glob("*.ttf"):
        size = font_path.stat().st_size
        fonts.append((font_path, size))
    return sorted(fonts, key=lambda x: x[1])  # 按文件大小排序

def create_preview(font_path, font_size):
    """创建字体预览图"""
    try:
        # 创建图像
        img = Image.new('RGB', (SCREEN_WIDTH, SCREEN_HEIGHT), BG_COLOR)
        draw = ImageDraw.Draw(img)
        
        # 加载字体
        font = ImageFont.truetype(str(font_path), font_size)
        
        # 绘制文本（居中）
        draw.text((2, 2), TEST_TEXT, font=font, fill=FG_COLOR)
        
        return img
    except Exception as e:
        print(f"  ❌ 无法加载: {e}")
        return None

def create_comparison_grid(font_dir, output_path="font_preview_grid.png"):
    """创建所有字体的对比网格图"""
    fonts = get_font_files(font_dir)
    
    if not fonts:
        print(f"❌ 在 {font_dir} 未找到 TTF 字体")
        return
    
    print(f"📊 找到 {len(fonts)} 个字体文件")
    print("=" * 60)
    
    # 计算网格布局（每行 4 个）
    cols = 4
    rows = (len(fonts) + cols - 1) // cols
    
    # 每个单元格的尺寸（包含标题）
    cell_width = SCREEN_WIDTH + 20
    cell_height = SCREEN_HEIGHT + 30
    
    # 创建大图
    grid_img = Image.new('RGB', 
                         (cols * cell_width, rows * cell_height), 
                         (40, 40, 40))
    
    valid_fonts = []
    
    for idx, (font_path, size) in enumerate(fonts):
        font_name = font_path.stem
        size_kb = size / 1024
        
        # 判断是否超过大小限制
        status = "✅" if size <= 150 * 1024 else "⚠️"
        
        print(f"{status} [{idx+1:2d}] {font_name:30s} {size_kb:6.1f} KB")
        
        # 创建预览
        preview = create_preview(font_path, FONT_SIZE)
        
        if preview:
            # 计算位置
            row = idx // cols
            col = idx % cols
            x = col * cell_width + 10
            y = row * cell_height + 25
            
            # 放大预览（2倍）便于观察
            preview_scaled = preview.resize((SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2), 
                                           Image.NEAREST)
            grid_img.paste(preview_scaled, (x, y))
            
            # 添加字体名称标签
            draw = ImageDraw.Draw(grid_img)
            label = f"{font_name[:15]}\n{size_kb:.0f}KB"
            draw.text((x, y - 22), label, fill=(200, 200, 200))
            
            valid_fonts.append((font_name, size_kb, size <= 150 * 1024))
    
    # 保存
    grid_img.save(output_path)
    print("=" * 60)
    print(f"✅ 预览图已保存: {output_path}")
    print(f"📏 网格尺寸: {cols} 列 × {rows} 行")
    print()
    
    # 输出推荐
    print("📋 推荐字体（<150KB，适合 ESP32）:")
    print("-" * 60)
    for name, size_kb, is_small in valid_fonts:
        if is_small:
            print(f"  ✓ {name:30s} {size_kb:6.1f} KB")
    
    return valid_fonts

def create_single_preview(font_path, output_path=None):
    """创建单个字体的放大预览"""
    preview = create_preview(font_path, FONT_SIZE)
    
    if preview:
        # 放大 8 倍便于观察像素
        preview_large = preview.resize((SCREEN_WIDTH * 8, SCREEN_HEIGHT * 8), 
                                      Image.NEAREST)
        
        if output_path is None:
            output_path = f"preview_{Path(font_path).stem}.png"
        
        preview_large.save(output_path)
        print(f"✅ 单个预览已保存: {output_path}")
    else:
        print(f"❌ 无法创建预览")

if __name__ == "__main__":
    # 默认字体目录
    font_dir = Path(__file__).parent.parent / "data_littlefs" / "fonts"
    
    if len(sys.argv) > 1:
        font_dir = Path(sys.argv[1])
    
    if not font_dir.exists():
        print(f"❌ 目录不存在: {font_dir}")
        sys.exit(1)
    
    print(f"🔍 扫描字体目录: {font_dir}")
    print(f"📺 模拟屏幕: {SCREEN_WIDTH}x{SCREEN_HEIGHT}px")
    print(f"📝 测试文本: {repr(TEST_TEXT)}")
    print(f"🔤 字体大小: {FONT_SIZE}px")
    print()
    
    # 创建对比网格
    output_path = Path(__file__).parent.parent / "font_preview_grid.png"
    create_comparison_grid(font_dir, output_path)
    
    print()
    print("💡 提示：打开 font_preview_grid.png 查看所有字体效果")
    print("💡 每个预览都是 2x 放大，便于观察清晰度")
