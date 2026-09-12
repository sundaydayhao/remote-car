# 接线与供电

## 两个扩展 IO 控制两个直流电机

实战派的多功能接口引出 GPIO10/11；另一个 I²C 接口与摄像头、音频等共用 GPIO1/2。本工程使用 GPIO10/11 建立独立 I²C 总线，接 PCA9685 PWM 扩展板，后接 TB6612FNG 双路 H 桥。**GPIO、PCA9685 输出都不能直接接电机。**

| 实战派/电源 | PCA9685/驱动端 |
|---|---|
| GPIO10 | PCA9685 SDA |
| GPIO11 | PCA9685 SCL |
| 3.3 V | PCA9685 VCC，TB6612 VCC |
| GND | PCA9685 GND、TB6612 GND、电机电源负极共地 |
| 独立受限流保护的 3～6 V 电机电源 | TB6612 VM |

I²C 上拉接 **3.3 V**，不能接 5 V。PCA9685 A0～A5 接地（地址 0x40），本工程配置约 1.017 kHz PWM。不要将舵机板上的 V+ 当成 VCC；本设计不需要 PCA9685 的舵机 V+。

| PCA9685 通道 | TB6612 引脚 | 作用 |
|---|---|---|
| CH0 | AIN1 | 左轮正向 |
| CH1 | AIN2 | 左轮反向 |
| CH2 | PWMA | 左轮调速 |
| CH3 | BIN1 | 右轮正向 |
| CH4 | BIN2 | 右轮反向 |
| CH5 | PWMB | 右轮调速 |
| CH6 | STBY | 驱动使能；停车拉低 |
| — | AO1/AO2 | 左电机 |
| — | BO1/BO2 | 右电机 |

STBY 外加约 10 kΩ 下拉，确认模块没有把 STBY 永久上拉或短接到高电平。初始状态所有 PWM 关闭。常见 PCA9685 模块 OE 拉低可使能输出；若加急停/独立看门狗，可通过 OE 拉高关断输出（MODE2 OUTNE=00），并保持 STBY 下拉。检查模块实际原理图，不凭排针颜色判断顺序。

车轮转向相反时，在 `menuconfig → SZPI car hardware` 设置对应轮极性。左/右键是两轮反向的原地差速转向，不是控制转向舵机。反向先减速到零，再向反方向增加；STOP 和失联直接关闭 STBY，不走缓慢减速。

## 电机电流与电源

[A01-C 电机规格书](hardware/references/TT-motor-A01-C.pdf)中齿比为 1:48，工作电压 3～6 V，空载电流 90～110 mA，未给出堵转电流。PDF 中扭矩数值/单位没有用于驱动选型。

TB6612 数据手册给出 1.2 A 平均输出和受脉宽、占空比约束的 3.2 A 峰值；**不能把峰值当连续电流**。须按实际电压测量或取得堵转电流，确认模块散热/走线能承受；超过能力时更换合适的 H 桥，并保留相应限流/保护。

开发板用稳定 5 V 供电，电机用独立 3～6 V 电源支路；若电池是 2S，满电 8.4 V 不能直接送给这些电机。驱动 VM 旁放置电源储能电容（例如 220～470 μF）和去耦，电机端可加 100 nF 抑制刷火花，导线尽量远离麦克风、音频线和天线。具体电容/限流按实测调整。

失联保护由 S3 任务执行。**PCA9685 会保持输出寄存器**：S3 卡死、I²C 断线或 S3 独立掉电时，纯软件无法保证立即关闭仍在供电的驱动。实体小车须配置可切断 VM/STBY/OE 的急停；如需覆盖 MCU 卡死的自动停车，应另加独立硬件看门狗。这里没有把软件 350 ms 超时宣称为硬件故障下的停止保证。

## 实战派板载资源（不重新接线）

| 资源 | 引脚/地址 |
|---|---|
| 板内 I²C | SDA1 / SCL2 |
| ES8311 | 7-bit 0x18；codec_dev 使用 8-bit 0x30 |
| ES7210 | 7-bit 0x41；codec_dev 使用 8-bit 0x82 |
| I²S | MCLK38 / BCLK14 / WS13 / DOUT45 / DIN12 |
| PCA9557 | 0x19；bit0 LCD_CS，bit1 PA_EN，bit2 DVP_PWDN |
| 摄像头 | XCLK5，D0～D7：16/18/8/17/15/6/4/9；VSYNC3，HREF46，PCLK7 |

## P4 v1.4

按原配 7 寸 1024×600 屏接 MIPI DSI，屏幕转接板 RST_LCD 接 GPIO27、PWM 接 GPIO26，按官方说明供电。若换过屏幕，应在 BSP 配置中选择实际型号；当前布局按横屏 1024×600 排布。

使用板载麦克风；喇叭接板上 SPK 插座（4 Ω/3 W 是官方支持规格），不要接到逻辑 IO。代码使用 ES8311 的 I²S 左麦克风/右 DAC 参考模式。

C6 SDIO 默认：CLK18、CMD19、D0 14、D1 15、D2 16、D3 17、RESET54；这是 P4 与板载 C6 的内部连接，勿当作空闲 IO 使用。

来源：[NXP PCA9685](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf)、[Toshiba TB6612FNG](https://toshiba.semicon-storage.com/info/docget.jsp?did=10660&prodName=TB6612FNG)、[P4 v1.4 接屏说明](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide_v1.4.html)。
