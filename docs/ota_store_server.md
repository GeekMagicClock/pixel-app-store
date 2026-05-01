# OTA Store Server

`scripts/ota_store_server.py` 提供产品化需要的三类接口：

- `GET /api/store/index`：返回 `stable/beta` 双索引配置（给设备前端读取）
- `GET /apps/<board>/<channel>/apps-index.json`：返回目录索引
- `GET /apps/<board>/<channel>/redirect/<zip_name>`：302 跳转到 GitHub Releases（或其他对象存储）

## 目录约定

默认 `--data-root dist/store`，并按以下结构读取：

```text
dist/store/
  pixel64x32V2/
    stable/
      apps-index.json
      thumbs/
    beta/
      apps-index.json
      thumbs/
```

## 启动示例

```bash
export PIXEL_STORE_GITHUB_REPO=GeekMagicClock/pixel-app-store
export PIXEL_STORE_GITHUB_BRANCH=main
export PIXEL_STORE_GITHUB_DIST_PREFIX=dist/store
python3 scripts/ota_store_server.py \
  --host 0.0.0.0 \
  --port 8001 \
  --board pixel64x32V2 \
  --data-root dist/store \
  --public-base https://ota.geekmagic.cc/apps \
  --default-channel stable \
  --github-repo "$PIXEL_STORE_GITHUB_REPO" \
  --github-branch "$PIXEL_STORE_GITHUB_BRANCH" \
  --github-dist-prefix "$PIXEL_STORE_GITHUB_DIST_PREFIX"
```

如果你已经想好下载目标，也可以继续显式指定：

```bash
export PIXEL_STORE_STABLE_ZIP_BASE=https://github.com/your-org/your-repo/releases/download/pixel64x32V2-stable
export PIXEL_STORE_BETA_ZIP_BASE=https://github.com/your-org/your-repo/releases/download/pixel64x32V2-beta
```

优先级是：

1. 显式 `PIXEL_STORE_STABLE_ZIP_BASE` / `PIXEL_STORE_BETA_ZIP_BASE`
2. `PIXEL_STORE_GITHUB_REPO` + `PIXEL_STORE_GITHUB_BRANCH` + `PIXEL_STORE_GITHUB_DIST_PREFIX`

## GitHub 仓库接入

如果你的仓库结构是：

```text
GeekMagicClock/pixel-app-store
  dist/store/
    pixel64x32V2/
      stable/
      beta/
```

那服务器会自动把 ZIP 目标推导成：

```text
https://raw.githubusercontent.com/GeekMagicClock/pixel-app-store/main/dist/store/pixel64x32V2/apps/<zip_name>
```

也就是说：

1. 你只需要更新 GitHub 仓库里的 `dist/store`。
2. 服务器端只负责读索引和把 `redirect/<zip>` 转发到 GitHub。
3. 将来如果想切换到 GitHub Releases、R2、S3 或自建 CDN，只要改环境变量里的 `ZIP_BASE`，前端和设备端都不用改。

## 维护方式建议

1. `stable` 和 `beta` 保持两个独立目录。
2. 每次发布只更新对应 channel 的 `apps-index.json` 和 ZIP。
3. 回滚时直接把 `apps-index.json` 指回旧 ZIP。
4. 如果后面要做自动化，最稳的是在 `pixel-app-store` 仓库里加 GitHub Action：
   - 构建 ZIP
   - 生成 `apps-index.json`
   - 提交到 `dist/store`
   - 或打 release/tag 并把 `ZIP_BASE` 切到 Releases

## GitHub Action

仓库里已经补了一份工作流模板：

- [`.github/workflows/publish-app-store.yml`](/Users/ifeng-macmini/develop/project/esp32-pixel/.github/workflows/publish-app-store.yml)

它会在 `main` 分支的相关文件变更后：

1. 构建 `stable` 和 `beta` 目录。
2. 重新生成 `dist/store`。
3. 把更新后的产物提交回仓库。

这样 `raw.githubusercontent.com/GeekMagicClock/pixel-app-store/...` 这条链路就能持续保持最新。

## 一键发布

如果你希望 OTA 站点和 GitHub 同步更新，推荐直接跑：

```bash
scripts/publish_app_store_all.sh --github-worktree ../pixel-app-store --push
```

这个脚本会：

1. 先构建 stable/beta。
2. 让 `zip_url` 和 `thumbnail_url` 都指向 GitHub raw。
3. 同时上传 OTA 站点的 `dist/store`。
4. 把同一份 `dist/store` 同步到 `pixel-app-store` 的 git 工作区并 push。

如果你暂时只想更新 OTA，不推 GitHub，可以用：

```bash
scripts/publish_app_store_all.sh --ota-only
```

## 发布脚本配套

`scripts/publish_apps.py` 新增参数：

- `--channel stable|beta`
- `--zip-url-mode redirect|direct`（默认 `redirect`）
- `--base-url` 支持 `{board}` 和 `{channel}` 占位符

推荐：

```bash
python3 scripts/publish_apps.py --channel stable --zip-url-mode redirect
python3 scripts/publish_apps.py --channel beta --zip-url-mode redirect
```

这样生成的 `zip_url` 会是：

`https://ota.geekmagic.cc/apps/<board>/<channel>/redirect/<zip_name>`

由服务端再 302 到 GitHub。
