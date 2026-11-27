# 常见问题

[English](FAQ.md) · [Русский](FAQ.ru.md) · [简体中文](FAQ.zh.md) · [Español](FAQ.es.md)

[完整安装指南（英文）](docs/INSTALLATION.md)

## 1. 首次安装需要什么？

需要系统版本为 **23.0.1** 的 Nintendo Switch 2、**Nintendo Switch 1 版 Civilization VII 1.0.0 实体卡带**、microSD 卡、配有兼容 Wi-Fi 网卡的原生 Linux 电脑，以及从您自己的 Switch 1 获取的 `prod.keys`。启动游戏前，将主机界面语言设为 English，USB 键盘语言设为 English (US)。本版本仅提供 Linux 安装包。

## 2. 应该在哪里设置语言？

界面语言：`HOME → 系统设置 → 主机 → 语言 → English`。USB 键盘：`HOME → 系统设置 → 控制器与配件 → USB 键盘 → English (US)`。请在启动游戏前完成。Windows 和 WSL 不是本版本支持的安装环境。

## 3. SD 压缩包应该解压到哪里？

解压到 SD 卡根目录，使路径直接以 `SD:/iz2s/` 开头。更新时合并文件夹并替换压缩包中同名文件。保留 `Nintendo`、其他应用、BIOS、游戏、存档和设置；保留随附许可证，写入完成后安全弹出。

## 4. 已安装旧版 IZ2S 或使用过其他加载器怎么办？

先更新 SD 文件，再执行完整安装流程。有条件的清理步骤位于 `HOME → System Settings → Data Management → Delete Save Data → Civilization VII → Your profile`。这会删除所选用户的游戏存档数据，请在确认前阅读提示。不能仅凭相同的启动器版本号，就把其他运行时修订版的启动存档视为兼容。

## 5. 连接后还需要手动安装存档吗？

不需要。“连接并安装存档”会恢复电脑 Wi-Fi、验证主机加载器，并自动写入 `IZ2SCIV.Civ7Save`。在完成之前保持 Awaiting payload 画面打开。若安装失败，面板会提供重试；没有收到确认的写入不会被视为成功。

## 6. 自动安装会删除其他游戏存档吗？

当前安装器只替换自己的启动存档，并确认文件系统提交成功。迁移旧加载器时，在系统设置中删除用户的游戏存档是独立操作，会删除该用户的游戏进度。

## 7. 找不到主机 IP 怎么办？

展开“会话状态”正下方的 IP 设置。搜索并验证建议地址，或输入主机屏幕显示的 IP。保持 Awaiting payload 打开，并确认电脑 Wi-Fi 已恢复。FTP 可用并不能证明游戏加载器已准备好。

## 8. 安装后如何启动？

返回 HOME，按 X 关闭 Civilization VII，重新打开游戏并选择 Load Game，此时不要重启主机。按照面板提示等待 IZ2S CORE IS RUNNING，关闭 Civilization VII，然后按住 R 打开一款 Switch 2 游戏。必要时再重复一次按住 R 的启动操作。完整的错误处理与重试步骤见[安装指南](docs/INSTALLATION.md#4-launch-iz2s)。

## 9. 为什么主机已开启 FTP，而面板显示未连接？

这是服务器与客户端的不同状态。用 L + R3 开启主机 FTP，然后在面板点击“连接 FTP”。PC Link 使用 R + L3，并有独立状态。L3/R3 是按下左/右摇杆。以主机显示的地址和端口为准。

## 10. 为什么启动慢，或者出现 sd-verify？

新的着色器或驱动缓存可能需要编译。本修订版修复了缓存替换失败导致每次启动都重新编译的问题。请保留本机缓存，不要复制其他主机的缓存。出现 sd-verify 时，应对照运行时及已安装启动存档的清单检查文件大小与 SHA-256。使用同一套匹配文件，必要时重新安装对应存档。单独出现旧的 RUN25 字样并不能确定实际加载的是旧版本。

RUN33：只需安装一次 SD-flex 启动存档。此后，兼容的启动器、菜单和 ROMFS 更新只需替换 SD 文件，文件大小可以变化。若 bootstrap、ro、core 或启动 ABI 改变，仍需安装对应存档。请复制经过完整校验的整套更新文件。
