#!/usr/bin/env python3
import os
import shlex
import sys
import subprocess
import time

# 配置参数
LOCAL_DIR = os.path.dirname(os.path.abspath(__file__))
REMOTE_USER = "root"  # 如有需要请修改
REMOTE_HOST = "111.229.177.3"
PUBLIC_HTTP_HOST = "ota.geekmagic.cc"
REMOTE_PORT = 22
REMOTE_BASE = "/root/fw/pixel64x32V2/apps"  # 绝对路径，确保有权限
REMOTE_HTTP_PORT = 8001

CORS_SERVER_SCRIPT = r"""
import functools
import http.server
import os
import sys
import urllib.parse
from http import HTTPStatus

port = int(sys.argv[1])
bind = sys.argv[2]
directory = sys.argv[3]

class CORSRequestHandler(http.server.SimpleHTTPRequestHandler):
    def _gzip_index_path(self):
        req_path = urllib.parse.urlsplit(self.path).path or "/"
        if not req_path.endswith("/apps-index.json") and req_path != "/apps-index.json":
            return None
        fs_path = self.translate_path(req_path)
        gz_path = fs_path + ".gz"
        if not os.path.isfile(gz_path):
            return None
        return gz_path

    def send_head(self):
        gz_path = self._gzip_index_path()
        if gz_path:
            f = open(gz_path, "rb")
            try:
                fs = os.fstat(f.fileno())
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Encoding", "gzip")
                self.send_header("Content-Length", str(fs.st_size))
                self.send_header("Cache-Control", "public, max-age=60, no-transform")
                self.send_header("Vary", "Accept-Encoding")
                self.end_headers()
                return f
            except Exception:
                f.close()
                raise
        return super().send_head()

    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, HEAD, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

handler = functools.partial(CORSRequestHandler, directory=directory)
http.server.ThreadingHTTPServer((bind, port), handler).serve_forever()
""".strip()

# 1. 上传本地目录到服务器
print("[1/3] 正在通过scp上传应用目录...")
scp_cmd = [
    "scp", "-r", "-P", str(REMOTE_PORT),
    LOCAL_DIR + "/", f"{REMOTE_USER}@{REMOTE_HOST}:{REMOTE_BASE}/"
]
ret = subprocess.call(scp_cmd)
if ret != 0:
    print("SCP上传失败，请检查网络和权限。")
    sys.exit(1)
print("上传完成。")

# 2. 远程启动web服务
print(f"[2/3] 正在远程启动web服务 (http.server:{REMOTE_HTTP_PORT}) ...")
# 先杀掉旧的静态服务进程
kill_cmd = f"pkill -f 'python3 .* {REMOTE_HTTP_PORT} 0.0.0.0 {REMOTE_BASE}' || true; pkill -f 'http.server {REMOTE_HTTP_PORT}' || true"
# 启动新的带 CORS 头的静态服务
web_cmd = (
    "nohup python3 -c "
    + shlex.quote(CORS_SERVER_SCRIPT)
    + f" {REMOTE_HTTP_PORT} 0.0.0.0 {REMOTE_BASE} >{REMOTE_BASE}/web.log 2>&1 &"
)
ssh_cmd = [
    "ssh", "-p", str(REMOTE_PORT), f"{REMOTE_USER}@{REMOTE_HOST}",
    f"{kill_cmd}; {web_cmd}"
]
ret = subprocess.call(ssh_cmd)
if ret != 0:
    print("远程web服务启动失败，请检查ssh和python环境。")
    sys.exit(2)
print("Web服务已启动。")

# 3. 输出接口说明
print(f"[3/3] 发布完成！Web服务地址：http://{PUBLIC_HTTP_HOST}:{REMOTE_HTTP_PORT}/")
print("接口示例：")
print(f"- 应用列表: http://{PUBLIC_HTTP_HOST}:{REMOTE_HTTP_PORT}/ （目录浏览）")
print(f"- 压缩索引: http://{PUBLIC_HTTP_HOST}:{REMOTE_HTTP_PORT}/apps-index.json （gzip 传输）")
print(f"- 单个app下载: http://{PUBLIC_HTTP_HOST}:{REMOTE_HTTP_PORT}/your_app.py")
print("- f.html 可通过fetch目录或文件列表实现检索和下载。")
