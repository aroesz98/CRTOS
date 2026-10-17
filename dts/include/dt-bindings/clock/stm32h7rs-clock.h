/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Clocks of the STM32H7Rx/7Sx RCC for CRTOS device trees, one cell each:
 *   <&rcc STM32_CLK(REGISTER, BIT)>   the enable bit of a peripheral's bus clock (RM0477 "RCC
 *                                     ... peripheral clock enable register"; the CMSIS header's
 *                                     RCC_xxxENR_yyyEN); its rate is that of the bus
 *   <&rcc STM32_CLK(STM32_KERNEL, n)> a kernel clock the clk-stm32 module manages (STM32_CK_*)
 */
#ifndef DT_BINDINGS_CLOCK_STM32H7RS_H
#define DT_BINDINGS_CLOCK_STM32H7RS_H

#define STM32_CLK(reg, bit) (((reg) << 8) | (bit))

/* register offsets in the RCC */
#define STM32_AHB5ENR   0x134
#define STM32_AHB1ENR   0x138
#define STM32_AHB2ENR   0x13c
#define STM32_AHB4ENR   0x140
#define STM32_APB5ENR   0x144
#define STM32_APB1ENR1  0x148
#define STM32_APB1ENR2  0x14c
#define STM32_APB2ENR   0x150
#define STM32_APB4ENR   0x154
#define STM32_AHB3ENR   0x158
#define STM32_KERNEL    0xf00           /* not a register: the kernel clocks below */

/* kernel clocks (STM32_CLK(STM32_KERNEL, n)) */
#define STM32_CK_LTDC   0               /* PLL3 R: the pixel clock (set_rate picks the divider) */
#define STM32_CK_AUDIO  1               /* PLL3 Q: SPI6's kernel clock (I2S to the codec), 256 fs x n */
#define STM32_CK_HSI48  2               /* the 48 MHz oscillator (RNG, USB) */
#define STM32_CK_LSE    3               /* the 32.768 kHz crystal (RTC) */

/* bits */
#define STM32_AHB5_HPDMA1   0
#define STM32_AHB5_DMA2D    1
#define STM32_AHB5_XSPI1    5
#define STM32_AHB5_SDMMC1   8
#define STM32_AHB5_XSPI2    12
#define STM32_AHB5_XSPIM    14
#define STM32_AHB5_GPU2D    20
#define STM32_AHB1_GPDMA1   4
#define STM32_AHB1_ETH1MAC  15
#define STM32_AHB1_ETH1TX   16
#define STM32_AHB1_ETH1RX   17
#define STM32_AHB1_OTGHS    25
#define STM32_AHB1_USBPHYC  26
#define STM32_AHB1_OTGFS    27
#define STM32_AHB2_SDMMC2   9
#define STM32_AHB2_SRAM1    29
#define STM32_AHB2_SRAM2    30
#define STM32_AHB3_RNG      0
#define STM32_APB1_TIM2     0
#define STM32_APB1_SPI2     14
#define STM32_APB1_SPI3     15
#define STM32_APB1_USART2   17
#define STM32_APB1_USART3   18
#define STM32_APB1_UART4    19
#define STM32_APB1_UART5    20
#define STM32_APB1_I2C1     21
#define STM32_APB1_I2C2     22
#define STM32_APB1_I2C3     23
#define STM32_APB1_UART7    30
#define STM32_APB1_UART8    31
#define STM32_APB12_CRS     1           /* (APB1ENR2) */
#define STM32_APB12_UCPD1   27          /* (APB1ENR2) */
#define STM32_APB2_USART1   4
#define STM32_APB2_SPI1     12
#define STM32_APB2_SPI4     13
#define STM32_APB2_SPI5     20
#define STM32_APB2_SAI1     22
#define STM32_APB2_SAI2     23
#define STM32_APB4_SBS      1
#define STM32_APB4_SPI6     5
#define STM32_APB4_LPUART1  3
#define STM32_APB4_RTCAPB   16
#define STM32_APB5_LTDC     1

#endif
