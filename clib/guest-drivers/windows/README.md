## 紫霄Windows VirtIO驱动

⚠️ **警告**：需要使用Visual Studio 2022 + WDK编译

## 编译

### 1. 安装依赖

**步骤 1：安装 Visual Studio 2022**

WDK 需要 Visual Studio。有关 Visual Studio 系统要求的详细信息，请参阅 [Visual Studio 2022 系统要求](https://docs.microsoft.com/visualstudio/releases/2022/system-requirements)。

> **重要**：目前不要将 Visual Studio 2026 用于 Windows 驱动程序开发。WDK 尚未使用 Visual Studio 2026 进行验证，并且不保证兼容性。继续使用 Visual Studio 2022 进行所有驱动程序开发。当 WDK 正式支持 Visual Studio 2026 时，我们将更新此页面。

Visual Studio 2022 Community、Professional 或 Enterprise 版本支持此版本的驱动程序开发。

[下载 Visual Studio 2022](https://visualstudio.microsoft.com/vs/)

安装 Visual Studio 2022 时，请选择 **具有C++工作负荷的桌面开发**。然后，在"单个组件"下添加：

- MSVC v143 - VS 2022 C++ ARM64/ARM64EC Spectre 缓解库（最新版本）
- MSVC v143 - VS 2022 C++ x64/x86 Spectre 缓解库（最新版本）
- 带有 Spectre 缓解库的适用于最新 v143 生成工具的 C++ ATL (ARM64/ARM64EC)
- 带有 Spectre 缓解库的适用于最新 v143 生成工具的 C++ ATL (x86 & x64)
- 带有 Spectre 缓解库的适用于最新 v143 生成工具的 C++ MFC (ARM64/ARM64EC)
- 适用于最新 v143 生成工具且带有 Spectre 漏洞缓解措施的 C++ MFC (x86 & x64)
- Windows 驱动程序工具包 (WDK)

> **提示**：使用搜索框查找"64 latest spectre"（在英文安装中）或"64 最新"（在非英文安装中）以快速查看这些组件。

**步骤 2：安装 SDK**

安装 Visual Studio 不会下载最新的 SDK 版本。使用以下链接安装最新的 SDK 版本：

[下载最新的 Windows SDK](https://developer.microsoft.com/windows/downloads/windows-sdk/)

所提供的 SDK 和 WDK 链接具有匹配的版本号，这对套件的协同工作始终必不可少。如果您决定安装您自己的SDK/WDK对，可能是针对不同的Windows版本，请确保版本号匹配。有关详细信息，请参阅 [工具包版本控制](https://docs.microsoft.com/windows-hardware/drivers/other-useful-resources/kit-version-numbers)。

**步骤 3：安装 WDK**

[下载最新的 WDK](https://docs.microsoft.com/windows-hardware/drivers/download-the-wdk)

从版本 17.11.0 开始，WDK VSIX 作为单个组件包含在 Visual Studio 中。安装 WDK 之前，安装程序会检查是否已安装兼容版本的 VSIX。如果安装程序找不到 WDK VSIX，它会提示你安装它。若要安装 WDK VSIX，请启动 Visual Studio 安装程序，选择"修改"，转到"单个组件"选项卡，添加 Windows 驱动程序工具包，然后选择"修改"。

### 2. 编译驱动程序

1. 打开 Visual Studio 2022
2. 选择 **文件** > **打开** > **项目/解决方案**
3. 导航到驱动程序项目文件夹，选择 `.sln` 解决方案文件
4. 在"解决方案资源管理器"中，右键点击项目，选择 **属性**
5. 配置以下设置：
   - **平台工具集**：选择 v143（VS 2022）
   - **Windows SDK 版本**：选择安装的最新版本
   - **配置**：选择 Release 或 Debug
6. 右键点击项目，选择 **生成** 或按 `Ctrl+B` 编译
7. 编译成功后，驱动程序文件（`.sys`）将生成在输出目录中

### 3. 部署到VM（暂无EV代码签名）

在部署驱动程序到虚拟机之前，请确保已正确安装并配置了驱动程序。由于当前没有EV代码签名，您需要在虚拟机中启用测试模式来加载驱动程序。

本驱动仅支持Windows测试模式，正式使用需自行承担安全风险；若需在生产环境使用，请自行购买EV代码签名证书或通过WHQL认证。

## 部署步骤

### 第一步：在虚拟机中启用测试模式

1. 在虚拟机中以管理员身份打开命令提示符（cmd.exe）或 PowerShell
2. 执行以下命令启用测试模式：
   ```cmd
   bcdedit.exe /set nointegritychecks on
   bcdedit.exe /set testsigning on
   ```
3. 重启虚拟机使设置生效

### 第二步：准备驱动程序文件

1. 复制编译生成的驱动程序文件（`.sys`）到虚拟机
2. 创建驱动程序目录结构：
   - 驱动程序二进制文件（`.sys`）
   - 驱动程序信息文件（`.inf`）
   - 目录文件（`.cat`）

### 第三步：安装驱动程序

**方法一：使用 DevCon 工具**

1. 下载 [Windows Driver Kit 工具](https://docs.microsoft.com/windows-hardware/drivers/download-the-wdk)
2. 在虚拟机中以管理员身份运行：
   ```cmd
   devcon install driver.inf PCI\VEN_xxxx&DEV_xxxx
   ```

**方法二：使用设备管理器手动安装**

1. 右键点击设备，选择 **更新驱动程序**
2. 选择 **浏览我的计算机以查找驱动程序**
3. 选择驱动程序文件所在的目录
4. 完成安装

### 第四步：验证驱动程序

1. 打开设备管理器，查找对应的设备
2. 检查驱动程序是否已正确加载
3. 查看事件查看器中是否有驱动程序相关的错误或警告

### 禁用测试模式（可选）

部署完成后，如需恢复正常模式，执行以下命令：

```cmd
bcdedit.exe /set nointegritychecks off
bcdedit.exe /set testsigning off
```

然后重启虚拟机。

### 安全警告：由于无代码签名，在GitHub Release下载的任何内核模式驱动请务必验证Checksum，以免导致系统损坏或者安全问题