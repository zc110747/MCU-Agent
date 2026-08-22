# STM32H743ZIT6 硬件配置

## 系统

- MCU: STM32H743ZIT6
- HSE: 25MHz (无源晶振)
- LSE: 32.768kHz (无源晶振)

## 外设配置

### LED (GPIO)

- LED: PG7, 输出, 低电平点亮, 推挽, 上拉

### USART

- USART1_RX：PA10
- USART1_TX：PA9

### QSPI (QuadSPI, 连接W25Q64)

- 模式: 4线
- CS: PG6, AF10
- CLK: PF10, AF9
- IO0: PF8, AF10
- IO1: PF9, AF10
- IO2: PF7, AF9
- IO3: PF6, AF9

### SDMMC (SD卡, 4-bit模式)

- CK: PC12, AF12
- CMD: PD2, AF12
- D0: PC8, AF12
- D1: PC9, AF12
- D2: PC10, AF12
- D3: PC11, AF12

### USB (FS模式)

- DP: PA12, AF10
- DN: PA11, AF10

### SPI6 (OLED显示，连接ST7789)

- 模式: 主机, 全双工
- SCK: PG13, AF5
- MOSI: PG14, AF5
- CS: PG8, 输出 (软件控制)
- DC: PG15, 输出 (软件控制)
- BL: PG12, 输出 (只I/O点亮控制)

### DCMI/I2C(摄像头ov5640)

- I2C_SCL: PF14
- I2C_SDA: PF15
- PWDN：PF13，控制摄像头供电，低电平工作
- DCMI_HSYNC：PA4
- DCMI_VSYNC：PG9
- DCMI_D0: PC6
- DCMI_D1: PC7
- DCMI_D2: PG10
- DCMI_D3: PG11
- DCMI_D4: PE4
- DCMI_D5: PD3
- DCMI_D6: PE5
- DCMI_D7: PE6
- DCMI_CLK：PA6
