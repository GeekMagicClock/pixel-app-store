#!/usr/bin/env python3
"""
ESP32 字体预览服务器
自动扫描 data_littlefs/fonts 目录并提供网页预览
"""

import os
import json
from pathlib import Path
from http.server import HTTPServer, SimpleHTTPRequestHandler
import urllib.parse

class FontServerHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        self.font_dir = Path(__file__).parent.parent / "data_littlefs" / "fonts"
        super().__init__(*args, directory=str(Path(__file__).parent), **kwargs)
    
    def do_GET(self):
        if self.path == '/api/fonts':
            self.send_font_list()
        elif self.path.startswith('/api/font/'):
            self.send_font_file()
        else:
            super().do_GET()
    
    def send_font_list(self):
        """返回字体列表 JSON"""
        fonts = []
        if self.font_dir.exists():
            for font_path in self.font_dir.glob("*.ttf"):
                size = font_path.stat().st_size
                fonts.append({
                    'name': font_path.stem,
                    'filename': font_path.name,
                    'size': size,
                    'size_kb': f"{size / 1024:.1f}"
                })
        
        fonts.sort(key=lambda x: x['size'])
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(json.dumps(fonts).encode())
    
    def send_font_file(self):
        """返回字体文件"""
        filename = urllib.parse.unquote(self.path.split('/')[-1])
        font_path = self.font_dir / filename
        
        if font_path.exists() and font_path.suffix == '.ttf':
            self.send_response(200)
            self.send_header('Content-Type', 'font/ttf')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            with open(font_path, 'rb') as f:
                self.wfile.write(f.read())
        else:
            self.send_error(404, 'Font not found')

def main():
    port = 8080
    server = HTTPServer(('localhost', port), FontServerHandler)
    
    print("=" * 60)
    print("🚀 ESP32 字体预览服务器已启动")
    print("=" * 60)
    print(f"📂 字体目录: {Path(__file__).parent.parent / 'data_littlefs' / 'fonts'}")
    print(f"🌐 访问地址: http://localhost:{port}/font_viewer.html")
    print("=" * 60)
    print("按 Ctrl+C 停止服务器")
    print()
    
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n\n✅ 服务器已停止")

if __name__ == '__main__':
    main()
