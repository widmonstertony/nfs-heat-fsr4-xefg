# 《极品飞车：热度》FSR4＋XeFG 桥接

[English](README.md) · [下载实验版 Release](https://github.com/widmonstertony/nfs-heat-fsr4-xefg/releases)

这是给《极品飞车：热度》接入超分和插帧的**实验项目**，在 Windows 下的 MacBookPro16,4／Radeon Pro5600M 上开发。**不是显卡驱动更新，也不是 AMD、Intel、NVIDIA 或 EA 的官方产品。**

## 有什么用

- 拦截游戏主画面的 TAA 通道，获取匹配的深度、运动向量和镜头参数，不是单纯录屏插帧。
- 保留游戏自己的 TAA 历史，将带抖动的线性色调映射画面送入超分，再交给游戏原来的界面合成通道。
- 可选择 FSR3.1，或本机使用的 **FSR4.1.1.2740 Navi12 INT8** 超分。**插帧始终是 XeFG，不是 FSR4 插帧。**
- 匹配到驾驶画面通道后约10秒自动启用，不用按F8，没有两分钟限制。**F9紧急关闭本次效果**，重启游戏才能再次启用。

## 验证范围

**仅限单人／离线测试。不要用于联机或带反作弊的模式。**

已测试游戏 EXE：**1.0.60.7040**。硬件：MacBookPro16,4／Radeon Pro5600M，`VEN_1002&DEV_7360&SUBSYS_021E106B`。其他版本、显卡和驱动没有验证。默认安装器检查此硬件ID；`-AllowUntestedHardware` 只是跳过安装检查，不代表其他硬件兼容。

目前证据：

- FSR3.1＋XeFG 在单人驾驶中连续处理1,800帧，**856×480 → 1280×720**，随后成功释放后端并恢复绘制钩子。
- FSR4.1.1 INT8 独立GPU测试确认了本地FSR4模型和有效超分输出；与XeFG组合的60次应用提交测试记录到36次插帧提交、96帧SDK报告输出。**这不是游戏内FPS跑分。**
- 机主反馈已安装游戏能够运行，但不等于已完成多硬件、长时间稳定性和画质认证。

仍需测试：载入、菜单、改变分辨率后的稳定性，运动拖影、插值HUD、长时间帧率和延迟。原生UI合成得到保留，但不能保证插帧时的HUD质量。后端视角目前固定45°，还没有完整处理动态镜头视角。5600M上FSR4可能比FSR3.1慢。保护机制触发后会停止替换并释放后端，需要重启重试；不会自动把渲染比例恢复100%。

## 下载与安装

1. 从Release下载并解压 **bridge-kit ZIP**。GitHub自动生成的Source code压缩包不包含编译后的插件。
2. 原版游戏至少运行一次再退出，让它生成配置。先还原其他注入MOD：安装器遇到已有 `d3d11.dll`、`dxgi.dll` 或其配置会停止，不强行覆盖。
3. 准备一份符合 `manifest.json` 的**现有可用运行库目录**，并自行遵守各组件许可。公开安装包**不重新分发OptiScaler nightly、Intel运行库DLL或修改后的Navi12 FSR DLL**，只从你提供的目录及子目录中按SHA-256导入。最新的普通OptiScaler压缩包不一定包含这些精确版本。**没有匹配运行库时，仅下载本Release无法独立完成安装。**
4. 双击 `INSTALL.cmd`，输入Heat游戏目录和运行库目录。游戏目录受保护时请从管理员命令提示符运行。
5. 正常启动游戏，选择**单人游戏**。先保持初始窗口模式测试；异常按 **F9**。

也可用PowerShell：

```powershell
.\Install.ps1 -GamePath 'D:\Games\Need for Speed Heat' -RuntimeSource 'D:\MyRuntime' -Upscaler FSR4
```

FSR3.1使用 `-Upscaler FSR3`。已安装后要切换模式，请先恢复再用另一选项安装。`-ValidateOnly` 只验证、不修改游戏或配置。脚本不会联网下载、装驱动、改BCD、关签名检查或修改游戏EXE。

安装器设置窗口模式、自定义画质、TAA和 **66.6667%内部渲染比例**；不主动修改其他画质项或存档。所有安装路径在本机生成，不依赖开发者的用户名和盘符。请保留解压后的安装包以便恢复。

## 恢复原状

退出游戏，运行 `RESTORE.cmd`，输入相同游戏目录。恢复前会检查已记录的DLL哈希；发生变化则停止，不删除未知文件。属于本安装的文件和日志会移动到游戏目录下 `HeatTemporalBridge-backups/时间戳`，而不是永久删除。

只恢复本项目修改的四个配置项；若你后来又修改了某项，会保留你的新选择。部分安装失败后也使用同一个恢复入口。遇到错误先查看备份，不要直接批量删除游戏文件。

## 源码构建

在带MSVC和Windows11 SDK的VS2022 x64 Developer PowerShell中：

```powershell
.\Build.ps1 -NgxInclude 'C:\SDKs\ngx-headers' -XeFgInclude 'C:\SDKs\XeSS\inc\xess_fg'
```

需自行按许可提供NVIDIA NGX声明头文件和Intel XeFG SDK头文件，本仓库不内置它们；AMD测试路径不需要NVIDIA DLSS运行库。MinHook源码和许可已附带。公开构建关闭游戏着色器、常量缓冲区和截图导出，不自动上传日志。

依赖来源、许可和致谢见[英文README的Dependencies and credits](README.md#dependencies-and-credits)及 `licenses/`。桥接代码和脚本采用MIT许可，第三方组件仍保留各自许可。不要把本项目宣传成适配所有Mac的“官方FSR4驱动”。
