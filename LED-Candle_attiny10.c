// LED-Candle_attiny10.c
// Dual LED Candle with daylight sensor and low battery detection
//		by Paul Duke	2013
//
//	Sense supply voltage and turn off but with occasional blinking
//	when voltage drops below threshold.
//		Paul Duke 2016

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>

#define LED1								0											// PB0 - LED1
#define LED2								1											// PB1 - LED2
#define RC_PIN 							2											// PB2 - light/dark sense
#define BATTERY							3											// PB3 - Battery low voltage sense

#define LED1_PWM				OCR0A										  // LED1 brightness
#define LED2_PWM				OCR0B										  // LED2 brightness

#define PWM_MIN 				0x80

// Daylight sensor threshold
#define TURN_OFF 400
#define TURN_ON TURN_OFF+800

#define ON 1
#define OFF 0
volatile uint16_t light;

ISR(WDT_vect, ISR_BLOCK){
	// Daylight sense -
	// measure resistance across a photocell by charging 
	// a capacitor and then letting it discharge thru
	// the photocell and measuring the discharge time.
	DDRB |= _BV(RC_PIN);														// OUTPUT RC pin
	light = 0;																			// reset counter

	PORTB |= _BV(RC_PIN);														// charge the capacitor
	_delay_us(100);																	// for a moment then
	DDRB &= ~_BV(RC_PIN);														// setup high impedence
	PORTB &= ~_BV(RC_PIN);													// and turn off power (no pull-up)

	// time voltage drain on capacitor
	for(;PINB & _BV(RC_PIN);){											// pin at logical 0?
		_delay_us(1);																	// nope, keep counting
		light++;
		if(light > (TURN_ON*3)){											// it's definately dark
			DDRB |= _BV(RC_PIN);												// drain the capacitor
			_delay_us(200);															// give it time to drain
			break;
		}
	}
}

//---------------------------------------------------------------------------------------
int8_t led1_step;
int8_t led2_step;
uint8_t led1_final;
uint8_t led2_final;
uint8_t lowBatteryCount;

void main() {
	// bump clock to 8MHz
	CCP = 0xD8;																				// enable protected change
	CLKPSR = 0;               												// set 8MHz prescaler = 1

	// power reduction
	ACSR = _BV(ACD);																	// disable analog comparator
	DIDR0 = _BV(ADC0D) | _BV(ADC1D);									// disable digital inputs for LEDs
	PRR = _BV(PRADC);																	// disable adc

	// setup ports
	DDRB = 0x03;																			// PB0 & PB1 OUTPUT, PB2 & PB3 INPUT

	// startup watchdog timer
	CCP = 0xD8;																				// enable protected change
	WDTCSR = _BV(WDP2) | _BV(WDP1);										// 1 second timeout
	WDTCSR |= _BV(WDIE);															// interrupt on timeout
	sei();																						// enable global interrupts
	uint32_t rand_value;
	uint8_t state = OFF;

	for(;;){
		if(PINB & _BV(BATTERY)){												// positive reading means battery low
			lowBatteryCount++;
			if(lowBatteryCount == 255){
				lowBatteryCount = 254;											// prevent overflow
			}
		} else {
			lowBatteryCount = 0;
		}

		if(light <= TURN_OFF){													// is it light?
			state = OFF;
		} else {
			if(light >= TURN_ON){													// or is it dark?
				state = ON;
			}
		}

		if(state == OFF){
			if(TCCR0B){																		// nope, timer running?
				TCCR0B = 0;																	// yep, stop it
				TCCR0A = 0;
				PORTB &= ~_BV(LED1);												// ensure LEDs are off
				PORTB &= ~_BV(LED2);
			}
			set_sleep_mode(SLEEP_MODE_PWR_DOWN);					// and powerdown until watchdog wakes
			sleep_enable();
			sleep_cpu();
			sleep_disable();
		} else {	// if not OFF then has to be ON
			if(TCCR0B == 0){															// timer running?
				LED1_PWM = 0; 												// nope, set OCRxx min
				LED2_PWM = 0;
				TCCR0A = _BV(COM0A1) | _BV(COM0B1) | _BV(WGM00);	// start timer
				TCCR0B = _BV(CS00) | _BV(WGM02);
				rand_value = light;													// new random start
			}
			if(lowBatteryCount>100){											// battery is low
				LED1_PWM = PWM_MIN;													// give a low power flash
				_delay_ms(25);
				TCCR0B = 0;																	// stop timer
				TCCR0A = 0;
				PORTB &= ~_BV(LED1);												// ensure LEDs are off
				PORTB &= ~_BV(LED2);
				sleep_enable();															// and sleep until watchdog wakeup
				sleep_cpu();
				sleep_disable();
			} else {
				#define CYCLE_MS 127
				#define RAND_MAX_32 ((1UL << 31) - 1)
				rand_value = ((rand_value * 214013UL + 2531011UL) & RAND_MAX_32);
				#define BIT_SHIFT 3
				#define STEPS (1<<BIT_SHIFT)
				led2_final = led1_final;
				led1_final = ((rand_value % 0x7F)|PWM_MIN);
				led2_step = ((led2_final - LED2_PWM)>>BIT_SHIFT);
				led1_step = ((led1_final - LED1_PWM)>>BIT_SHIFT);
				for(uint8_t i=0;i<(STEPS-1);i++){
					LED2_PWM += led2_step;
					LED1_PWM += led1_step;
					_delay_ms(CYCLE_MS>>BIT_SHIFT);
				}
				LED2_PWM = led2_final;
				LED1_PWM = led1_final;
				_delay_ms(CYCLE_MS>>BIT_SHIFT);
			}
		}
	}
}

