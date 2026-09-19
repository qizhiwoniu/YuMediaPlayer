# YuMediaPlayer
PC电脑端mp3播放器 desktop mp3 player

一个简单的PC端mp3播放器，支持播放本地音乐文件，
提供基本的播放控制功能，如播放、暂停、停止、上一曲、下一曲等。
 
---

## 目录

## 1. 项目简介

YuMediaPlayer 是一个面向工程师和开发者的**播放器框架**，目标是在桌面端（Windows）

## 2. 架构分层概览

## 3. 关键技术决策

---

## 4. 开发进度
 
### 阶段规划

```
[×]  阶段 0  基础设施                              
[×]  ├── CMake 多目标工程搭建
[×]  ├── Core/Base（Result<T>、错误码）
[×]  └── Platform 接口 + Desktop 实现（窗口 + 输入）
     
[√]  阶段 1  第一个可见结果                          
[√]  └── UI 基础面板（属性 + 图层）

[×]  阶段 2  稳定化

[×]  阶段 3  功能完善
     
[×]  阶段 4  三端 + 发布质量 desktop android and YuOS
[×]  └── 性能剖析与优化
```
## 里程碑
---

## 5. 文件目录

```
MiniCAD
│    
├── assets/                                        # 只读资源，打包进可执行文件
│   ├── background/                                # ui 背景图资源SVG源文件，构建时生成的PNG图集
│   ├── disk/                                      # 封面图资源及图片，用于旋转播放的资源
│   ├── font/                                      # 字体文件，构建所需的字体文件TFT/TTC文件
│   ├── icons/                                     # ui 图标：ico源文件 + 构建时生成的 PNG 图集
│   ├── language/                                  # 语言.yu文件
│   ├── skin/                                      # 皮肤
│   ├── ui/                                        # GUI资源 显示窗口所需要的资源文件
│   └── userprofile/                               # 用户文件
│   
├── fengbao/  
│   ├── fengbao.ism (暂无)                         # (预留)InstallShield 2025 封包文件
│   ├── fengbao.iss                                # Inno setup Compiler 6 封包文件
│   └── YuMediaPlayer.evb                          # Enigma Virtual Box 封包压缩文件
│   └── setup.exe(ver x.x.x.x)                     # 正式安装文件（版本x.x.x.x)
│ 
├── src/
│   │
│   ├── App/                                      # APP
│   │   ├── Main.cpp                              # 初始化主函数
│   │   ├── MainWindow.cpp                        # 主窗口初始化
│   │   └── MainWindow.h                          # 主窗口初始化所需资源
│   │
│   ├── Core/                                     # 主要代码层
|   |   ├── CircularAvatar.h/cpp                  # 图片专辑唱片圆形封面
│   │   └── Theme.h                               # 主题
│   │
│   ├── NotifyIcon/                               # 系统通知栏图标 
│   │   └── TrayIcon.h/cpp                        # 系统通知栏图标：托盘图标、气泡提示、右键菜单
│   │
│   └── UI/                                       # UI 层
│        ├── miniWindowsGUI.h/cpp                 # 小窗口：浮动工具栏、悬浮提示框、弹出菜单                      
│        └── WindowGUI.h/cpp                      # 主窗口     
│   
├── thirdParty/                                   # 第三方库：以源码或预编译二进制形式管理
│                                                   
└── tests/                                        # 测试：按层分组
    ├── Core/                                     # Core 层单元测试：算法正确性、数值稳定性、边界情况
    └── Render/                                   # Render 层测试：
    
```
---
# feature
  V1.0.0 主要功能 main      </br>
  V1.1.0 新增功能 update    </br>
  V1.2.0 修复     bug fix   </br>
  V1.3.0 ...      ...       </br>
# thanks
  myfather

*日期：2026-09-15* 
