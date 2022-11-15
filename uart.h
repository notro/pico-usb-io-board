// SPDX-License-Identifier: MIT
/*
 * Copyright 2021 Álvaro Fernández Rojas <noltari@gmail.com>
 */

#define UART0_TX 0
#define UART0_RX 1
#define UART1_TX 8
#define UART1_RX 9
#define NUM_UART_IFCE 2

void update_uart_cfg(uint8_t itf);
void uart_write_bytes(uint8_t itf);
void init_uart_data(uint8_t itf);
void core1_entry(void);
