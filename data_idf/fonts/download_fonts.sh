#!/bin/bash
# 下载开源像素友好字体

cd "$(dirname "$0")"

echo "下载像素友好的开源字体..."

# VT323 - 经典终端字体，非常简洁
if [ ! -f "VT323-Regular.ttf" ]; then
    echo "下载 VT323-Regular.ttf..."
    curl -L "https://github.com/google/fonts/raw/main/ofl/vt323/VT323-Regular.ttf" -o VT323-Regular.ttf
fi

# Press Start 2P - 复古像素游戏字体，天然粗体
if [ ! -f "PressStart2P-Regular.ttf" ]; then
    echo "下载 PressStart2P-Regular.ttf..."
    curl -L "https://github.com/google/fonts/raw/main/ofl/pressstart2p/PressStart2P-Regular.ttf" -o PressStart2P-Regular.ttf
fi

# Share Tech Mono - 等宽清晰字体
if [ ! -f "ShareTechMono-Regular.ttf" ]; then
    echo "下载 ShareTechMono-Regular.ttf..."
    curl -L "https://github.com/google/fonts/raw/main/ofl/sharetechmono/ShareTechMono-Regular.ttf" -o ShareTechMono-Regular.ttf
fi

# Silkscreen - 像素风格字体
if [ ! -f "Silkscreen-Regular.ttf" ]; then
    echo "下载 Silkscreen-Regular.ttf..."
    curl -L "https://github.com/google/fonts/raw/main/ofl/silkscreen/Silkscreen-Regular.ttf" -o Silkscreen-Regular.ttf
fi

# Silkscreen Bold
if [ ! -f "Silkscreen-Bold.ttf" ]; then
    echo "下载 Silkscreen-Bold.ttf..."
    curl -L "https://github.com/google/fonts/raw/main/ofl/silkscreen/Silkscreen-Bold.ttf" -o Silkscreen-Bold.ttf
fi

echo "字体下载完成！"
echo ""
echo "推荐使用："
echo "  - VT323-Regular.ttf: 最简洁，适合小文本"
echo "  - ShareTechMono-Regular.ttf: 等宽，清晰"
echo "  - PressStart2P-Regular.ttf: 粗体，醒目"
echo "  - Silkscreen-Bold.ttf: 像素风粗体"
