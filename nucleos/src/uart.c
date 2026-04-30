/* UART serial functions
 * www.ece.tufts.edu/ee/14
 * Steven Bell <sbell@ece.tufts.edu>,
 * based almost entirely on work from Joel Grodstein
 * March 2026
 */
#include "ee14lib.h"

#define CIRCBUF_SIZE 512
char circbuf[CIRCBUF_SIZE];
int circbuf_head = 0;
int circbuf_tail = 0;


void usart1_midi_init(uint32_t baud)
{
    // 1. Enable clocks
    RCC->APB2ENR  |= RCC_APB2ENR_USART1EN;   // USART1 is on APB2
    RCC->AHB2ENR  |= RCC_AHB2ENR_GPIOAEN;    // adjust if your RX pin is on a different port

    // 2. Configure GPIO pin for USART1_RX (e.g. PA10, AF7)
    //    Set mode to Alternate Function
    GPIOA->MODER  &= ~(3U << (10 * 2));
    GPIOA->MODER  |=  (2U << (10 * 2));       // AF mode
    //    Set AF7 for USART1
    GPIOA->AFR[1] &= ~(0xF << ((10 - 8) * 4));
    GPIOA->AFR[1] |=  (7U  << ((10 - 8) * 4));

    // 3. Configure baud rate
    // USART1->BRR = SystemCoreClock / baud;     // assumes no prescaler
    USART1->BRR = 80000000U / baud;             //explicity use 80MHz

    // 4. Enable receiver + RXNE interrupt
    USART1->CR1 = USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;

    // 5. Enable interrupt in NVIC
    NVIC_SetPriority(USART1_IRQn, 1);
    NVIC_EnableIRQ(USART1_IRQn);
}

/**
 * Configure either USART with the "normal" defaults:
 * Set for 8 data bits, 1 start & 1 stop bit, 16x oversampling
 * And by default, we also get no parity, no hardware flow control (USART_CR3),
 * asynch mode (USART_CR2).
 *
 * :param USARTx: Pointer to the CMSIS USART struct, either USART1 or USART2
 * :param tx_en: True to enable TX channel
 * :param rx_en: True to enable RX channel
 * :param baud: Baud rate to use
 */
static void USART_Init(USART_TypeDef *USARTx, bool tx_en, bool rx_en, int baud)
{
    // Disable the USART.
    USARTx->CR1 &= ~USART_CR1_UE;

    // The "M" field is two bits, M1 and M0. We're setting it to 00 (which
    // is the reset value anyway), to use 8-bit words and one start bit.
    USARTx->CR1 &= ~USART_CR1_M;

    // Configure stop bits to 1 stop bit (which is the default). Other
    // choices are .5, 1.5 and 2 stop bits.
    USARTx->CR2 &= ~USART_CR2_STOP;

    // Set baudrate as desired. This is done by dividing down the APB1 clock.
    // E.g., 80MHz/9600 = 8333 = 0x208D.
    // (We're oversampling by 16; the calculation would be slightly
    // different if we were 8x mode).
    extern uint32_t SystemCoreClock;
    uint32_t val = SystemCoreClock / baud;
    USARTx->BRR = val;

    // Configure oversampling mode: Oversampling by 16 (which is the
    // default). This means that our Rx runs at 16x the nominal baud rate.
    // If we're not enabling the Rx anyway, this step is moot (but harmless).
    USARTx->CR1 &= ~USART_CR1_OVER8;

    // Turn on transmitter and receiver enables. Note that the entire USART
    // is still disabled, though. Turning on the Rx enable kicks off the Rx
    // looking for a stop bit.
    if (tx_en)
        USARTx->CR1 |= USART_CR1_TE;
    if (rx_en)
        USARTx->CR1 |= USART_CR1_RE;

    // We originally turned off the USART -- now turn it back on.
    // Note that page 1202 says to turn this on *before* asserting TE and/or RE.
    USARTx->CR1 |= USART_CR1_UE; // USART enable

    // Verify that the USART is ready to transmit...
    if (tx_en)
        while ((USARTx->ISR & USART_ISR_TEACK) == 0)
            ;
    // ... and to receive.
    if (rx_en)
        while ((USARTx->ISR & USART_ISR_REACK) == 0)
            ;
}

/**
 * Configure the serial connection to the host PC.
 * This will always be on USART 2 and GPIO pins A7 (PA2) and PA15, since those
 * are the wired connections to the host bridge chip on the Nucleo PCB.
 *
 * :param baud: The baud rate, typically 9600 or 115200.
 */
void host_serial_init(const unsigned int baud)
{

    // Enable USART 2 clock
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;

    // Select SYSCLK as the USART2 clock source. The reset default is PCLK1;
    // we usually set both SYSCLK and PCLK1 to 80MHz anyway.
    RCC->CCIPR &= ~RCC_CCIPR_USART2SEL;
    RCC->CCIPR |= RCC_CCIPR_USART2SEL_0;

    // Connect the I/O pins to the serial peripheral
    // UART2_GPIO_Init();
    gpio_config_alternate_function(A7, 7);     // PA 2, AF7
    gpio_config_alternate_function(VCP_RX, 3); // PA 15, AF3

    USART_Init(USART2, 1, 1, baud); // Enable both Tx and Rx sides.
}

/*INITIALIZE USART1 */
void usart1_init(int baud)
{
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN; // USART1 is on APB2, not APB1

    gpio_config_alternate_function(D1, 7); // PA9  TX, AF7
    gpio_config_alternate_function(D0, 7); // PA10 RX, AF7

    USART_Init(USART1, 0, 1, baud); // RX only
}

/**
 * Write a buffer to the serial port.  This function blocks until the
 * transmission is complete.
 *
 * :param USARTx: Pointer to the CMSIS USART struct, either USART1 or USART2
 * :param buffer: Pointer to array of bytes to send
 * :param len: Number of bytes to transmit
 */
void serial_write(USART_TypeDef *USARTx, const char *buffer, int len)
{
    // The main flag we use is Tx Empty (TXE). The HW sets it when the
    // transmit data register (TDR) is ready for more data. TXE is then
    // cleared when we write new data in (by a write to the USART_DR reg).
    // When the HW transfers the TDR into the shift register, it sets TXE=1.
    for (unsigned int i = 0; i < len; i++)
    {
        // Wait until the "Transmit data register empty" flag is set
        // TXE is bit 7
        // so while TXE == 0 aka not ready for data yet
        while (!(USARTx->ISR & USART_ISR_TXE))
        {
        }

        // Put the next byte into the buffer; this will automatically clear the TXE flag
        // NOTE: SENDS ONE CHARACTER
        USARTx->TDR = buffer[i];

        // NOTE:
        // after sending one character, goes back to while loop and wait.
        // circular buffer pro: instead of going to while loop, do other things while waiting for next character to be ready. elminate use of the while loop
    }

    // RM0394 page 1203 says that you must wait for ISR.TC=1 before you shut
    // off the USART. We never shut off the USART... but we'll wait anyway.
    // TC is bit 6
    while (!(USARTx->ISR & USART_ISR_TC))
        ;
    USARTx->ISR &= ~USART_ISR_TC;
}

int serial_write_nonblocking(USART_TypeDef *USARTx, const char *buffer, int len)
{
    int bytes_pushed = 0;

    for (int i = 0; i < len; i++)
    {
        int next = (circbuf_head + 1) % CIRCBUF_SIZE;

        if (next == circbuf_tail)
        {
            break;
        }

        circbuf[circbuf_head] = buffer[i];
        bytes_pushed++;
        circbuf_head = next;
    }

    // enable the handler
    USART2->CR1 |= USART_CR1_TXEIE;
    NVIC_EnableIRQ(USART2_IRQn);

    return bytes_pushed;
}

void USART2_IRQHandler(void)
{
    // WRITE TO AN LED FOR TESTING
    gpio_config_mode(D12, OUTPUT);
    gpio_write(D12, 1);
    if (USART2->ISR & USART_ISR_TXE)
    {
        if (circbuf_head != circbuf_tail)
        {
            USART2->TDR = circbuf[circbuf_tail];
            circbuf_tail = (circbuf_tail + 1) % CIRCBUF_SIZE;
        }
        else
        {
            USART2->CR1 &= ~USART_CR1_TXEIE;
            NVIC_DisableIRQ(USART2_IRQn);
        }
    }
    gpio_write(D12, 0);
}

/**
 * Read a byte from the serial port.  This blocks until a byte comes in and
 * then returns that value.
 * Wouldn't it be nice if this were buffered and asynchronous too?
 *
 * :param USARTx: Pointer to the CMSIS USART struct, either USART1 or USART2
 *
 * :return: Returns a byte read from the serial port.
 */
char serial_read(USART_TypeDef *USARTx)
{
    // The SR_RXNE (Read data register not empty) bit is set by hardware.
    // We spin wait until that bit is set
    while (!(USARTx->ISR & USART_ISR_RXNE))
    {
    }

    // Reading USART_DR automatically clears the RXNE flag
    return ((char)(USARTx->RDR & 0xFF));
}
