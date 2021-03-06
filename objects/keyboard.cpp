/*! \file Keyboard.cpp
	Implements member functions of Keyboard class.
*/
/************************************************************************************************
Copyright 2008 Gregory W Heckler

This file is part of the GPS Software Defined Radio (GPS-SDR)

The GPS-SDR is free software; you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The GPS-SDR is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along with GPS-SDR; if not,
write to the:

Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
************************************************************************************************/

#include "keyboard.h"

/*----------------------------------------------------------------------------------------------*/
void *Keyboard_Thread(void *_arg)
{

	Keyboard *aKeyboard = pKeyboard;
	int32 key;

	aKeyboard->SetPid();

	while(grun)
	{

		key = getchar();
		printf("%c",(char)key);

		if((char)key == 'Q')
		{
			grun = 0x0;
		}

		if(pTelemetry != NULL)
		{
			if((char)key == 'n') //Navigation
			{
				pTelemetry->Lock();
				pTelemetry->SetDisplay(0);
				pTelemetry->Unlock();
			}

			if((char)key == 'a') //Almanac & SV predict
			{
				pTelemetry->Lock();
				pTelemetry->SetDisplay(1);
				pTelemetry->Unlock();
			}

			if((char)key == 'h') //Acquisition history
			{
				pTelemetry->Lock();
				pTelemetry->SetDisplay(2);
				pTelemetry->Unlock();
			}
		}

		aKeyboard->IncExecTic();

	}

	pthread_exit(0);

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Keyboard::Start()
{
	Start_Thread(Keyboard_Thread, NULL);

	if(gopt.verbose)
		printf("Keyboard thread started\n");
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
Keyboard::Keyboard()
{
	if(gopt.verbose)
		printf("Creating Keyboard\n");
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
Keyboard::~Keyboard()
{
	if(gopt.verbose)
		printf("Destructing Keyboard\n");
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Keyboard::Import()
{

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Keyboard::Export()
{

}
/*----------------------------------------------------------------------------------------------*/


