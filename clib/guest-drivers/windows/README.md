# 紫霄 Windows VirtIO 驱动

Windows 平台的 VirtIO 半虚拟化驱动程序，用于 Zixiao Hypervisor 管理的虚拟机。

## 驱动列表

| 驱动 | 说明 | 设备类型 |
|------|------|----------|
| zviopci | VirtIO PCI 总线驱动 | 系统设备 |
| zvioblk | VirtIO 块存储驱动 | 磁盘驱动器 |
| zvionet | VirtIO 网络驱动 | 网络适配器 |
| zviobln | VirtIO 内存气球驱动 | 系统设备 |

## 快速开始

### 方式一：使用安装程序（推荐）

1. 从 [GitHub Releases](https://github.com/your-org/hypervisor/releases) 下载最新驱动包
2. 解压后，以**管理员身份**双击运行 `installer\install.cmd`
3. 安装程序将自动：
   - 安装驱动签名证书到受信任发布者
   - 安装所有 VirtIO 驱动程序

```powershell
# 或者使用 PowerShell
.\installer\Install-ZixiaoDrivers.ps1
```

### 方式二：手动安装证书和驱动

```powershell
# 1. 导入证书
certutil -addstore "TrustedPublisher" certs\zixiao-driver-signing.cer
certutil -addstore "Root" certs\zixiao-driver-signing.cer

# 2. 安装驱动
pnputil /add-driver zviopci\zviopci.inf /install
pnputil /add-driver zvioblk\zvioblk.inf /install
pnputil /add-driver zvionet\zvionet.inf /install
pnputil /add-driver zviobln\zviobln.inf /install
```

### 验证安装

```powershell
# 查看已安装的紫霄驱动
Get-WmiObject Win32_PnPSignedDriver | Where-Object { $_.DeviceName -like "*Zixiao*" }
```

## 生产环境部署

### 证书注入方式

本驱动使用自签名证书，适用于以下场景：

1. **Hypervisor 预装镜像**：在创建 Windows VM 镜像时预先注入证书
2. **企业内部 CA**：使用企业 PKI 基础设施签发的证书
3. **Ansible/Terraform 自动化**：通过配置管理工具自动安装

### 生产环境建议

对于生产环境，建议采用以下方式之一：

| 方式 | 适用场景 | 说明 |
|------|----------|------|
| WHQL 认证 | 公开发布 | 获取微软 WHQL 认证，无需安装证书 |
| EV 代码签名 | 商业部署 | 购买 EV 代码签名证书 |
| 企业 CA | 企业内部 | 使用企业 PKI 签发证书 |
| 自签名 + 预装 | 私有云 | 在 VM 镜像中预装证书（当前方式） |

## 开发者指南

### 编译环境

- Visual Studio 2022
- Windows SDK (最新版本)
- Windows Driver Kit (WDK)

详细安装步骤参见 [WDK 安装指南](https://docs.microsoft.com/windows-hardware/drivers/download-the-wdk)

### 编译驱动

```powershell
# 使用 Visual Studio
msbuild zviopci\zviopci.vcxproj /p:Configuration=Release /p:Platform=x64

# 编译所有驱动
$drivers = @("zviopci", "zvioblk", "zvionet", "zviobln")
foreach ($d in $drivers) {
    msbuild "$d\$d.vcxproj" /p:Configuration=Release /p:Platform=x64
}
```

### 生成签名证书

```powershell
cd signing
.\generate-cert.ps1
```

生成的文件：
- `certs\zixiao-driver-signing.pfx` - 私钥（勿提交到 Git）
- `certs\zixiao-driver-signing.cer` - 公钥证书（随驱动分发）
- `certs\thumbprint.txt` - 证书指纹（用于 CI/CD）

### 签名驱动

```powershell
cd signing

# 使用 PFX 文件签名
.\sign-drivers.ps1 -PfxPath ..\certs\zixiao-driver-signing.pfx

# 或使用证书指纹签名（证书已安装到证书存储）
.\sign-drivers.ps1 -CertThumbprint "ABCD1234..."

# 指定平台
.\sign-drivers.ps1 -PfxPath ..\certs\zixiao-driver-signing.pfx -Platform ARM64
```

### 创建发布包

```powershell
# 打包所有驱动和安装程序
.\installer\Create-DriverPackage.ps1 -Version "1.0.0"
```

## 目录结构

```
clib/guest-drivers/windows/
├── signing/                    # 签名工具
│   ├── generate-cert.ps1       # 生成自签名证书
│   └── sign-drivers.ps1        # 签名驱动
├── installer/                  # 安装程序
│   ├── Install-ZixiaoDrivers.ps1   # 安装脚本
│   ├── Uninstall-ZixiaoDrivers.ps1 # 卸载脚本
│   └── install.cmd             # 快速安装入口
├── certs/                      # 证书目录
│   └── .gitignore              # 排除私钥文件
├── zviopci/                    # PCI 总线驱动
├── zvioblk/                    # 块存储驱动
├── zvionet/                    # 网络驱动
├── zviobln/                    # 内存气球驱动
└── README.md                   # 本文档
```

## 卸载驱动

```powershell
# 使用卸载脚本
.\installer\Uninstall-ZixiaoDrivers.ps1

# 同时移除证书
.\installer\Uninstall-ZixiaoDrivers.ps1 -RemoveCertificate
```

## 故障排除

### 驱动未加载

1. 确认证书已正确安装：
   ```powershell
   Get-ChildItem Cert:\LocalMachine\TrustedPublisher | Where-Object { $_.Subject -like "*Zixiao*" }
   ```

2. 检查设备管理器中是否有未识别的 VirtIO 设备

3. 查看系统事件日志中的驱动相关错误

### 签名验证失败

确保：
- 证书已添加到 `TrustedPublisher` 和 `Root` 存储
- 驱动文件未被修改（校验 SHA256）
- 系统时间正确（证书有效期检查）

## 安全注意事项

- **私钥保护**：`*.pfx` 文件包含私钥，切勿提交到版本控制
- **校验完整性**：从 GitHub Release 下载驱动时，请验证 SHA256 校验和
- **信任链**：自签名证书仅适用于受控环境，公开发布建议使用 WHQL 认证

## 许可证

Apache License 2.0

Copyright (c) 2025 Zixiao System
