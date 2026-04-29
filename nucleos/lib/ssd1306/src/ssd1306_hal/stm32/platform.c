#include "ssd1306_hal/io.h"

#if defined(SSD1306_STM32_PLATFORM)

#include "intf/ssd1306_interface.h"
#include "stm32l4xx.h"

// ── Hardware config ───────────────────────────────────────────────────────────
// I2C3 on PA7/A6 (SCL) and PB4/D12 (SDA), AF4, open-drain.
// SSD1306 I2C address is passed in from display_init via ssd1306_i2cInitEx.
// TIMINGR for 100 kHz @ 80 MHz (STM32CubeMX, standard mode).
#define I2C_TIMING  0x10909CECUL

// TX buffer — large enough for one full-screen write (1 ctrl + 1024 data bytes).
#define TX_BUF_SIZE 1200
static uint8_t  s_tx[TX_BUF_SIZE];
static uint16_t s_txlen;
static uint8_t  s_addr;

// ── I2C helpers ───────────────────────────────────────────────────────────────

// Timeout counts chosen for ~80 MHz CPU: 100 000 iterations ≈ several ms,
// enough for any valid I2C byte at 100 kHz without blocking the ISR indefinitely.
#define I2C_TIMEOUT 100000U

static void i2c_reset(void)
{
    I2C3->CR1 &= ~I2C_CR1_PE;
    for (volatile int i = 0; i < 200; i++) {}
    I2C3->CR1 |= I2C_CR1_PE;
}

static void i2c_flush(void)
{
    if (s_txlen == 0) return;

    uint16_t remaining = s_txlen;
    uint16_t pos       = 0;
    uint32_t t;

    uint8_t chunk = (remaining > 255) ? 255 : (uint8_t)remaining;
    I2C3->CR2 = ((uint32_t)chunk   << I2C_CR2_NBYTES_Pos)         |
                ((uint32_t)(s_addr << 1) & I2C_CR2_SADD_Msk)      |
                (remaining <= 255 ? I2C_CR2_AUTOEND : I2C_CR2_RELOAD) |
                I2C_CR2_START;

    while (remaining > 0) {
        t = I2C_TIMEOUT;
        while (!(I2C3->ISR & I2C_ISR_TXIS) && --t) {}
        if (!t) { i2c_reset(); s_txlen = 0; return; }

        I2C3->TXDR = s_tx[pos++];
        remaining--;
        chunk--;

        if (chunk == 0 && remaining > 0) {
            t = I2C_TIMEOUT;
            while (!(I2C3->ISR & I2C_ISR_TCR) && --t) {}
            if (!t) { i2c_reset(); s_txlen = 0; return; }

            chunk = (remaining > 255) ? 255 : (uint8_t)remaining;
            I2C3->CR2 = ((uint32_t)chunk   << I2C_CR2_NBYTES_Pos)    |
                        ((uint32_t)(s_addr << 1) & I2C_CR2_SADD_Msk) |
                        (remaining <= 255 ? I2C_CR2_AUTOEND : I2C_CR2_RELOAD);
        }
    }

    t = I2C_TIMEOUT;
    while (!(I2C3->ISR & I2C_ISR_STOPF) && --t) {}
    I2C3->ICR = I2C_ICR_STOPCF;
    if (!t) i2c_reset();

    s_txlen = 0;
}

// ── ssd1306_intf callbacks ────────────────────────────────────────────────────

static void hw_start(void)
{
    s_txlen = 0;
}

static void hw_stop(void)
{
    i2c_flush();
}

static void hw_send(uint8_t b)
{
    if (s_txlen < TX_BUF_SIZE)
        s_tx[s_txlen++] = b;
}

static void hw_send_buffer(const uint8_t *buf, uint16_t len)
{
    while (len-- && s_txlen < TX_BUF_SIZE)
        s_tx[s_txlen++] = *buf++;
}

static void hw_close(void) {}

// ── Public: ssd1306_platform_i2cInit ─────────────────────────────────────────

#if defined(CONFIG_PLATFORM_I2C_AVAILABLE) && defined(CONFIG_PLATFORM_I2C_ENABLE)
void ssd1306_platform_i2cInit(int8_t busId, uint8_t addr,
                               ssd1306_platform_i2cConfig_t *cfg)
{
    (void)busId;
    (void)cfg;

    s_addr  = addr ? addr : 0x3C;
    s_txlen = 0;

    // Enable GPIOA, GPIOB, and I2C3 clocks.
    RCC->AHB2ENR  |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_I2C3EN;

    // PA7 (A6, SCL): AF mode, open-drain, no pull, AF4 (I2C3).
    GPIOA->MODER  &= ~GPIO_MODER_MODE7;
    GPIOA->MODER  |=  GPIO_MODER_MODE7_1;
    GPIOA->OTYPER |=  GPIO_OTYPER_OT7;
    GPIOA->PUPDR  &= ~GPIO_PUPDR_PUPD7;
    GPIOA->AFR[0] &= ~GPIO_AFRL_AFSEL7;
    GPIOA->AFR[0] |=  (4UL << GPIO_AFRL_AFSEL7_Pos);

    // PB4 (D12, SDA): AF mode, open-drain, no pull, AF4 (I2C3).
    GPIOB->MODER  &= ~GPIO_MODER_MODE4;
    GPIOB->MODER  |=  GPIO_MODER_MODE4_1;
    GPIOB->OTYPER |=  GPIO_OTYPER_OT4;
    GPIOB->PUPDR  &= ~GPIO_PUPDR_PUPD4;
    GPIOB->AFR[0] &= ~GPIO_AFRL_AFSEL4;
    GPIOB->AFR[0] |=  (4UL << GPIO_AFRL_AFSEL4_Pos);

    // Disable I2C3 before configuring.
    I2C3->CR1 &= ~I2C_CR1_PE;

    // Timing for 100 kHz @ 80 MHz.
    I2C3->TIMINGR = I2C_TIMING;

    // Enable I2C3.
    I2C3->CR1 |= I2C_CR1_PE;

    // Brief power-on delay for the display.
    for (volatile int i = 0; i < 800000; i++) {}

    // Wire up the library interface.
    ssd1306_intf.spi         = 0;
    ssd1306_intf.start       = hw_start;
    ssd1306_intf.stop        = hw_stop;
    ssd1306_intf.send        = hw_send;
    ssd1306_intf.send_buffer = hw_send_buffer;
    ssd1306_intf.close       = hw_close;
}
#endif

#endif // SSD1306_STM32_PLATFORM
