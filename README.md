# Canon CanoScan LiDE 25 Driver for Windows 10 x64

English | [简体中文](README.zh-CN.md)

A WIA 2.0 driver for the **Canon CanoScan LiDE 25**, for use with scanning and image-editing applications on **Windows 10 x64**. The standalone installer includes the required user-mode components and uses the USB scanner support supplied with Windows. No legacy Canon driver, internet connection, Python installation, or additional runtime download is required to install it.

## Download and install

1. Open [Releases](https://github.com/Jimi-from-mars/Canon-Scanner-LiDE-25-Driver-Windows10.x64/releases) and download `LiDE25-Win10-x64-Setup-0.2.0-rc1.exe`.
2. Unlock the scanner's transport lock, connect its USB cable, and close scanning applications.
3. Run the installer and approve the Windows administrator prompt.
4. Reopen your scanning application and choose its **WIA** scanner acquisition option.

In **XnView Classic**, use **File → Acquire from scanner or camera (WIA) → Import → Scan**. Menu wording may vary with the application language. Applications using only a TWAIN source cannot use this driver directly.

The installer is currently unsigned, so Windows may display an unknown-publisher warning. This is a pre-release package, not a WHQL-certified driver. No test-signing mode, imported root certificate, or change to Windows driver-signing policy is required.

## Compatibility and scanning

| Item | Support |
| --- | --- |
| Scanner | CanoScan LiDE 25, USB ID `04A9:2220` |
| Operating system | Windows 10, Intel/AMD x64 |
| Application interface | WIA 2.0; compatible 32-bit applications can use the Windows WIA service |
| Resolution | 75, 150, 300, 600, and 1200 dpi |
| Image modes | RGB color, grayscale, and 1-bit black-and-white |
| Scan area | Up to approximately 215 × 296.9 mm |
| Transfer format | BMP; save or convert the image in your application |

Windows 11, ARM64, 32-bit Windows, other scanner models, hardware buttons, and a native TWAIN data source are not supported. The package does not include a scanning application. Connect only one LiDE 25 during installation.

## Uninstall and troubleshooting

Uninstall **LiDE 25 离线扫描驱动** from Windows **Settings → Apps → Apps & features**. Connect the same scanner and finish any scan before uninstalling. The uninstaller restores the device settings saved during installation.

If the scanner is missing, reconnect its USB cable, close and reopen the application, and select the WIA acquisition menu. End any scan in another application before retrying. If setup or uninstall fails, keep the installation folder and its recovery files. `setup-state.json` and setup logs contain information specific to the computer where the package was installed; do not upload them publicly or copy them to another computer.

## Build from source

Download the matching source archive from Releases or clone this repository. Build on Windows using:

- Python 3.10 or later, available as `python`.
- An x86_64 MinGW-w64 toolchain with GCC, G++, `windres`, and Windows import libraries, available on `PATH`.
- NSIS 3 with Unicode support, available as `makensis.exe` on `PATH`, or a portable copy in `toolchain/nsis-<version>/`.

From the repository root, run:

```powershell
python -B tools/build.py
python -B tools/build_installer.py
```

The driver is written to `artifacts/native/Lide25Wia.dll`. The standalone installer is written to `release/LiDE25-Win10-x64-Setup-0.2.0-rc1.exe`. The source includes the required SANE backend and reference headers; the build scripts do not download dependencies. Obtaining the compiler, Python, or NSIS is separate from installing the finished driver.

For a DLL-only rebuild after the first complete build:

```powershell
python -B tools/build.py --wia-only
```

Rebuild the installer after changing the DLL so its integrity check matches. To create the corresponding source archive and SHA-256 checksum file:

```powershell
python -B tools/package_offline_release.py
```

## Verify a download

Compare the installer or source archive hash with `SHA256SUMS-0.2.0-rc1.txt` from the same release:

```powershell
Get-FileHash .\LiDE25-Win10-x64-Setup-0.2.0-rc1.exe -Algorithm SHA256
```

## License

See [LICENSE](LICENSE) and the notices under `third_party/`. The SANE plustek backend retains its original license and copyright notices. The installer includes applicable third-party notices. Redistribute the corresponding source and license notices together with the binary package. This is an independent project and is not an official Canon product.
