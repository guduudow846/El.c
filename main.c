#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdbool.h>
#include <stdint.h>

// --- Pin map  ---
// Normal LEDs
#define NLED_RED   PB5   // D13
#define NLED_GREEN PB4   // D12
#define NLED_BLUE  PB3   // D11
#define NLED_WHITE PB2   // D10

// RGB LED
#define RGB_R PB0        // D8
#define RGB_G PD7        // D7
#define RGB_B PD6        // D6

// Buttons
#define BTN1_TOGGLE PD5  // D5 (btn1)
#define BTN2_RESET  PB1  // D9 (btn2)

// Rotary encoder
#define ENC_CLK PD2      // D2
#define ENC_DT  PD3      // D3
#define ENC_SW  PD4      // D4

#define POT_A0_CH 0      // A0

typedef enum {
    SEL_RED = 0,
    SEL_GREEN,
    SEL_BLUE,
    SEL_WHITE,
    SEL_OFF
} Selection;

typedef enum {
    LED_BLINK = 0,
    LED_HOLD
} LedMode;

typedef struct {
    bool stable;
    bool raw;
    uint32_t changed_at;
    bool press_event;
} Debounce;

static volatile uint32_t g_ms = 0;

static Selection g_selected = SEL_OFF;
static bool g_active = false;

static LedMode g_mode[4] = { LED_BLINK, LED_BLINK, LED_BLINK, LED_BLINK };
static bool g_hold_state[4] = { false, false, false, false };

static bool g_normal_phase = false;
static uint32_t g_last_normal_ms = 0;

static bool g_rgb_phase = false;
static uint32_t g_last_rgb_ms = 0;

static Debounce g_btn1 = { false, false, 0, false };
static Debounce g_btn2 = { false, false, 0, false };
static Debounce g_enc_btn = { false, false, 0, false };

ISR(TIMER0_COMPA_vect) {
    g_ms++;
}

static uint32_t millis_now(void) {
    uint32_t now;
    uint8_t sreg = SREG;
    cli();
    now = g_ms;
    SREG = sreg;
    return now;
}

static void timer_init(void) {
    TCCR0A = (1 << WGM01);
    TCCR0B = (1 << CS01) | (1 << CS00);
    OCR0A = 249;
    TIMSK0 = (1 << OCIE0A);
}

static void adc_init(void) {
    ADMUX = (1 << REFS0);
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

static uint16_t adc_read(uint8_t channel) {
    ADMUX = (ADMUX & 0xF0) | (channel & 0x0F) | (1 << REFS0);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC)) {
    }
    return ADC;
}

static void io_init(void) {
    DDRB |= (1 << NLED_WHITE) | (1 << NLED_RED) | (1 << NLED_GREEN) | (1 << NLED_BLUE);
    PORTB &= ~((1 << NLED_WHITE) | (1 << NLED_RED) | (1 << NLED_GREEN) | (1 << NLED_BLUE));

    DDRB |= (1 << RGB_R);
    PORTB &= ~(1 << RGB_R);

    DDRD |= (1 << RGB_G) | (1 << RGB_B);
    PORTD &= ~((1 << RGB_G) | (1 << RGB_B));

    DDRD &= ~(1 << BTN1_TOGGLE);
    PORTD |= (1 << BTN1_TOGGLE);

    DDRB &= ~(1 << BTN2_RESET);
    PORTB |= (1 << BTN2_RESET);

    DDRD &= ~((1 << ENC_CLK) | (1 << ENC_DT) | (1 << ENC_SW));
    PORTD |= (1 << ENC_CLK) | (1 << ENC_DT) | (1 << ENC_SW);
}

static void normal_led_set(uint8_t index, bool on) {
    uint8_t pin = NLED_RED;
    switch (index) {
        case 0: pin = NLED_RED; break;
        case 1: pin = NLED_GREEN; break;
        case 2: pin = NLED_BLUE; break;
        case 3: pin = NLED_WHITE; break;
        default: return;
    }

    if (on) PORTB |= (1 << pin);
    else PORTB &= ~(1 << pin);
}

static void rgb_set_raw(bool r, bool g, bool b) {
    if (r) PORTB |= (1 << RGB_R); else PORTB &= ~(1 << RGB_R);
    if (g) PORTD |= (1 << RGB_G); else PORTD &= ~(1 << RGB_G);
    if (b) PORTD |= (1 << RGB_B); else PORTD &= ~(1 << RGB_B);
}

static void rgb_apply(Selection s) {
    switch (s) {
        case SEL_RED: rgb_set_raw(true, false, false); break;
        case SEL_GREEN: rgb_set_raw(false, true, false); break;
        case SEL_BLUE: rgb_set_raw(false, false, true); break;
        case SEL_WHITE: rgb_set_raw(true, true, true); break;
        case SEL_OFF: rgb_set_raw(false, false, false); break;
        default: rgb_set_raw(false, false, false); break;
    }
}

static Selection next_sel(Selection s) {
    switch (s) {
        case SEL_RED: return SEL_GREEN;
        case SEL_GREEN: return SEL_BLUE;
        case SEL_BLUE: return SEL_WHITE;
        case SEL_WHITE: return SEL_OFF;
        case SEL_OFF: return SEL_RED;
        default: return SEL_OFF;
    }
}

static Selection prev_sel(Selection s) {
    switch (s) {
        case SEL_RED: return SEL_OFF;
        case SEL_GREEN: return SEL_RED;
        case SEL_BLUE: return SEL_GREEN;
        case SEL_WHITE: return SEL_BLUE;
        case SEL_OFF: return SEL_WHITE;
        default: return SEL_OFF;
    }
}

static uint8_t sel_to_led_index(Selection s) {
    switch (s) {
        case SEL_RED: return 0;
        case SEL_GREEN: return 1;
        case SEL_BLUE: return 2;
        case SEL_WHITE: return 3;
        default: return 0;
    }
}

static void debounce_update(Debounce *d, bool raw_pressed, uint32_t now) {
    if (raw_pressed != d->raw) {
        d->raw = raw_pressed;
        d->changed_at = now;
    }

    if ((now - d->changed_at) >= 25U) {
        if (d->stable != d->raw) {
            d->stable = d->raw;
            if (d->stable) {
                d->press_event = true;
            }
        }
    }
}

static bool debounce_take_press(Debounce *d) {
    if (!d->press_event) return false;
    d->press_event = false;
    return true;
}

static int8_t encoder_step(void) {
    static const int8_t lut[16] = {
        0, -1, 1, 0,
        1, 0, 0, -1,
        -1, 0, 0, 1,
        0, 1, -1, 0
    };
    static int8_t acc = 0;
    static uint8_t prev = 0;

    uint8_t a = (PIND & (1 << ENC_CLK)) ? 1U : 0U;
    uint8_t b = (PIND & (1 << ENC_DT)) ? 1U : 0U;
    uint8_t curr = (uint8_t)((a << 1) | b);

    uint8_t idx = (uint8_t)((prev << 2) | curr);
    prev = curr;
    acc += lut[idx];

    if (acc >= 4) {
        acc = 0;
        return 1;
    }
    if (acc <= -4) {
        acc = 0;
        return -1;
    }
    return 0;
}

static uint16_t extra_delay_from_a0(uint16_t adc) {
    uint16_t mapped_255 = adc / 4U;
    return (uint16_t)(mapped_255 * 10U);
}

int main(void) {
    io_init();
    adc_init();
    timer_init();
    sei();

    while (1) {
        uint32_t now = millis_now();

        debounce_update(&g_btn1, !(PIND & (1 << BTN1_TOGGLE)), now);
        debounce_update(&g_btn2, !(PINB & (1 << BTN2_RESET)), now);
        debounce_update(&g_enc_btn, !(PIND & (1 << ENC_SW)), now);

        if (!g_active) {
            int8_t step = encoder_step();
            if (step > 0) g_selected = next_sel(g_selected);
            else if (step < 0) g_selected = prev_sel(g_selected);
        }

        if (debounce_take_press(&g_enc_btn)) {
            g_active = !g_active;
            g_rgb_phase = false;
            g_last_rgb_ms = now;
        }

        if (debounce_take_press(&g_btn1)) {
            if (g_active && g_selected != SEL_OFF) {
                uint8_t i = sel_to_led_index(g_selected);
                if (g_mode[i] == LED_BLINK) {
                    g_mode[i] = LED_HOLD;
                }
                g_hold_state[i] = !g_hold_state[i];
            }
        }

        if (debounce_take_press(&g_btn2)) {
            if (g_active && g_selected != SEL_OFF) {
                uint8_t i = sel_to_led_index(g_selected);
                g_mode[i] = LED_BLINK;
            }
        }

        uint16_t interval = (uint16_t)(250U + extra_delay_from_a0(adc_read(POT_A0_CH)));
        if ((uint32_t)(now - g_last_normal_ms) >= interval) {
            g_last_normal_ms = now;
            g_normal_phase = !g_normal_phase;
        }

        for (uint8_t i = 0; i < 4; i++) {
            bool on = (g_mode[i] == LED_BLINK) ? g_normal_phase : g_hold_state[i];
            normal_led_set(i, on);
        }

        if (g_active && g_selected != SEL_OFF) {
            if ((uint32_t)(now - g_last_rgb_ms) >= 250U) {
                g_last_rgb_ms = now;
                g_rgb_phase = !g_rgb_phase;
            }
            rgb_apply(g_rgb_phase ? g_selected : SEL_OFF);
        } else {
            rgb_apply(g_selected);
        }
    }
}
