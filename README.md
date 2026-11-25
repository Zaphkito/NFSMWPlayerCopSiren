# NFSMWPlayerCopSiren

《极品飞车：最高通缉》（2005，PC 1.3 x86）的玩家警笛与地图图标 ASI 插件。当前版本为 v0.6.3 测试版。

玩家驾驶警车并开启警灯时，插件使用游戏原生警笛音源。玩家驾驶警用直升机时，插件为其显示地图图标，使旋转圈跟随机体位置并继续播放原生动画。车辆识别要求驾驶者为本地玩家。警车地图图标需要 Extra Options。

## 构建

安装 Visual Studio 的 C++ 桌面开发工具和 Windows SDK，从 **x86 Native Tools Command Prompt for VS** 进入项目目录，运行 `build.cmd`。产物为 `build\NFSMWPlayerCopSiren.asi` 和同名 INI。

## 安装与升级

退出游戏，将编译好的 ASI 和 INI 放入 ASI 加载器使用的 `scripts` 目录。旧版文件名为 `PlayerCopSiren.asi`。升级时先将旧 ASI 移出加载目录，只保留一个版本。要沿用已有设置，把旧的 `PlayerCopSiren.ini` 改名为 `NFSMWPlayerCopSiren.ini`，跳过复制默认 INI。

随附 INI 中，`[Siren]` 下的 `Enabled=1` 开启警笛，`Volume=100` 设置音量，`Logging=0` 关闭日志。音量可设为 0 至 100。将 `Logging` 改为 1 后重启游戏，可在 ASI 同目录查看 `NFSMWPlayerCopSiren.log`。

## 验证记录

v0.6.3 的 120 帧离线 UI 指令仿真中，玩家直升机的旋转圈跟随移动坐标并持续旋转，AI 原始旋转圈数据保持一致。实机小地图和世界地图的显示效果仍待确认。插件按游戏运行代码的字节签名识别所支持的版本。
