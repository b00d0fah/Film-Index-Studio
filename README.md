# Film Index Studio

独立的 Windows C++ / Qt 索引图软件，当前版本 1.0.1。

## 使用

双击 `FilmIndexStudio.exe`。添加照片或文件夹，把当前这组照片排成一张索引图。

- 保留 9 套胶片模板，每条 6 格，单张最多 42 张。没有批量任务或任务队列。
- 页面中央实时预览：修改参数后防抖 160ms 自动渲染，后台渲染期间仍可编辑。旧结果自动丢弃，以最新设置为准。
- 左侧可拖动排序、上下移动、按文件名自然排序、多选旋转/移除。
- 点击预览中的照片选中；照片页可旋转、改变横纵裁切位置。支持中心裁切 3:2 或完整显示留边。
- EXIF 自动转正；竖幅自动转横可以关闭。手动旋转在自动转正/转横之后应用。
- 带色彩配置的照片转换到 sRGB 后合成，输出标记 sRGB；未标记色彩空间的照片按 sRGB 处理。
- 标题栏显隐、高度、主标题/标题/备注/胶片/ISO/地点/日期/相机/镜头、字号、颜色、边框、间距均可调整。
- 左侧顶部文字支持左对齐、水平居中、右对齐；右侧胶卷信息开启时独占右侧信息栏并保持左对齐，不与左侧文字发生对齐冲突。
- Ctrl + 滚轮缩放，拖动平移，“适应”恢复完整视图。预览选中框不会导出。
- 输出 JPG 或 PNG，宽度 600–9000 px，高度按比例计算；JPEG 质量可调。为限制内存使用，单图不超过 1 亿像素。
- 支持 JPEG、PNG、TIFF、WebP、BMP 等 Qt 图像格式；不支持相机 RAW、HEIC、120 胶片专用模板。
- 保存 `.findex` 项目可恢复参数、照片顺序、旋转和裁切；项目引用原照片路径，不嵌入照片，不依赖数据库。
- 原图不会被修改。读取失败会指出照片；导出通过临时文件原子提交，取消/失败不留下半成品。已有输出文件由保存对话框确认覆盖。

预览采用 1600 像素宽度，导出采用设定分辨率；两者共用同一个 QPainter 排版器。文字极长时省略，标题栏较矮时自动缩小。

预览使用最多约 128 MiB 的解码缓存；照片文件时间或大小改变后自动失效。首次读取大型 TIFF 会比后续参数调整慢。模板上的预印胶片型号/帧号来自原模板 PNG，“胶片型号”字段修改的是标题栏信息。

快捷键：Ctrl+S 保存项目，Ctrl+Shift+S 另存为，Ctrl+O 打开项目，Ctrl+E 导出，照片列表内 Delete 移除。

## 开发与构建

代码为 C++17，使用 Qt 6.8.3 Widgets / Concurrent / Test，MinGW 13.1，CMake / Ninja。

当前机器复用 `Documents/DevTools/TransWithAI` 下的 Qt、MinGW 与 CMake。可通过 `FILM_INDEX_QT`、`FILM_INDEX_MINGW`、`FILM_INDEX_CMAKE` 覆盖对应路径。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Test -Deploy
```

VS Code：Ctrl+Shift+B 构建与测试，F5 调试，运行 `Film Index: Release Package` 生成 `dist/` 便携版。
便携版目录内双击 `FilmIndexStudio.bat` 即可启动，也可以直接运行 EXE。

其他机器可安装 Qt 6.8.3 MinGW 桌面组件、qtimageformats、MinGW 13.1、CMake 与 Ninja，再设置上述路径。Qt 模板资源编译进 EXE，运行不需要 Python、Flask 或 Qt SDK。

## 结构

- `src/document.*`：项目模型、参数验证、JSON 保存/加载。
- `src/renderer.*`：预览与导出共用的图像排版器。
- `src/window.*`：中文原生桌面界面、后台预览与单次导出。
- `tests/test_core.cpp`：模板、几何、裁切、项目、导出、取消与图像插件测试。
- `assets/film_strips/135`：随独立软件发布的胶片资源。

## 来源与许可

这是已修改的独立移植版本，来源为原项目 `FilmLog_project` 的索引图逻辑和胶片模板资源（源提交 `7c9a250`）。独立工作区和本地 Git 历史与原应用分开；原项目文件未修改，也未复制照片、数据库、环境变量或密钥。

保留原项目 `FilmLog Non-Commercial Source License`，见 `LICENSE`。第三方组件及其源码获取方式见 `THIRD_PARTY_NOTICES.md` 与 `licenses/`。
