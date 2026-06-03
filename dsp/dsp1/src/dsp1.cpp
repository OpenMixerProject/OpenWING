/*


     OOOOOOOOO                                                           WWWWWWWW                           WWWWWWWWIIIIIIIIIINNNNNNNN        NNNNNNNN        GGGGGGGGGGGGG
   OO:::::::::OO                                                         W::::::W                           W::::::WI::::::::IN:::::::N       N::::::N     GGG::::::::::::G
 OO:::::::::::::OO                                                       W::::::W                           W::::::WI::::::::IN::::::::N      N::::::N   GG:::::::::::::::G
O:::::::OOO:::::::O                                                      W::::::W                           W::::::WII::::::IIN:::::::::N     N::::::N  G:::::GGGGGGGG::::G
O::::::O   O::::::Oppppp   ppppppppp       eeeeeeeeeeee    nnnn  nnnnnnnn W:::::W           WWWWW           W:::::W   I::::I  N::::::::::N    N::::::N G:::::G       GGGGGG
O:::::O     O:::::Op::::ppp:::::::::p    ee::::::::::::ee  n:::nn::::::::nnW:::::W         W:::::W         W:::::W    I::::I  N:::::::::::N   N::::::NG:::::G
O:::::O     O:::::Op:::::::::::::::::p  e::::::eeeee:::::een::::::::::::::nnW:::::W       W:::::::W       W:::::W     I::::I  N:::::::N::::N  N::::::NG:::::G
O:::::O     O:::::Opp::::::ppppp::::::pe::::::e     e:::::enn:::::::::::::::nW:::::W     W:::::::::W     W:::::W      I::::I  N::::::N N::::N N::::::NG:::::G    GGGGGGGGGG
O:::::O     O:::::O p:::::p     p:::::pe:::::::eeeee::::::e  n:::::nnnn:::::n W:::::W   W:::::W:::::W   W:::::W       I::::I  N::::::N  N::::N:::::::NG:::::G    G::::::::G
O:::::O     O:::::O p:::::p     p:::::pe:::::::::::::::::e   n::::n    n::::n  W:::::W W:::::W W:::::W W:::::W        I::::I  N::::::N   N:::::::::::NG:::::G    GGGGG::::G
O:::::O     O:::::O p:::::p     p:::::pe::::::eeeeeeeeeee    n::::n    n::::n   W:::::W:::::W   W:::::W:::::W         I::::I  N::::::N    N::::::::::NG:::::G        G::::G
O::::::O   O::::::O p:::::p    p::::::pe:::::::e             n::::n    n::::n    W:::::::::W     W:::::::::W          I::::I  N::::::N     N:::::::::N G:::::G       G::::G
O:::::::OOO:::::::O p:::::ppppp:::::::pe::::::::e            n::::n    n::::n     W:::::::W       W:::::::W         II::::::IIN::::::N      N::::::::N  G:::::GGGGGGGG::::G
 OO:::::::::::::OO  p::::::::::::::::p  e::::::::eeeeeeee    n::::n    n::::n      W:::::W         W:::::W          I::::::::IN::::::N       N:::::::N   GG:::::::::::::::G
   OO:::::::::OO    p::::::::::::::pp    ee:::::::::::::e    n::::n    n::::n       W:::W           W:::W           I::::::::IN::::::N        N::::::N     GGG::::::GGG:::G
     OOOOOOOOO      p::::::pppppppp        eeeeeeeeeeeeee    nnnnnn    nnnnnn        WWW             WWW            IIIIIIIIIINNNNNNNN         NNNNNNN        GGGGGG   GGGG
                    p:::::p
                    p:::::p
                   p:::::::p
                   p:::::::p
                   p:::::::p
                   ppppppppp

  ControlSystem for DSP1 v0.0.1, 03.06.2026

  OpenWING - The OpenSource Operating System for the Behringer WING Audio Mixing Console
  Copyright 2026 OpenMixerProject
  https://github.com/OpenMixerProject/OpenWING

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  version 3 as published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
*/

#include "dsp1.h"

static volatile uint32_t timerCounter;

static void timerIsr(uint32_t iid, void* handlerArg) {
	volatile uint32_t *timer_counter = (uint32_t *) handlerArg;
	// Don't call standard I/O functions or update non-volatile global data in interrupt handlers

	// You can use the handler arguments to identify the interrupt being handled (iid)
	// and to access data via an interrupt specific callback pointer argument (handlerArg)
	assert(iid == ADI_CID_TMZHI || iid == ADI_CID_TMZLI);
	assert(handlerArg != NULL);

	// increment timer-counter up to 100000 and wrap around
	if (*timer_counter >= 100000) {
		*timer_counter = 0;
	}else{
		*timer_counter += 1;
	}
}

void delay(int i) {
    for (; i > 0; --i) {
    	NOP();
    }
}

void timerInit() {
	adi_int_InstallHandler(ADI_CID_TMZHI,        	// iid - high priority core timer. Use "ADI_CID_TMZLI" for low priority
							timerIsr,        		// handler
							(void *)&timerCounter,	// handler parameter
							true					// enable this timer
	);
	timer_set(1000, 1000); // set period to 1000 and counter to 1000 (= 1us)
	timer_on(); // start timer
}

int main()
{
	// initialize all components
	adi_initComponents();
	systemPllInit();
	systemSruInit();
	timerInit();
	
	while(1) {
		if (timerCounter == 0) {
			// toggle LED
			sysreg_bit_tgl(sysreg_FLAGS, FLG7); // alternative: sysreg_bit_clr() / sysreg_bit_set()
		}
	}

}

