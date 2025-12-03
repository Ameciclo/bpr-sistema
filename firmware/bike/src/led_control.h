#ifndef LED_CONTROL_H
#define LED_CONTROL_H

extern unsigned long lastLedBlink;
extern int ledState;
extern int ledStep;

void updateLED();

#endif