#ifndef SYSTEM_H
#define	SYSTEM_H

/* Microcontroller MIPs (FCY) */
#define _XTAL_FREQ        4000000L
#define FCY             _XTAL_FREQ/4

#ifdef	__cplusplus
extern "C" {
#endif /* __cplusplus */

/******************************************************************************/
/* System Function Prototypes                                                 */
/******************************************************************************/

/* Custom oscillator configuration functions, reset source evaluation
functions, and other non-peripheral microcontroller initialization functions
go here. */

void ConfigureOscillator(void); /* Handles clock switching/osc initialization */

#ifdef	__cplusplus
}
#endif /* __cplusplus */

#endif	/* SYSTEM_H */
