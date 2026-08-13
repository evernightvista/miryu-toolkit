# Miryu Toolkit

`miryu-toolkit` 是一个使用 Qt6 与 KDE Frameworks 6 编写的 Miryu 图形工具箱。

## 功能

- 图一风格的标签页式管理界面。
- “杂项”板块中的“额外组件”管理：
  - 安装 / 卸载 Wine 组件：`terra-wine-dxvk-d3d9`、`terra-wine-dxvk`、`terra-wine-dxvk-d3d10`、`winetricks-git`、`wineasio`
  - 安装 / 卸载 Steam：`steam`
  - 安装 / 卸载 MIDI 播放支持：`gstreamer1-plugins-bad-free-fluidsynth`
  - 安装 / 卸载补充字体：`google-noto-sans-fonts.noarch`、`google-noto-sans-mono-fonts.noarch`、`google-noto-serif-fonts.noarch`、`google-noto-serif-cjk-vf-fonts`、`google-noto-sans-mono-cjk-vf-fonts`
- 使用 `rpm -q` 检测软件包状态：
  - 全部已安装时，“安装”按钮不可用，“卸载”按钮可用。
  - 全部未安装时，“安装”按钮可用，“卸载”按钮不可用。
  - 部分安装时，安装与卸载按钮都可用，便于补齐或清理。
- 使用 `pkexec` 调用受限的专用 helper 执行提权安装和卸载。
- 每个额外组件都有独立 polkit 认证文案：
  - Wine：`安装Wine需要认证`
  - Steam：`安装Steam需要认证`
  - MIDI：`安装MIDI播放需要认证`
  - 补充字体：`安装补充字体需要认证`
- “系统范围环境变量”板块：
  - 读取 `/etc/environment`
  - 添加、编辑、删除 `NAME=VALUE` 格式变量
  - 使用 `pkexec install -m 0644` 提权写回 `/etc/environment`
  - 支持恢复默认环境变量（fcitx 输入法相关）
- “收集系统日志”按钮（刷新状态按钮左侧）：
  - 收集系统信息（uname、os-release、硬件信息等）
  - 收集 dnf5 日志（`/var/log/dnf5.log`）
  - 收集 Anaconda 安装器日志（`/var/log/anaconda`）
  - 使用 zstd（或 gzip 回退）压缩为 tar 归档，保存到桌面
  - 通过 polkit 提权执行（标题：需要认证以收集系统日志）

## 构建

安装依赖后执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

运行：

```bash
./build/miryu-toolkit
```

安装：

```bash
sudo cmake --install build
```

## RPM 打包

Spec 文件位于 `rpm/miryu-toolkit.spec`。生成源码包后可使用：

```bash
rpmbuild -ba rpm/miryu-toolkit.spec
```

## 运行要求

- Qt 6.5 或更新版本
- KDE Frameworks 6
- `rpm`
- `dnf`
- `pkexec` / polkit

环境变量修改写入 `/etc/environment` 后，需要注销并重新登录才能生效。
