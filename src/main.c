/*
    ToDos:
        - save reset to eeprom
        - do not wait til eeprom is ready to write
*/

#include <avr/io.h>
#include <util/delay.h>

#define MANCHESTER_GAP_THRESHOLD 180U //
#define MANCHESTER_GLITCH_FILTER 16U  // ignore pulses shorter than ~16 µs
#define MANCHESTER_MIN_GAP 75U        //
#define MANCHESTER_MAX_GAP 125U       //

typedef enum
{
    START_BIT,
    PAYLOAD
} MANCHESTER_STATEMACHINE;

typedef enum
{
    ON,
    OFF
} LED_STATE;

typedef enum
{
    EDGE_FALLING,
    EDGE_RISING,
} EdgeDirection;

typedef enum
{
    IR_RELEASE,
    IR_AWAIT_SE,
    IR_AWAIT_TE,
    IR_IDLE
} IR_STATEMACHINE;

typedef enum
{
    EEPROM_IDLE,
    EEPROM_RELEASE_POSITION,
    EEPROM_WAIT_1_POSITION,
    EEPROM_WAIT_2_POSITION,
    EEPROM_WRITE_POSITION,
    EEPROM_WRITE_ONOFF
} EEPROM_STATE_MACHINE;

typedef struct
{
    uint8_t position;
    uint8_t id;
} DRIVER;

typedef enum
{
    SETTINGS_IDLE,
    SETTINGS_INIT_1,
    SETTINGS_INIT_2,
    SETTINGS_POS_1_1,
    SETTINGS_POS_1_2,
    SETTINGS_POS_2_1,
    SETTINGS_POS_2_2,
    SETTINGS_POS_3_1,
    SETTINGS_POS_3_2,
    SETTINGS_POS_4_1,
    SETTINGS_POS_4_2,
    SETTINGS_ON_OFF_1,
    SETTINGS_ON_OFF_2
} SETTINGS;


/* =========================
   LUT
   ========================= */

// lsb decode id
uint8_t id_lut_reverb[8] = { // 0, 1, 2, 3, 4, 5, 6, 7
    0x00, 0x04, 0x02, 0x06, 0x01, 0x05, 0x03, 0x07};



/* =========================
   VARIABLES
   ========================= */

DRIVER driver = {0};

LED_STATE led_state = ON;
uint16_t led_timer;

IR_STATEMACHINE ir_state = IR_RELEASE;
uint8_t ir_edge_detection = 0;
uint8_t ir_measurements[2] = {0};
uint8_t ir_changed_positive = 0;

uint8_t track_edge_detection = 0;
uint16_t data;
uint16_t data_t;
uint16_t last_edge;
EdgeDirection edge_direction;

EEPROM_STATE_MACHINE eeprom_state_machine = EEPROM_IDLE;

SETTINGS settings_state_machine = SETTINGS_IDLE;
uint16_t settings_timeout = 0;
uint8_t settings_edge_trigger = 0;
uint8_t settings_edge_t = 0;

uint16_t timer_us = 0;
uint16_t timer_ms = 0;
uint16_t timer_ms_flag = 0;
uint16_t timer_s_flag = 0;
uint8_t timer1_flag = 0;


/* =========================
   TRACK DATA DECODER
   ========================= */

uint8_t track_data_decode(uint16_t capture, EdgeDirection direction)
{
    uint16_t diff = capture - last_edge;
    if (diff <= MANCHESTER_GLITCH_FILTER)
    {
        return 0; // glitch: nothing to do
    }

    if (MANCHESTER_GAP_THRESHOLD < diff)
    {
        last_edge = capture;
        data = data_t;
        data_t = 0x0001;
        return 1;
    }
    else
    {
        if ((MANCHESTER_MIN_GAP <= diff) && (diff < MANCHESTER_MAX_GAP))
        {
            last_edge = capture;
            data_t <<= 1;
            if (EDGE_FALLING == direction)
                data_t |= 0x0001;
        }
    }

    return 0;
}

/* =========================
   TIMER SETUP
   ========================= */

// 125kHz -> 8us
void timer0_init(void)
{
    TCCR0A = 0x00;
    TCCR0B = (1 << CS01) | (1 << CS00);

    // Reset counter
    TCNT0 = 0;
}

void timer1_init(void)
{
    TCCR1 = 0x00;
    TCCR1 |= (1 << CS12);

    // Reset counter
    TCNT1 = 0;
}

/* =========================
   GPIO SETUP
   ========================= */

void gpio_init(void)
{
    // PB0, PB1, PB2 as outputs
    DDRB |= (1 << PB0) | (1 << PB1) | (1 << PB2);

    // PB3, PB4 as inputs
    DDRB &= ~((1 << PB3) | (1 << PB4));
}

/* =========================
   INPUT READ
   ========================= */

static inline uint8_t read_input(uint8_t pin)
{
    return (PINB >> pin) & 1;
}

/* =========================
   EEPROM
   =========================
*/
uint8_t EEPROM_write(unsigned char ucAddress, unsigned char ucData)
{
    /* Wait for completion of previous write */
    // while (EECR & (1 << EEPE))
    //     ;
    if (EECR & (1 << EEPE))
        return 0; // still busy and skip

    /* Set Programming mode */
    EECR = (0 << EEPM1) | (0 << EEPM0);
    /* Set up address and data registers */
    EEAR = ucAddress;
    EEDR = ucData;
    /* Write logical one to EEMPE */
    EECR |= (1 << EEMPE);
    /* Start eeprom write by setting EEPE */
    EECR |= (1 << EEPE);
    /* Return success */
    return 1;
}

unsigned char EEPROM_read(unsigned char ucAddress)
{
    /* Wait for completion of previous write */
    while (EECR & (1 << EEPE))
        ;
    /* Set up address register */
    EEAR = ucAddress;
    /* Start eeprom read by writing EERE */
    EECR |= (1 << EERE);
    /* Return data from data register */
    return EEDR;
}

/* =========================
   LEADER LIGHT
   =========================
*/

void set_leader_lights(uint8_t value)
{
    if (OFF == led_state)
    {
        PORTB &= ~(1 << PB0);
        PORTB &= ~(1 << PB1);
        PORTB &= ~(1 << PB2);
        return;
    }

    if (value == 0x08)
    {
        PORTB |= (1 << PB0);
        PORTB &= ~(1 << PB1);
        PORTB &= ~(1 << PB2);
    }
    else if (value == 0x04)
    {
        PORTB |= (1 << PB0);
        PORTB |= (1 << PB1);
        PORTB &= ~(1 << PB2);
    }
    else if (value == 0x0c)
    {
        PORTB |= (1 << PB0);
        PORTB |= (1 << PB1);
        PORTB |= (1 << PB2);
    }
    else
    {
        PORTB &= ~(1 << PB0);
        PORTB &= ~(1 << PB1);
        PORTB &= ~(1 << PB2);
    }
}


/* =========================
   MAIN
   ========================= */

int main(void)
{
    (void)gpio_init();
    (void)timer0_init();
    (void)timer1_init();

    led_state = ON;

    driver.position = EEPROM_read(0x00);
    if (driver.position != 0x00 &&
        driver.position != 0x04 &&
        driver.position != 0x08 &&
        driver.position != 0x0c)
    {
        driver.position = 0x00; // default

        PORTB |= (1 << PB2);
        _delay_ms(2000);
        PORTB &= ~(1 << PB2);
    }

    led_state = (LED_STATE)EEPROM_read(0x01);

    (void)set_leader_lights(driver.position);

    while (1)
    {
        // read timer and process
        uint8_t t = TCNT1;
        uint8_t t_overflow = t & (1 << 7);

        if (!t_overflow && !timer1_flag)
        {
            timer1_flag = 1;
            timer_us += 256;
        }
        else if (t_overflow)
        {
            timer1_flag = 0;
        }

        uint16_t time = timer_us + (uint16_t)t;
        if ((uint16_t)(time - timer_ms_flag) > (uint16_t)1000)
        {
            timer_ms_flag = time;
            timer_ms++;
        }

        // read track signal and process
        uint8_t input_track = read_input(PB4);
        uint8_t changed = input_track ^ track_edge_detection;
        track_edge_detection = input_track;
        if (changed)
        {
            uint8_t message_available = 0;
            message_available = track_data_decode(time, track_edge_detection);

            if (message_available) // 0x1000 proofs if status message
            {
                uint16_t msg = data;
                if (msg & 0x1000)
                {
                    ir_state = IR_RELEASE; // release id detection after status message

                    uint8_t low = msg & 0xFF;
                    uint8_t high = msg >> 8;

                    uint8_t command = (low >> 3) & 0x1F;
                    uint8_t value = high & 0x0F;

                    if (0x01 == command) // 16
                    {                    // startlights
                        switch (value)
                        {
                        case 0x00: // all lights out
                            break;
                        case 0x08: // one light
                                   // reset the positions
                            driver.position = 0;
                            (void)set_leader_lights(0);
                            break;
                        case 0x04: // two lights
                            break;
                        case 0x0c: // three lights
                            break;
                        case 0x02: // four lights
                            break;
                        case 0x0a: // five lights
                            break;
                        }
                    }
                    else if (0x0c == command) // car related message - (6)
                    {
                        if (value && (0x09 != value)) // position message
                        {
                            uint8_t controller = low & 0x07;
                            if ((id_lut_reverb[controller] + 0x01) == driver.id)
                            {
                                driver.position = value; // position [1,8]
                                set_leader_lights(value);
                                eeprom_state_machine = EEPROM_RELEASE_POSITION;
                            }
                        }
                        else if (0x09 == value) // reset - does not work with cxp
                        {
                            //__BUILTIN_AVR_NOP;
                        }
                    }

                    switch (eeprom_state_machine)
                    {
                    case EEPROM_RELEASE_POSITION:
                        eeprom_state_machine = EEPROM_WAIT_1_POSITION;
                        break;
                    case EEPROM_WAIT_1_POSITION:
                        eeprom_state_machine = EEPROM_WRITE_POSITION;
                        break;
                    default:
                        eeprom_state_machine = EEPROM_IDLE;
                        break;
                    }
                }
                else if (msg & 0x0200) // controller message
                {
                    // 0 0 0 0   0 0 1 R2  /  R1 R0 SW G3   G2 G1 G0 TA

                    uint8_t controller = (uint8_t)(msg >> 6) & 0x07;

                    // CONTROLLER IS MSB IN THIS CASE !!!!
                    if ((controller + 1) == driver.id)
                    {
                        uint8_t low = msg & 0xFF;
                        uint8_t settings_button = (low & 0x20) >> 5;
                        settings_edge_trigger = !settings_edge_t && settings_button;
                        settings_edge_t = settings_button;

                        if (settings_edge_trigger)
                        {
                            // PORTB |= (1 << PB2); // indicate a falling edge for debugging purposes
                            settings_timeout = timer_ms;

                            switch (settings_state_machine)
                            {
                            case SETTINGS_IDLE:
                                settings_state_machine = SETTINGS_INIT_1;
                                break;
                            case SETTINGS_INIT_1:
                                settings_state_machine = SETTINGS_INIT_2;
                                break;
                            case SETTINGS_INIT_2:
                                settings_state_machine = SETTINGS_POS_1_1;
                                break;
                            case SETTINGS_POS_1_1:
                                settings_state_machine = SETTINGS_POS_1_2;
                                break;
                            case SETTINGS_POS_1_2:
                                settings_state_machine = SETTINGS_POS_2_1;
                                break;
                            case SETTINGS_POS_2_1:
                                settings_state_machine = SETTINGS_POS_2_2;
                                break;
                            case SETTINGS_POS_2_2:
                                settings_state_machine = SETTINGS_POS_3_1;
                                break;
                            case SETTINGS_POS_3_1:
                                settings_state_machine = SETTINGS_POS_3_2;
                                break;
                            case SETTINGS_POS_3_2:
                                settings_state_machine = SETTINGS_POS_4_1;
                                break;
                            case SETTINGS_POS_4_1:
                                settings_state_machine = SETTINGS_POS_4_2;
                                break;
                            case SETTINGS_POS_4_2:
                                settings_state_machine = SETTINGS_ON_OFF_1;
                                break;
                            case SETTINGS_ON_OFF_1:
                                settings_state_machine = SETTINGS_ON_OFF_2;
                                break;
                            default:
                                settings_state_machine = SETTINGS_IDLE;
                                break;
                            }
                        }

                        if (SETTINGS_IDLE != settings_state_machine)
                        {
                            if ((uint16_t)(timer_ms - settings_timeout) > (uint16_t)1000)
                            {
                                switch (settings_state_machine)
                                {
                                case SETTINGS_POS_1_2:
                                    set_leader_lights(0x08);
                                    driver.position = 0x08;
                                    eeprom_state_machine = EEPROM_RELEASE_POSITION;
                                    break;
                                case SETTINGS_POS_2_2:
                                    set_leader_lights(0x04);
                                    driver.position = 0x04;
                                    eeprom_state_machine = EEPROM_RELEASE_POSITION;
                                    break;
                                case SETTINGS_POS_3_2:
                                    set_leader_lights(0x0c);
                                    driver.position = 0x0c;
                                    eeprom_state_machine = EEPROM_RELEASE_POSITION;
                                    break;
                                case SETTINGS_POS_4_2:
                                    set_leader_lights(0x00);
                                    driver.position = 0x00;
                                    eeprom_state_machine = EEPROM_RELEASE_POSITION;
                                    break;
                                case SETTINGS_ON_OFF_2:
                                    if (eeprom_state_machine == EEPROM_IDLE)
                                    {
                                        led_state = led_state == ON ? OFF : ON;
                                        set_leader_lights(driver.position);
                                        eeprom_state_machine = EEPROM_WRITE_ONOFF;
                                    }
                                    break;
                                default:
                                    break;
                                }
                                settings_state_machine = SETTINGS_IDLE;
                            }
                        }
                    }
                }
                else if (msg & 0x0080)
                {
                    // if (msg & 0x0008)
                    //     PORTB ^= (1 << PB2);
                }
            }
        }

        uint8_t ir_input = read_input(PB3);
        ir_changed_positive = ir_input && !ir_edge_detection;
        // if (ir_changed_positive)
        switch (ir_state)
        {
        case IR_RELEASE:
            if (ir_changed_positive)
            {
                ir_measurements[0] = TCNT0;
                ir_state = IR_AWAIT_SE;
            }
            break;
        case IR_AWAIT_SE:
            if (ir_changed_positive)
            {
                ir_measurements[1] = TCNT0;
                ir_state = IR_AWAIT_TE;
            }
            break;
        case IR_AWAIT_TE:
            if (ir_changed_positive)
            {
                uint8_t timer_value = TCNT0;
                uint8_t id_t0 = (ir_measurements[1] - ir_measurements[0] + 4) >> 3;
                uint8_t id_t1 = (timer_value - ir_measurements[1] + 4) >> 3;
                // if ((id_t0 >= id_t1 - 1) && (id_t0 <= id_t1 + 1))
                if (id_t0 == id_t1)
                {
                    driver.id = id_t0;
                }
                ir_state = IR_IDLE;
            }
            break;
        }
        ir_edge_detection = ir_input;

        if (EEPROM_WRITE_POSITION == eeprom_state_machine)
        {
            if (EEPROM_write(0x00, driver.position))
                eeprom_state_machine = EEPROM_IDLE;
        }
        else if (EEPROM_WRITE_ONOFF == eeprom_state_machine)
        {
            if (EEPROM_write(0x01, (uint8_t)led_state))
                eeprom_state_machine = EEPROM_IDLE;
        }
    }
}