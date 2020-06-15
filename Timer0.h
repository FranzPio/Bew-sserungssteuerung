/*
 * Timer0.h
 *
 * Created: 30.04.2015 13:05:28
 * Author: Petre Sora (adapted: Franz Piontek)
 */ 


#ifndef TIMER1_H_
#define TIMER1_H_

void Timer0_Init(void);
char Timer0_Get_100msState(void);
//char Timer0_Get_200msState(void);
//char Timer0_Get_500msState(void);
char Timer0_Get_1sState(void);

#define TIMER_TRIGGERED   1
#define TIMER_RUNNING     0


#endif /* TIMER1_H_ */
