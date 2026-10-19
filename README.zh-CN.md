# Canon CanoScan LiDE 25 Windows 10 x64 驱动

[English](README.md) | 简体中文

本驱动为 **Canon CanoScan LiDE 25** 提供 **WIA 2.0** 扫描接口，供 **Windows 10 x64** 上的扫描软件和图像处理软件调用。独立安装包包含所需的用户态组件，并使用 Windows 自带的 USB 扫描支持。安装时无需预装佳能旧驱动、联网、安装 Python 或另行下载运行库。

## 下载与安装

1. 打开 [Releases 下载页](https://github.com/Jimi-from-mars/Canon-Scanner-LiDE-25-Driver-Windows10.x64/releases)，下载 `LiDE25-Win10-x64-Setup-0.2.0-rc1.exe`。
2. 解开扫描仪底部的运输锁，连接 USB 线，关闭扫描软件。
3. 运行安装程序，允许 Windows 提示的管理员权限。
4. 重新打开扫描软件，从软件的 **WIA** 获取入口选择扫描仪。

**XnView Classic** 的使用入口：**文件 → 从扫描仪或摄像头（WIA）获取 → 导入 → 扫描**。菜单名称可能随语言版本略有不同。仅支持 TWAIN 数据源的软件无法直接调用此驱动。

安装程序目前没有代码签名，Windows 可能显示“未知发布者”提示。此软件包为候选版本，未经 WHQL 认证。使用时无需开启测试模式、导入根证书或修改 Windows 驱动签名策略。

## 兼容范围与扫描功能

| 项目 | 支持范围 |
| --- | --- |
| 扫描仪 | CanoScan LiDE 25，USB 标识 `04A9:2220` |
| 操作系统 | Windows 10，Intel/AMD x64 |
| 软件接口 | WIA 2.0；兼容的 32 位软件可通过 Windows WIA 服务调用 |
| 分辨率 | 75、150、300、600、1200 dpi |
| 图像模式 | RGB 彩色、灰度、1 位黑白 |
| 扫描区域 | 最大约 215 × 296.9 毫米 |
| 传输格式 | BMP；由调用软件另存或转换格式 |

不支持 Windows 11、ARM64、32 位 Windows、其他扫描仪型号、机身按键或原生 TWAIN 数据源。安装包不包含独立扫描软件。安装时请只连接一台 LiDE 25。

## 卸载与常见问题

在 Windows **设置 → 应用 → 应用和功能** 中找到 **LiDE 25 离线扫描驱动**，选择卸载。卸载前请连接安装时使用的同一台扫描仪，并结束正在进行的扫描。卸载程序会恢复安装时备份的设备设置。

如果软件中找不到扫描仪，请重新连接 USB 线、退出后重新打开软件，并确认使用的是 WIA 获取入口。如果其他软件正在扫描，请结束扫描后重试。安装或卸载失败时，请保留安装目录及其中的恢复文件。`setup-state.json` 和安装日志包含安装所在电脑的专用信息，请勿公开上传，也不要复制到其他电脑使用。

## 从源码构建

从 Releases 下载与安装包对应的源码归档，或克隆本仓库。请在 Windows 上准备：

- Python 3.10 或更新版本，可通过 `python` 命令调用。
- x86_64 MinGW-w64 工具链，包含 GCC、G++、`windres` 和 Windows 导入库，并加入 `PATH`。
- 支持 Unicode 的 NSIS 3，将 `makensis.exe` 加入 `PATH`；也可将便携版放在 `toolchain/nsis-<版本号>/` 下。

在仓库根目录运行：

```powershell
python -B tools/build.py
python -B tools/build_installer.py
```

驱动输出为 `artifacts/native/Lide25Wia.dll`，独立安装程序输出为 `release/LiDE25-Win10-x64-Setup-0.2.0-rc1.exe`。源码已包含构建所需的 SANE 后端及参考头文件，构建脚本不会下载依赖。准备编译器、Python 和 NSIS 与安装已编译好的驱动是两回事；普通使用者只需下载 EXE。

首次完整构建后，如只修改了 WIA DLL，可运行：

```powershell
python -B tools/build.py --wia-only
```

修改 DLL 后必须重新构建安装程序，以更新完整性校验。生成对应源码归档与 SHA-256 校验清单：

```powershell
python -B tools/package_offline_release.py
```

## 校验下载文件

计算安装程序或源码归档的 SHA-256，与同一版本中的 `SHA256SUMS-0.2.0-rc1.txt` 对比：

```powershell
Get-FileHash .\LiDE25-Win10-x64-Setup-0.2.0-rc1.exe -Algorithm SHA256
```

## 许可证

请参阅 [LICENSE](LICENSE) 和 `third_party/` 中的声明。SANE plustek 后端保留其原始许可证与版权声明，安装包包含适用的第三方许可。分发二进制安装包时，请同时提供对应源码与许可证。本项目独立维护，并非佳能官方产品。
