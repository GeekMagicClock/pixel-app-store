# 字体测试更新说明

## 更新内容

为64x32px像素屏添加了4种新的开源字体，适合低分辨率显示：

### 1. VT323-Regular.ttf【最简洁】
- **特点**：经典终端字体，线条极简，像素占用最少
- **适用场景**：小文本、数字、价格显示
- **推荐尺寸**：8-10px
- **示例**：`ABC123 Price:$99 +5.2%`

### 2. ShareTechMono-Regular.ttf【等宽】
- **特点**：等宽字体，数字对齐整齐，清晰易读
- **适用场景**：数字表格、代码、股票代码
- **推荐尺寸**：8px
- **示例**：`0123456789 AAPL $88.88`

### 3. PressStart2P-Regular.ttf【粗体醒目】
- **特点**：复古像素游戏风格，天然粗体，非常醒目
- **适用场景**：重要信息、标题、时间显示
- **推荐尺寸**：8px
- **示例**：`AAPL 88:88`
- **注意**：字母较粗，适合大号显示

### 4. Silkscreen-Bold.ttf【像素粗体】
- **特点**：像素风格粗体，方正清晰，不带圆角和渐变
- **适用场景**：大信息显示、百分比、涨跌幅
- **推荐尺寸**：8px
- **示例**：`BIG INFO 99%`

## 代码更新

### 1. 字体加载 (`lvgl_runtime_fonts.cpp`)
新增了4个字体加载函数：
```cpp
LvglRuntimeFontSimple()    // VT323 - 最简洁
LvglRuntimeFontMono()      // ShareTechMono - 等宽
LvglRuntimeFontBold()      // PressStart2P - 粗体
LvglRuntimeFontPixelBold() // Silkscreen - 像素粗体
```

### 2. 测试界面 (`lvgl_ttf_test_screen.cpp`)
测试界面现在会循环显示7种字体：
1. UNSCII_8 (内置)
2. DIGITS_THIN_8 (内置)
3. TIME_BIG_10 (内置)
4. VT323 [简洁]
5. SHARETECH [等宽]
6. PRESSSTART2P [粗]
7. SILKSCREEN [像素粗]

每种字体会显示1.4秒，自动切换。

## 使用步骤

### 1. 下载字体（已完成）
```bash
cd data_idf/fonts
./download_fonts.sh
```

### 2. 烧录LittleFS分区
```bash
pio run -e hub75_idf -t uploadfs
```

### 3. 编译并上传固件
```bash
pio run -e hub75_idf -t upload
```

### 4. 查看效果
打开串口监视器，字体会自动循环切换显示。

## 字体选择建议

### 小文本、数据密集场景
- **首选**：VT323 或 ShareTechMono
- **优点**：最节省像素，清晰度高
- **示例应用**：股票列表、价格表、多行信息

### 大号醒目信息
- **首选**：PressStart2P 或 Silkscreen Bold
- **优点**：粗体醒目，易于识别
- **示例应用**：时钟、涨跌百分比、重要提示

### 避免使用
- 带圆角的字体（如原来的small.ttf、time.ttf可能有抗锯齿）
- 带渐变的字体
- 太细的字体（64x32屏幕看不清）

## 调整字体大小

如果觉得字体太大或太小，可以修改 `lvgl_runtime_fonts.cpp` 中的加载参数：

```cpp
// 第二个参数是字体大小（单位：px）
lv_tiny_ttf_create_file_ex("S:/littlefs/fonts/xxx.ttf", 
    8,  // <-- 改这里：6-14之间尝试
    LV_FONT_KERNING_NONE, 64);
```

## 许可证

所有字体均为开源字体，遵循 OFL (Open Font License) 许可证。
许可证详情见 `data_idf/fonts/licenses/OFL.txt`

---

**测试建议**：观察每种字体在实际屏幕上的显示效果，选择最适合你应用的字体组合。
