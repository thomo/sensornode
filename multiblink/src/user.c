/******************************************************************************/
/* Files to Include                                                           */
/******************************************************************************/

#include <xc.h>         /* XC8 General Include File */

#include <stdint.h>         /* For uint8_t definition */
#include <stdbool.h>        /* For true/false definition */

#include "user.h"

/******************************************************************************/
/* User Functions                                                             */
/******************************************************************************/


void ledOnWait(void) {
    __delay_ms(50);
}

void ledOffWait(void) {
    __delay_ms(400);
}

unsigned char onePinLow;

void InitApp(void) {
    
    // GP<2:0> are digital IO
    CMCON = 0x07;
            
    // GP<2:0> are outputs
    TRISIO = 0b00111000;
    GPIO = 0x07;
    
    onePinLow = 0b11111110;
}

void LoopApp(void) {
    if (onePinLow == 0b11110111) {
        onePinLow = 0b11111110;
    }
    
    GPIO = onePinLow;
    ledOnWait();
    GPIO = 0x07;
    ledOffWait();
    
    
    onePinLow *= 2;    
    onePinLow += 1;
}

