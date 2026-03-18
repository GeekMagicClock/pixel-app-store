#!/usr/bin/env python3
import os
import sys
import subprocess
import time

# 配置参数
LOCAL_DIR = os.path.dirname(os.path.abspath(__file__))
REMOTE_USER = "root"  # 如有需要请修改
REMOTE_HOST = "111.229.177.3"
REMOTE_PORT = 22
REMOTE_BASE = "/root/fw/pixel64x32V2/apps"  # 绝对路径，确保有权限
REMOTE_HTTP_PORT = 8001

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
# 先杀掉旧的http.server
kill_cmd = f"pkill -f 'http.server {REMOTE_HTTP_PORT}' || true"
# 启动新的http.server
web_cmd = f"cd {REMOTE_BASE} && nohup python3 -m http.server {REMOTE_HTTP_PORT} --bind 0.0.0.0 >web.log 2>&1 &"
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
print(f"[3/3] 发布完成！Web服务地址：http://{REMOTE_HOST}:{REMOTE_HTTP_PORT}/")
print("接口示例：")
print(f"- 应用列表: http://{REMOTE_HOST}:{REMOTE_HTTP_PORT}/ （目录浏览）")
print(f"- 单个app下载: http://{REMOTE_HOST}:{REMOTE_HTTP_PORT}/your_app.py")
print("- f.html 可通过fetch目录或文件列表实现检索和下载。")
