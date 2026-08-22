#ifndef INC_BSP_UART_H
#define INC_BSP_UART_H
#include <stdint.h>
/* minimal PC stub: only the symbols shell.c uses */
typedef struct { volatile void *dummy; } UART_HandleTypeDef;
#define UART_RX_BUF_SIZE   256
#define UART_TX_BUF_SIZE   512
#define USART_CR1_RXNEIE   0x20
#define USART_SR_RXNE      0x20
#define USART_SR_TXE       0x80
#define SET_BIT(r,b)  (*(volatile uint32_t*)(r) |= (b))
#define CLEAR_BIT(r,b) (*(volatile uint32_t*)(r) &= ~(b))
#define READ_REG(r)    (*(volatile uint32_t*)(r))
int uart_write(const uint8_t *data, int len);
int uart_puts(const char *s);
int uart_getc(uint8_t *c);
#endif
