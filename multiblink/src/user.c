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
    __delay_ms(10);
}

void ledOffWait(void) {
    __delay_ms(15 * 1000 / 4);
}

unsigned char onePinHigh;

void InitApp(void) {
    
    // GP<2:0> are digital
    CMCON = 0x07;
            
    // GP<4,2:0> are outputs
    TRISIO = 0b00100000;
    GPIO = 0x00;
    
    onePinHigh = 0b00000001;
}

void LoopApp(void) {
    if (onePinHigh == 0b00001000) {
       onePinHigh *= 2;    
    }
    
    if (onePinHigh == 0b00100000) {
        onePinHigh = 0b00000001;
    }
    
    GPIO = onePinHigh;
    ledOnWait();
    GPIO = 0x00;
    ledOffWait();
    
    
    onePinHigh *= 2;    

    
}

