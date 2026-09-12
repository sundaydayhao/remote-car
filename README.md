# 实战派 S3 小车 + P4 触屏遥控器

独立的 ESP-IDF 小车与遥控器工程。`car_s3`、`remote_p4` 分别编译，共用本仓库内的 `components/rc_common`，不需要其他工程的源码或构建目录。

仓库：[zhanghaogit/remote-car](https://gitee.com/zhanghaogit/remote-car)。本机工作目录：`C:\projects\remote-car`。

## 硬件

- 小车：立创实战派 ESP32-S3，使用板载 GC0308、ES7210 麦克风、ES8311 和喇叭。
- 遥控器：ESP32-P4 Function EV Board **v1.4**，默认原配 **7 寸 1024×600 EK79007 + GT911** 触屏，板载 ES8311 麦克风和外接喇叭，板载 C6 通过 SDIO 提供 Wi-Fi。
- 行走：两只 A01-C TT 电机，差速转向。实战派 GPIO10/11 作为 I²C 接到 **PCA9685 + TB6612FNG**；需要这两个外接模块，具体接线见 [WIRING.md](WIRING.md)，电机资料见 [A01-C 规格书](hardware/references/TT-motor-A01-C.pdf)。

电机资料只给出 3～6 V 和空载 90～110 mA，没有堵转电流。TB6612 适配代码可用于原型，但购买/接电前仍须确认电机堵转电流和驱动模块散热能力；不能用空载电流证明额定驱动裕量。

## 实现的功能

- P4 界面左侧视频，右侧前进、后退、左转、右转、STOP，速度滑条，语音开关，连接与电机状态。按住方向键运动，释放/滑出立即发停车指令，重连后需要重新按下。
- GC0308 输出 QVGA RGB565，在 S3 软件编码 JPEG，P4 连续获取 `/jpg` 并解码显示。图像按 2 倍显示到 640×480；这不会增加摄像头分辨率。视频任务与控制、音频分开。
- 默认**全双工**对讲：连接成功后双方持续采音、播放，均经过 ESP-SR 的 VOIP AEC。没有按住讲话按钮，也不按说话状态关闭麦克风。详见 [AUDIO.md](AUDIO.md)。
- 控制每约 50 ms 更新；车端独立任务检查 350 ms 指令超时，超时或断线停车。CRC、会话、序号检查拒绝坏包、旧连接和重复指令。重连第一包必须停车。
- 音频采用 16 kHz、16 bit、单声道、32 ms UDP 包，3 包容量、2 包预缓冲。丢弃迟到/旧会话数据，缺包播放静音，限制播放幅度。

这些是代码与编译验证的实现范围，**尚未烧录或实测**。帧率、音视频时延、AEC 效果、实际停止距离尚无实测结果。

## 构建

验证环境：Windows，ESP-IDF **5.5.5**，两端目标分别为 `esp32s3`、`esp32p4`，Flash 16 MB。主依赖版本在 `main/idf_component.yml` 固定，全部解析版本在各自的 `dependencies.lock` 中。

```powershell
cd C:\projects\remote-car
.\tools\build.ps1
```

也可以在 ESP-IDF 5.5.x 终端分别运行：

```powershell
cd C:\projects\remote-car\car_s3
idf.py build

cd C:\projects\remote-car\remote_p4
idf.py build
```

各自运行 `idf.py menuconfig` 可设置 `Camera car link` 中的 Wi-Fi 名称/密码、音量与增益。两端 Wi-Fi 配置必须一致。初始 AP：`SZPI-P4-Car`，密码 `CarVideo2026`，车端地址 `192.168.4.1`。

P4 固件默认面向早期 P4 芯片（`ESP32P4_SELECTS_REV_LESS_V3=y`）。**PCB v1.4 不是芯片 revision**；烧录前读取芯片版本。若实际芯片是 rev3.x，需调整该选项重新构建。不要把为 rev3.x 编译的镜像直接烧到早期 P4。

烧录在各自项目目录运行 `idf.py -p 实际端口 flash monitor`。先确认两个端口对应的板卡；本次没有连接、擦除或烧录设备。

产物：`car_s3/build/szpi_camera_car.bin`、`remote_p4/build/p4_touch_remote.bin`，配套 bootloader、partition-table 与 `flash_args` 均在对应 build 目录。两个项目都使用 4 MB factory 分区，不包含 OTA。

## C6 无线协处理器

P4 本体没有 Wi-Fi。若 P4 启动后反复报告无法连接 slave，检查 C6 是否有兼容的 ESP-Hosted 固件。当前 P4 锁定的 ESP-Hosted 是 **2.12.11**。板卡出厂旧 slave 不保证与当前主机版本兼容。

依赖下载后，同版本官方 slave 源码位于 `remote_p4/managed_components/espressif__esp_hosted/slave`。在此目录用 IDF 5.5.x 设置 `esp32c6` 目标、选择 P4 Function EV Board/SDIO，再构建。C6 下载口与 P4 下载口不同；不要把 C6 镜像写进 P4。参照[官方 P4/C6 配置与烧录说明](https://github.com/espressif/esp-hosted-mcu/blob/main/docs/esp32_p4_function_ev_board.md)。本次未更新板上的 C6 固件。

## 测试与上板

主机测试直接编译生产 `rc_protocol.c`：

```powershell
.\tools\test.ps1 -Compiler '你的 gcc、clang 或 tcc 路径'
```

覆盖 CRC、完整帧和截断、PCM 字节序、坏包、会话、序号回绕、重复包、超时、首包停车、转向及反转过零。测试范围与上板步骤见 [VALIDATION.md](VALIDATION.md)。

代码入口：`car_s3/main/main.c`、`remote_p4/main/main.c`；板载音频适配：`components/rc_common/rc_codec.c`；全双工：`rc_audio.c`；接线：`car_s3/main/szpi_board.c`、`motor.c`。

界面目前使用英文标签和方向图标；详细报文见 [PROTOCOL.md](PROTOCOL.md)。

## 资料依据

- [立创实战派板卡与 GPIO10/11](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/introduction.html)
- [GC0308 摄像头引脚](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/camera.html)
- [ES7210 输入](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/audio-input-es7210.html)、[ES8311/PCA9557 输出](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/audio-output-es8311.html)
- [P4 Function EV Board v1.4](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide_v1.4.html)
- [P4 BSP 5.2.3](https://components.espressif.com/components/espressif/esp32_p4_function_ev_board/versions/5.2.3/readme)
