# LT768 16bpp 8080/SPI Demo 对照结论

日期：2026-09-14
来源：`docs/STM32_LT768_16bpp_v01_8080_8bit_16bit_SPI_test/STM32_LT768_16bpp_v01_8080_8bit_16bit_SPI_test/`（Levetop 原厂 1024x600 demo，仅作对照基准，不作为本板规格依据）

本板边界：STM32F103C8T6 + LT7680A-R（10MHz 晶振）经 SPI 控 LT7680，驱动 320x960 ST7701S 18-bit RGB666；`PA3` 共享复位 LT7680 与 LCD；W25Q128 挂 LT7680 一侧。

## 1. 与当前问题的关联线索

| 线索 | demo 位置 | 对本板的意义 |
|---|---|---|
| 上电/复位/PLL 就绪是“轮询 + 重试”，不是一次写完 | `HARDWARE/LT768_Lib/LT768_Lib.c:System_Check_Temp(), LT768_Init()`；`USER/main.c:8-18` | demo 流程 `Delay 100ms -> PE1 拉低100ms/拉高100ms -> 轮询 STATUS&0x02 + 0x01 bit7 -> 5次失败再硬复位 -> 等 STATUS&0x02 清零`。我们 `lt7680_wait_ready()` 只有超时返回，缺“失败后复位重试”一环；黑屏/偶发不亮时优先补这一环，而不是先怀疑时序值 |
| 接口默认是 FSMC 16-bit 8080，SPI 只是关闭的备选 | `HARDWARE/IF_PORT/if_port.h:13-16`（`FSMC_16=1`，其余 0）；`if_port.c:FSMC_Init_16(), SPI2_Init()` | SPI 备选是 `SPI2 Mode3(CPOL_High/CPHA_2Edge)/2分频/PB12 CS`。本板是 `SPI1 bit-bang Mode0` 隔离实验中，demo 的模式/速度不能照搬；FSMC 时序 `AddressSetup=1/DataSetup=1` 只供将来切 8080 参考 |
| SPI 协议字与我们一致，且像素是分两次独立 CS | `if_port.c:SPI_CmdWrite/SPI_DataWrite/SPI_StatusRead/SPI_DataRead/SPI_DataWrite_Pixel()` | `0x00写地址 / 0x80写数据 / 0x40读状态 / 0xC0读数据` 与 `firmware/src/lt7680_bus.h` 一致；`SPI_DataWrite_Pixel` 分两次 `CS低->0x80->byte->CS高`，佐证“每字节独立 CS”，burst 包多个 `0x80` 是错的 |
| PLL/SDRAM 是按公式算的，不是固定 magic number | `LT768_Lib.c:LT768_PLL_Initial(), LT768_SDRAM_initail()`；`LT768_Lib.h:14-29` | `SCLK按(全行×全列×60)算、封顶65`；按 `XI_4/8/10/12M` 查 `N/R/OD` 表；`SDRAM刷新间隔=(64000000/8192)/(1000/mclk)-2`。公式可复用，数值不可抄（demo 是 1024x600） |
| GE Fill 不用设窗口就能出色条，真图必须设全套窗口 | `USER/main.c:24-31` 只 Fill；`HARDWARE/LT768_Demo/LT768_Demo.c:Show()` 设 `MISA/MIW/MWUL/CVSSA/CVS_W/AWUL/AW_WH` 后才放图/字库 | “色条正常、真图/文字不出”先查 `MISA/CVSSA/AW + MRWDP 0x04` 地址写，不先动色深寄存器 |
| GE 填充与前景色编码与我们一致 | `HARDWARE/LT768/LT768.c:Foreground_color_65k/Square_Start_XY/Square_End_XY/Start_Square_Fill/Check_2D_Busy()` | `D2=temp>>8, D3=temp>>3, D4=temp<<3`；`SPT/EPT + REG[76h]=0xE0 + poll STATUS bit3(0x08)`。证实 `DCR1=0xE0` 正确，`DCR0=0x67` 不管矩形填充 |

## 2. 可取经验（可直接用）

1. 保留 `R/G/B 整屏 Fill + 1s 延时` 作为冒烟测试，隔离复杂渲染（对应我们 color-bars 验收）。
2. `wait_ready` 超时后加“硬复位重试一次再报超时”，上电/复位保守延时不收紧（demo 用 100ms 级）。
3. PLL/SDRAM 改参数化计算（分辨率算 SCLK、晶振选表、刷新间隔公式），避免 hardcode。
4. 所有面板/总线配置寄存器坚持读-改-写（demo `LT768.c` 全是 `Cmd->Read->改位->Write`）。
5. SPI 写数据保持“每字节独立 CS”；`0x80` 前缀不复用、不 burst。

## 3. 不可取（明确丢弃）

* `1024x600` 时序（HBPD140/HFPD160/HSPW20/VBPD20/VFPD12/VSPW3）、`TFT_16bit + RGB_16b_16bpp` 组合，不适用于 320x960 RGB666。
* `FSMC_16` 默认、`SPI Mode3 高速`、`PE1 独立复位`：与本板 `bit-bang Mode0 + PA3 共享复位` 冲突。
* `E0h=0x20` 是小 SDRAM 配置，本板 128Mb 用 `0x29 + CAS3` 不回退。
* demo 无 ST7701S 9-bit 初始化（`0x11/0x35/0x3A 0x66/0x29`）、无共享复位排序、无双页/BTE `0xC2`，不能做面板侧依据。

## 4. 后续动作

* 本板若复现“偶发不亮”，优先补 PLL 就绪失败后的复位重试，并记录 `STATUS/0x01` 回读值。
* 切 8080 或换屏时，用 demo 的 FSMC 时序和 PLL 公式做起点，再按本板实测收敛。
