# 字体文件映射说明

## 新添加的字体（已复制为f##.ttf格式）

为了兼容现有的自动扫描测试程序，新字体已复制为以下编号：

### f10.ttf = VT323-Regular.ttf 【最简洁】
- **特点**：经典终端字体，线条极简，像素占用最少
- **适用**：小文本、数字、价格显示
- **推荐尺寸**：8-10px

### f13.ttf = ShareTechMono-Regular.ttf 【等宽清晰】
- **特点**：等宽字体，数字对齐整齐，清晰易读
- **适用**：数字表格、代码、股票代码
- **推荐尺寸**：8px

### f14.ttf = PressStart2P-Regular.ttf 【粗体醒目】
- **特点**：复古像素游戏风格，天然粗体，非常醒目
- **适用**：重要信息、标题、时间显示
- **推荐尺寸**：8px
- **注意**：字母较粗，适合大号显示

### f15.ttf = Silkscreen-Bold.ttf 【像素粗体】
- **特点**：像素风格粗体，方正清晰，不带圆角和渐变
- **适用**：大信息显示、百分比、涨跌幅
- **推荐尺寸**：8px

## 现有字体编号（供参考）

- f01.ttf = Andale Mono
- f02.ttf = Montserrat-Medium
- f03.ttf = Montserrat-Bold
- f04.ttf = Ubuntu-Medium
- f05.ttf = Lato-Regular
- f06.ttf = unscii-8
- f07.ttf = test_gpos_one
- f08.ttf = font_montserrat_24
- f09.ttf = (未知)
- f11.ttf = (未知)
- f12.ttf = (未知)

## 测试方法

1. 上传文件系统：
```bash
pio run -e hub75_idf -t uploadfs
```

2. 重新编译并上传固件：
```bash
pio run -e hub75_idf -t upload
```

3. 监视串口输出：
```bash
pio device monitor
```

## 观察要点

系统会自动循环扫描所有 f##.ttf 文件，每个字体显示约4秒。
重点关注：
- **f10.ttf (VT323)** - 看是否够简洁清晰
- **f13.ttf (ShareTechMono)** - 看数字是否对齐整齐
- **f14.ttf (PressStart2P)** - 看是否够粗够醒目
- **f15.ttf (Silkscreen)** - 看像素风格是否合适

## 字体选择建议

根据测试效果，可以选择：
1. **小文本首选**：f10 (VT323) 或 f13 (ShareTechMono)
2. **大号信息首选**：f14 (PressStart2P) 或 f15 (Silkscreen)
3. **避免使用**：带圆角和渐变的字体（如f02, f03等）

## 后续调整

如果某个字体不满意，可以替换对应的 f##.ttf 文件，然后重新上传文件系统即可。
