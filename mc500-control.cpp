#ifndef __AVR_ATmega328P__
  #define __AVR_ATmega328P__ 
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdbool.h>
#include <string.h> 
#include <util/delay.h>
#include "debounce.h"
#include "debounce.cpp"
#include "rotary.h"
#include "rotary.cpp"
#include "ShiftRegister.cpp"
#include "ToggleSwitch.cpp"
#include "twi-master.h"
#include "twi-master.c"
#include "uart.h"
#include "uart.c"

volatile uint8_t _switchState = 0;
volatile uint8_t _attenuationMain = 127;
volatile uint8_t _gainHP = 0;
volatile bool IsDim = false;
volatile bool DoScan;

#define COUNTER 65500;

#define INPUT_1_ID 1;
#define INPUT_2_ID 2;
#define INPUT_3_ID 3;
#define OUTPUT_1_ID 4;
#define OUTPUT_2_ID 5;
#define OUTPUT_3_ID 6;

#define INPUT_1 PB1;
#define INPUT_2 PB2;
#define INPUT_3 PB3;
#define OUTPUT_1 PB4;
#define OUTPUT_2 PC0;
#define OUTPUT_3 PC1;

#define DIM_OFFSET_UINT 32

ISR (TIMER1_OVF_vect)
{
    DoScan = true;
	TCNT1 = COUNTER;
}

ISR (PCINT1_vect)
{
    static uint8_t state = 3;
    static int8_t encval = 0;  
    static const int8_t enc_states [] PROGMEM = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};

    state = (state<<2) & 0x0f;
    uint8_t pincRead = PINC;
    state |= bit_is_clear(pincRead, PINC1)<<PINC1 | bit_is_clear(pincRead, PINC0)<<PINC0;

    encval += pgm_read_byte(&(enc_states[state]));

    if( encval > 3 ) //four steps forward
    {
        encval = 0;

        if (_attenuationMain == 0)
        {
            return;
        }

        _attenuationMain -=1;
    }
    else if( encval < -3 ) //four steps backwards
    {
        encval = 0;

        if (_attenuationMain == 127)
        {
            return;
        }

        _attenuationMain +=1;
    }
}

void timer_init(void)
{
    TCNT1 = COUNTER; //63974;   // for 1 sec at 16 MHz	
	TCCR1A = 0x00;
	TCCR1B = (1<<CS10) | (1<<CS12);  // Timer mode with 1024 prescler
	TIMSK1 = (1 << TOIE1) ;   // Enable timer1 overflow interrupt(TOIE1)
}
 
void pin_change_interrupt_init(void)
{
    PCICR |= 1<<PCIE1;
    PCMSK1 |= (1<<PCINT8) | (1<<PCINT9);
}

void interrupt_init(void)
{
    debounce_init();
    timer_init();
    pin_change_interrupt_init();

	sei();
}

int main (void)
{
    // pullups
    PORTB |= (1<<PB2) | (1<<PB1) | (1<<PB0);
    PORTC |= (1<<PC5) | (1<<PC4);
    PORTD |= (1<<PD7) | (1<<PD6) | (1<<PD5) | (1<<PD3) | (1<<PD2);

    // outputs
    DDRB |= (1<<PB4);
    DDRB |= (1<<PB3);
    DDRC |= (1<<PC2);

    // inputs
    DDRB &= ~(1 << PINB5);
    DDRB &= ~(1 << PINB2);
    DDRB &= ~(1 << PINB1);
    DDRB &= ~(1 << PINB0);
    DDRC &= ~(1 << PINC0);
    DDRC &= ~(1 << PINC1);
    DDRD &= ~(1 << PIND7);
    DDRD &= ~(1 << PIND6);
    DDRD &= ~(1 << PIND5);
    DDRD &= ~(1 << PIND3);
    DDRD &= ~(1 << PIND2);

    interrupt_init();
    tw_init(TW_FREQ_250K, true);

    uart_init();
    stdout = &uart_output;
    //stdin  = &uart_input;

    ExclusiveToggleSwitchGroup inputSwitchGroup = ExclusiveToggleSwitchGroup(
        ToggleSwitch(&PIND, PIND2, &_switchState, 7, 4),
        ToggleSwitch(&PIND, PIND3, &_switchState, 6, 4),
        ToggleSwitch(&PIND, PIND5, &_switchState, 5, 4));

    ExclusiveToggleSwitchGroup outputSwitchGroup = ExclusiveToggleSwitchGroup(
        ToggleSwitch(&PIND, PIND6, &_switchState, 4, 4),
        ToggleSwitch(&PIND, PIND7, &_switchState, 3, 4),
        ToggleSwitch(&PINB, PINB0, &_switchState, 2, 1));
    
    ////_rotaryEncoder = Rotary(&PINC, PINC0, &PINC, PINC1);

    ToggleSwitch monoSwitch = ToggleSwitch(&PINB, PINB1, &_switchState, 1, 1);
    ToggleSwitch dimSwitch = ToggleSwitch(&PINB, PINB2, &_switchState, 0, 1);

    ShiftRegister shiftRegister = ShiftRegister();
	
    _switchState = 0b10010000;
    uint8_t lastSwitchState = 0;
    uint8_t lastAttenuationMain = 0;
    int txCounter = 0;
    bool doShift = false;
    bool doI2cTx = false;

    char _stringOut[30] = {0};

    while(1)
    {
        if (DoScan)
        {
            DoScan = false;

            debounce();
            inputSwitchGroup.Scan();
            outputSwitchGroup.Scan();
            monoSwitch.Scan();
            dimSwitch.Scan();

            if (txCounter++ % 16 == 0)
            {

                if (_switchState != lastSwitchState)
                {
                    lastSwitchState = _switchState;
                    doShift = true;
                    doI2cTx = true;
                }
                
                uint8_t attenuationMain = lastAttenuationMain;

                if (_attenuationMain != lastAttenuationMain)
                {
                    //sprintf(_stringOut, "attenuation: \t%d\r\n", _attenuationMain);
                    //printf(_stringOut);
                    lastAttenuationMain = _attenuationMain;
                    doI2cTx = true;

                    if (dimSwitch.Get())
                    {
                        if (attenuationMain + DIM_OFFSET_UINT > 127)
                        {
                            attenuationMain = 127;
                        }
                        else
                        {
                            attenuationMain = attenuationMain+DIM_OFFSET_UINT;
                        }
                        //attenuationMain = attenuationMain/2;

                        //if (attenuationMain != lastAttenuationMain)
                        //{
                        //    sprintf(_stringOut, "attenuation: \t%d\r\n", attenuationMain);
                        //    printf(_stringOut);
                        //}
                    }
                }

                if (doI2cTx)
                {
                    ret_code_t error_code;
                    uint8_t data[2] = { attenuationMain, lastSwitchState };
                    error_code = tw_master_transmit(0x10, data, sizeof(data), false);
                    doI2cTx = false;
                }

                if (doShift)
                {
                    shiftRegister.shiftOut(lastSwitchState);
                    doShift = false;
                }
            }
        }
    }

    return 0;
}

