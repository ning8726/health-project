# Windows 与 Git 使用说明

这个项目现在可以在 Windows 上上传程序、通过 USB 查看数据，并通过本机
OpenSSH 安装 Yun Wi-Fi 页面。原来的 macOS 和 Yun 用法仍然保留。

## 1. BMX160 加速度计接线

照片中的模块是 7 针 BMX160：

`VCC`、`GND`、`SCL`、`SDA`、`CSB`、`INT1`、`INT2`

它没有 `SDO` 是正常的。当前固件同时支持 ADXL345、BMA150 和 BMX160。

| BMX160 模块 | UNO R4 Minima |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SDA | A2 |
| SCL | A3 |
| CSB | 3.3V |
| INT1、INT2 | 不接 |

不要接 5V。BMX160 的 I2C 地址为 `0x68`，把 `CSB` 拉高即可选择 I2C；
芯片 ID 应为 `0xD8`。接好后，OLED 第 3 页应显示倾斜角，而不是
`-- DEG`。

## 2. 在 Windows 上传 Arduino 程序

1. 打开 Arduino IDE。
2. 在 Boards Manager 中确认已安装 **Arduino UNO R4 Boards**。
3. 在 Library Manager 中安装
   **SparkFun MAX3010x Pulse and Proximity Sensor Library**。
4. 打开 `R4_Health/R4_Health.ino`。
5. 选择开发板 **Arduino UNO R4 Minima**。
6. 选择 UNO 对应的 COM 端口。
7. 点击 Verify，确认编译成功后点击 Upload。
8. 上传完成后关闭 Arduino IDE 的 Serial Monitor，否则 Windows 检查程序
   无法打开同一个串口。

## 3. 在 Windows 查看实时数据

最方便的方法是双击：

`windows/Run_Health_Checker.bat`

第一次运行会安装 `pyserial`，以后会直接启动。浏览器会打开：

`http://127.0.0.1:8770/`

如果自动检测失败，先列出串口：

```powershell
py -3 mac\check_health.py --list-ports
```

然后指定实际端口：

```powershell
py -3 mac\check_health.py --usb COM3
```

如果 8770 被占用：

```powershell
py -3 mac\check_health.py --usb COM3 --port 8771
```

不连接硬件也可以检查网页界面：

```powershell
py -3 mac\check_health.py --demo
```

页面会明确显示 `SIMULATED DATA`，这些不是传感器读数。

### 用手机查看

让手机和 Windows 电脑连接同一个 Wi-Fi，然后双击：

`windows/Run_Phone_Viewer.bat`

命令窗口会打印类似下面的地址：

```text
Phone URL (same Wi-Fi): http://192.168.1.25:8770/
```

在手机浏览器输入这个完整地址。不要使用 `127.0.0.1`，它只代表手机自己。
Windows 防火墙第一次询问时，只允许“专用网络”。公共校园 Wi-Fi 可能禁止
设备互相访问，这种情况下改用手机热点，让电脑连接该热点。

### 手机和电脑不在同一个网络

不要直接把 8770 端口映射到公网。推荐在 Windows 和手机上安装 Tailscale，
并在两台设备登录同一个 Tailscale 账号。电脑继续运行
`windows/Run_Phone_Viewer.bat`，然后在电脑 PowerShell 中运行：

```powershell
tailscale ip -4
```

假设结果显示 `100.80.20.10`，手机浏览器访问：

```text
http://100.80.20.10:8770/
```

Tailscale 会在两台设备之间建立加密的私有连接，不需要路由器端口转发。
Yun 的 `/workout/` 页面同样可以只通过 Tailscale 或 Yun 本身的局域网访问。
公网临时链接服务会暴露实时数据，这个项目默认不提供公网发布。

## 4. 从 Windows 安装 Yun Wi-Fi 页面

Windows 10/11 通常已经带有 `ssh` 和 `scp`。在项目目录打开 PowerShell：

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\install_to_yun.ps1 -YunHost 192.168.240.1
```

旧版 Dragino OpenWrt 固件可能只支持 `diffie-hellman-group14-sha1`。
Windows 安装脚本已经显式启用与该旧 Yun 通信所需的兼容选项；这些选项
只作用于连接到 Yun 的本次 SSH/SCP 命令。

把 `192.168.240.1` 换成 Yun 当前 IP。SSH 会提示输入 Yun 的 root 密码。
安装后手机访问：

```text
http://Yun的IP/workout/
```

Windows 也可以直接读取 Yun 的 JSON：

```powershell
py -3 mac\check_health.py --wifi http://192.168.240.1/workout/latest.json
```

## 5. 把项目放到 GitHub

先在 GitHub 网页新建一个空仓库，建议使用 Private。不要勾选自动生成
README、`.gitignore` 或 License，否则第一次推送可能发生冲突。

假设仓库地址是：

```text
https://github.com/你的用户名/health-project.git
```

在 PowerShell 中执行：

```powershell
cd "$env:USERPROFILE\Desktop\Health_Project"
git remote add origin https://github.com/你的用户名/health-project.git
git branch -M main
git push -u origin main
```

如果提示 `remote origin already exists`，改用：

```powershell
git remote set-url origin https://github.com/你的用户名/health-project.git
git push -u origin main
```

第一次推送时，Git Credential Manager 通常会打开浏览器让你登录 GitHub。
不要把 GitHub 密码、Token 或 Yun root 密码写进项目文件。

## 6. 用 Git 管理 Arduino 版本

查看当前修改：

```powershell
git status
git diff
```

完成一个可用版本后提交：

```powershell
git add R4_Health/R4_Health.ino
git commit -m "Add BMX160 accelerometer support"
git push
```

如果要一次提交所有已检查的修改：

```powershell
git add .
git commit -m "Describe the change"
git push
```

查看历史：

```powershell
git log --oneline --decorate --graph -10
```

做新功能时先建分支：

```powershell
git switch -c feature/new-feature
```

确认版本可以用于演示后创建标签：

```powershell
git tag -a v0.1.0 -m "First working prototype"
git push --tags
```

提交前发现文件改错，可以安全恢复该文件：

```powershell
git restore R4_Health/R4_Health.ino
```

提交前一定要先看 `git status` 和 `git diff`。`.gitignore` 已排除 Python
缓存、编译产物、日志和本地编辑器设置。
