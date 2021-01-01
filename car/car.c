// #define __AVR_ATmega88PB__
#include <avr/io.h>
#include <stdlib.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include "radio_receiver.h"
#include "uart.h"
#include "crc.h"

#define DEBUG

#define DEBUG_PIN _BV(5) // PB5

#define MOTOR_LEFT_DIR _BV(4)
#define MOTOR_LEFT_DIR_DDR DDRD
#define MOTOR_LEFT_DIR_REG PORTD

#define MOTOR_RIGHT_DIR _BV(3)
#define MOTOR_RIGHT_DIR_DDR DDRD
#define MOTOR_RIGHT_DIR_REG PORTD

// maximum speed difference (in percent) between both
// motors while turning and moving forwards / backwards at the same time
// this ultimately defines how fast you're able to turn when not stationary
#define MAX_MOTOR_SPEED_DIFF 0.2

enum direction {
	FORWARD,BACKWARD,STOP
};

uint8_t car_stopped;

void motor_init() {

    MOTOR_LEFT_DIR_DDR |= MOTOR_LEFT_DIR;
    MOTOR_RIGHT_DIR_DDR |= MOTOR_RIGHT_DIR;
    
	car_stopped = 1;
    
	DDRD |= (1<<6 | 1<<5); // PD6 (OCR0A) and PD5 (OCR0B)
}

void motor_stop() {
#ifdef DEBUG
	uart_print("\r\nmotor stopped");
#endif

    TCCR0B &= ~(1<<CS02|1<<CS01|1<<CS00);
    TCCR0A &= ~(1<<COM0A1 | 1<<COM0A0 | 1<<COM0B1 | 1<<COM0B0 );  
  
	PORTD &= ~(1<<5|1<<6);

    car_stopped = 1;
}

void motor_start() {
    // phase-correct PWM mode, 0CR0A defines TOP
    TCCR0A |= 1<<WGM00 | 1<<COM0A1 | 1<<COM0B1;	
	TCCR0B |= 1<<CS00; // CPU_FREQ / 8
	car_stopped = 0;
}

void motor_change(enum direction newLeftDir,enum direction newRightDir, uint8_t newDutyLeft, uint8_t newDutyRight) {

    if ( newLeftDir == STOP || newRightDir == STOP ) {
    	motor_stop();
    } else {
    	if ( car_stopped ) {
    		motor_start();
    	}
    	if ( newLeftDir == FORWARD ) {
    		MOTOR_LEFT_DIR_REG |= MOTOR_LEFT_DIR;
    	} else {
			MOTOR_LEFT_DIR_REG &= ~MOTOR_LEFT_DIR;
    	}
    	if ( newRightDir == FORWARD ) {
    		MOTOR_RIGHT_DIR_REG |= MOTOR_RIGHT_DIR;
    	} else {
			MOTOR_RIGHT_DIR_REG &= ~MOTOR_RIGHT_DIR;
    	}    	

    	uint8_t pwm0 = (0xff * newDutyLeft)/100.0;
    	uint8_t pwm1 = (0xff * newDutyRight)/100.0;    	

		OCR0A = pwm0;
		OCR0B = pwm1;

#ifdef DEBUG
	uart_print("\r\nSpeed %: left = ");
	uart_putdecimal(newDutyLeft);
	uart_print(" (");	
	uart_putdecimal(pwm0);
	uart_print(") , right = ");
	uart_putdecimal(newDutyRight);	
	uart_print(" (");		
	uart_putdecimal(pwm1);	
	uart_print(")");			
#endif

	}
}

void main() {

	uint8_t msg[3];

 	DDRC |= (1<<2) | (1<<3);

  #if defined(DEBUG) || defined(DEBUG_IR)
      uart_init();
  #endif	

 	motor_init();
 	radio_receiver_init();

 #ifdef DEBUG
 	uart_print("online");
 #endif	

 	while (1) {
 		int8_t received = radio_receive(&msg[0],3);
 		if ( received == 3 && crc8(&msg[0],3) == 0 ) 
 		{
 			uart_print("\r\nreceived : ");
 			uint32_t value = (uint32_t) msg[0] << 16 | (uint32_t) msg[1] << 8 | (uint32_t) msg[2];
 			uart_puthex( value );

 			int8_t xDir = msg[0];
 			int8_t yDir = msg[1];
 			uart_print("(");
 			uart_putsdecimal(xDir);
 			uart_print("/");
 			uart_putsdecimal(yDir);
 			uart_print(")");

 			// TODO: Fix sender to not send values > 100
 			if ( xDir < -100 ) {
 				xDir = -100;
 			}
 			if ( yDir < -100 ) {
 				yDir = -100;
 			}

 			if ( yDir == 0 ) {
 				if ( xDir == 0 ) {
 					// full stop
 					motor_change(STOP,STOP, 0, 0);
 				} else if ( xDir < 0 ) {
 					// turn left in-place (left motor backwards, right motor forwards)
 					motor_change(BACKWARD,FORWARD, -xDir, -xDir );
 				} else {
 					// turn right in-place (left motor forwards, right motor backwards)
 					motor_change(FORWARD,BACKWARD, xDir, xDir ); 					
 				}
 			} else if ( xDir == 0 ) {
 				// yDir != 0 , xDir == 0 
 				if ( yDir > 0 ) {
 					// forwards		
					motor_change(FORWARD,FORWARD, yDir, yDir ); 								
 				} else {
 					// backwards
					motor_change(BACKWARD,BACKWARD, -yDir, -yDir );  					
 				}
 			} else {
 				// yDir != 0 , xDir != 0 
 				enum direction dir = yDir > 0 ? FORWARD : BACKWARD;
 				if ( xDir < 0 ) {
 					// turn left while driving forward -> left motor must run SLOWER than right motor
 					float delta = 1.0 - MAX_MOTOR_SPEED_DIFF*(-xDir/100.0);
					motor_change(dir, dir, yDir*delta , yDir ); 
 				}  else {
 					// turn right while driving forward -> right motor must run SLOWER than left motor
					float delta = 1.0 - MAX_MOTOR_SPEED_DIFF*(xDir/100.0); 						
					motor_change(dir, dir, yDir, yDir*delta ); 						
 				}																
 			}
 		}
 	}

 	// while( 1 ) {	
 	// 	if ( is_moving ) {
 	// 		PORTC = (PORTC & ~_BV(2)) | _BV(3);
 	// 		_delay_ms(250);
  //            PORTC = (PORTC & ~_BV(3)) | _BV(2);			
  //            _delay_ms(250);
 	// 	} else {
 	// 		PORTC = 0;
 	// 		while ( ! is_moving ) {				
 	// 		}
 	// 	}
 	// }
}
