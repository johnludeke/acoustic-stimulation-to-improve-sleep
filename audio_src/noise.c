#include <xc.h>
#include <stdint.h>
#include <stdlib.h>

#pragma config FNOSC = FRCPLL
#pragma config FPBDIV = DIV_1
#define SYS_FREQ 40000000UL
#define SAMPLE_RATE 8000

// SDO1 = RD0, SCK1 = RD10, CS = RD9
#define DAC_CS_TRIS  TRISDbits.TRISD9
#define DAC_CS       LATDbits.LATD9

// MCP4822 command word: channel A, gain=1x, active
// Bits [15:12]: 0=channel A, 0=ignore, 1=1x gain, 1=active
#define DAC_CMD  0x3000   // 0011 xxxx xxxx xxxx

void spi1_init(void) {
    DAC_CS_TRIS = 0;
    DAC_CS = 1;
    TRISDbits.TRISD0  = 0; // SDO
    TRISDbits.TRISD10 = 0; // SCK

    SPI1CON = 0;
    SPI1BRG = 0;            // SCK = PBCLK/2 = 20 MHz
    SPI1CONbits.MSTEN = 1;
    SPI1CONbits.CKP   = 0;  // Mode 0,0
    SPI1CONbits.CKE   = 1;
    SPI1CONbits.MODE16 = 1; // 16-bit transfers
    SPI1CONbits.ON    = 1;
}

void dac_write(uint16_t value12) {
    // value12: 0–4095
    uint16_t cmd = DAC_CMD | (value12 & 0x0FFF);
    DAC_CS = 0;
    SPI1BUF = cmd;
    while (SPI1STATbits.SPIBUSY);
    DAC_CS = 1;
}

// Using Voss-McCartney pink noise
#define PINK_STAGES 16

static uint32_t pink_rows[PINK_STAGES];
static uint32_t pink_running_sum;
static uint32_t pink_index;

static uint32_t xorshift_state = 0xDEADBEEF;
uint32_t fast_rand(void) {
    xorshift_state ^= xorshift_state << 13;
    xorshift_state ^= xorshift_state >> 17;
    xorshift_state ^= xorshift_state << 5;
    return xorshift_state;
}

void pink_noise_init(void) {
    pink_running_sum = 0;
    pink_index = 0;
    for (int i = 0; i < PINK_STAGES; i++) {
        pink_rows[i] = fast_rand();
        pink_running_sum += pink_rows[i];
    }
}

// Returns a 12-bit pink noise sample (0–4095)
uint16_t pink_noise_sample(void) {
    uint32_t last_sum = pink_running_sum;
    pink_index = (pink_index + 1) & ((1 << PINK_STAGES) - 1);

    // Find the lowest set bit in the index — this determines which stage to update
    uint32_t bit = pink_index & (-pink_index); // isolate lowest bit
    uint32_t stage = 0;
    uint32_t b = bit;
    while (b >>= 1) stage++;
    if (stage < PINK_STAGES) {
        pink_running_sum -= pink_rows[stage];
        pink_rows[stage] = fast_rand();
        pink_running_sum += pink_rows[stage];
    }

    // Add a fresh white noise term to each sample (fills high-frequency content)
    uint32_t white = fast_rand();
    uint32_t sum = pink_running_sum + white;

    // Normalise: sum of PINK_STAGES+1 uint32_t values → shift down to 12-bit
    // Each stage contributes ~32 bits; sum of 17 terms ≈ 36-bit range
    // Scale to 0–4095 and bias to midpoint
    uint16_t out = (uint16_t)((sum >> 22) & 0xFFF);
    return out;
}

// --- Timer2 ISR at SAMPLE_RATE ---
void __ISR(_TIMER_2_VECTOR, IPL4SOFT) T2_ISR(void) {
    IFS0bits.T2IF = 0;
    dac_write(pink_noise_sample());
}

void timer2_init(void) {
    T2CON = 0;
    PR2 = (SYS_FREQ / SAMPLE_RATE) - 1;  // 4999 for 8 kHz @ 40 MHz
    IPC2bits.T2IP = 4;
    IFS0bits.T2IF = 0;
    IEC0bits.T2IE = 1;
    T2CONbits.ON  = 1;
}

int main(void) {
    spi1_init();
    pink_noise_init();
    timer2_init();
    __builtin_enable_interrupts();
    while (1);  // ISR handles everything
    return 0;
}
