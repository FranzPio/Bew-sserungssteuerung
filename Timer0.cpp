/*
 * Timer0.cpp
 *
 * Created: 30.04.2015 13:05:48
 * Author: Petre Sora (adapted: Franz Piontek)
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include "Timer0.h"


unsigned char ucTimer0_Flag_1ms = 0, ucTimer0_Flag_100ms = 0, ucTimer0_Flag_1s = 0, ucTimer0_Flag_500ms = 0, ucTimer0_Flag_200ms = 0;
unsigned char ucTimer0_Cnt_100ms = 0, ucTimer0_Cnt_1ms, ucTimer0_Cnt_200ms;
unsigned int uiTimer0_Cnt_1s = 0, uiTimer0_Cnt_500ms = 0;


void Timer0_Init(void)
{
  cli();
  
  TCCR0A = 0,								// Register von Timer 0 werden zurückgesetzt
  TCCR0B = 0;
  TCNT0 = 0;
  TCCR0A |= (1 << WGM01);					// CTC-Modus gewählt
  OCR0A = 249;								// Zeit auf 1 ms eingestellt
  TCCR0B |= ((1 << CS01) | (1 << CS00));	// Prescaler wird auf 64 gesetzt
  TIMSK0 |= (1 << OCIE0A);					// Timer 0 wird jetzt gestartet
  
  sei();									// Interrupts werden freigegeben
}


char Timer0_Get_100msState(void) // liefert zurück, ob 100 ms verstrichen sind
{
	if (ucTimer0_Flag_100ms == 1)
	{
		ucTimer0_Flag_100ms = 0;
		return TIMER_TRIGGERED;
	}
	else return TIMER_RUNNING;
}

//char Timer0_Get_200msState(void) // liefert zurück, ob 200 ms verstrichen sind
//{
//  if (ucTimer0_Flag_200ms == 1)
//  {
//    ucTimer0_Flag_200ms = 0;
//    return TIMER_TRIGGERED;
//  }
//  else return TIMER_RUNNING;
//}

//char Timer0_Get_500msState(void) // liefert zurück, ob 500 ms verstrichen sind
//{
//  if (ucTimer0_Flag_500ms == 1)
//  {
//    ucTimer0_Flag_500ms = 0;
//    return TIMER_TRIGGERED;
//  }
//  else return TIMER_RUNNING;
//}


char Timer0_Get_1sState(void) // liefert zurück, ob 1 s verstrichen ist
{
	if (ucTimer0_Flag_1s == 1)
	{
		ucTimer0_Flag_1s = 0;
		return TIMER_TRIGGERED;
	}
	else return TIMER_RUNNING;
}


ISR(TIMER0_COMPA_vect)
{
	ucTimer0_Cnt_100ms++;	
	if(ucTimer0_Cnt_100ms == 100)
	{
		ucTimer0_Flag_100ms = 1;
		ucTimer0_Cnt_100ms = 0;
	}

// ucTimer0_Cnt_200ms++; 
//  if(ucTimer0_Cnt_200ms == 200)
//  {
//    ucTimer0_Flag_200ms = 1;
//    ucTimer0_Cnt_200ms = 0;
//  }

// uiTimer0_Cnt_500ms++; 
//  if(uiTimer0_Cnt_500ms == 500)
//  {
//    ucTimer0_Flag_500ms = 1;
//    uiTimer0_Cnt_500ms = 0;
//  }
	
	uiTimer0_Cnt_1s++;
	if(uiTimer0_Cnt_1s == 1000)
	{
		uiTimer0_Cnt_1s = 0;
		ucTimer0_Flag_1s = 1;
	}
}
