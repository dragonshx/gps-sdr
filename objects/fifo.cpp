/*! \file FIFO.cpp
	Implements member functions of FIFO class.
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

#include "fifo.h"

/*----------------------------------------------------------------------------------------------*/
void *FIFO_Thread(void *_arg)
{

	FIFO *aFIFO = pFIFO;

	aFIFO->SetPid();
	aFIFO->Open();

	while(grun)
	{
		aFIFO->Import();
		aFIFO->IncExecTic();
	}

	pthread_exit(0);

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void FIFO::Start()
{

	Start_Thread(FIFO_Thread, NULL);

	if(gopt.verbose)
		printf("FIFO thread started\n");
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void FIFO::SetScale(int32 _agc_scale)
{
	agc_scale = _agc_scale;
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
FIFO::FIFO()
{
	int32 lcv;

	/* Create the buffer */
	buff = new ms_packet[FIFO_DEPTH];

	memset(buff, 0x0, sizeof(ms_packet)*FIFO_DEPTH);

	head = &buff[0];
	tail = &buff[0];

	for(lcv = 0; lcv < FIFO_DEPTH-1; lcv++)
		buff[lcv].next = &buff[lcv+1];

	buff[FIFO_DEPTH-1].next = &buff[0];

	/* Buffer for the raw IF data */
	if_buff = new CPX[IF_SAMPS_MS];

	/* Make pipe write non-blocking, this is to prevent the USRP from overflowing,
	 * which hoses the data steam. It is up to the CLIENT to make sure it is
	 * receiving continguous data packets */
	//fcntl(npipe, F_SETFL, O_NONBLOCK);

	tic = overflw = count = 0;

	//agc_scale = 1 << AGC_BITS;
	agc_scale = 2048;

	for(lcv = 0; lcv < MAX_CHANNELS; lcv++)
	{
		pthread_mutex_init(&chan_mutex[lcv], NULL);
		pthread_mutex_unlock(&chan_mutex[lcv]);
	}

	if(gopt.verbose)
		printf("Creating FIFO\n");

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
FIFO::~FIFO()
{
	int32 lcv;

	for(lcv = 0; lcv < MAX_CHANNELS; lcv++)
	{
		pthread_mutex_destroy(&chan_mutex[lcv]);
	}

	delete [] if_buff;
	delete [] buff;

	close(npipe);

	if(gopt.verbose)
		printf("Destructing FIFO\n");

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void kill_program(int _sig)
{
	grun = false;
	printf("Lost USRP-GPS!\n");
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void FIFO::Import()
{
	int32 lcv;
	char *p;
	int32 nbytes, bread, bytes_per_read, agc_scale_p = agc_scale;

	bytes_per_read = IF_SAMPS_MS*sizeof(CPX);

	/* Get data from pipe (1 ms) */
	nbytes = 0; p = (char *)&if_buff[0];
	while((nbytes < bytes_per_read) && grun)
	{
		//signal(SIGPIPE, kill_program); /* This only matters for a pipe WRITER */
		bread = read(npipe, &p[nbytes], PIPE_BUF);
		if(bread >= 0)
			nbytes += bread;
	}

	IncStartTic();

	/* Add to the buff */
	if(count == 0)
	{
		init_agc(&if_buff[0], IF_SAMPS_MS, AGC_BITS, &agc_scale);
	}
	else if(count < 1000)
	{
		overflw = run_agc(&if_buff[0], IF_SAMPS_MS, AGC_BITS, &agc_scale);
	}
	else
	{
		overflw = run_agc(&if_buff[0], IF_SAMPS_MS, AGC_BITS, &agc_scale);
		Enqueue();
	}

	IncStopTic();

	/* Resample? */
	count++;
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void FIFO::Enqueue()
{

	Lock();

	int32 lcv;
	ms_packet *p;

	if(head->next == tail)
	{
		overflw++;
		if((overflw % 1000) == 0)
			printf("FIFO overflow!\n");
	}
	else
	{
		memcpy(&head->data[0], &if_buff[0], SAMPS_MS*sizeof(CPX));
		head->count = count;

		/* Actual measurement rate needs to be double to properly calculate ICP */
		if((count % (MEASUREMENT_INT)) == 0)
		{
			tic++;
			head->measurement = tic;
		}
		else
			head->measurement = 0;

		for(lcv = 0; lcv < (MAX_CHANNELS+1); lcv++)
			head->accessed[lcv] = 666;

		head = head->next;
	}

	Unlock();

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void FIFO::Dequeue(int32 _resource, ms_packet *p)
{
	int32 lcv, complete;

	Lock();

	if(tail->accessed[_resource] = 666)
	{
		memcpy(p, tail, sizeof(ms_packet));
		tail->accessed[_resource] = 333;
	}

	complete = 1;

	/* Check to see if all correlators have gotten the data */
	for(lcv = 0; lcv < MAX_CHANNELS; lcv++)
		complete &= (tail->accessed[lcv] == 333);

	/* If the acquisition is active add one more to the count */
	pthread_mutex_lock(&mAcq);
	if(gAcq_high)
		complete &= (tail->accessed[MAX_CHANNELS] == 333);
	pthread_mutex_unlock(&mAcq);

	if(complete == 1)
	{
		for(lcv = 0; lcv < MAX_CHANNELS+1; lcv++)
			tail->accessed[lcv] = 0;

//		for(lcv = 0; lcv < MAX_CHANNELS; lcv++)
//			pthread_mutex_unlock(&chan_mutex[_resource]);

		if(tail->measurement)
		{
			telem.tic = tail->measurement;
			telem.count = count;
			telem.head = ((uint32)head - (uint32)&buff[0])/sizeof(ms_packet);
			telem.tail = ((uint32)tail - (uint32)&buff[0])/sizeof(ms_packet);
			telem.agc_scale = agc_scale;
			telem.overflw = overflw;

			write(FIFO_2_Telem_P[WRITE], &telem, sizeof(FIFO_M));
			write(FIFO_2_PVT_P[WRITE], &telem, sizeof(FIFO_M));

			tail->measurement = 0;
		}

		if(tail->next != head)
			tail = tail->next;
	}

	Unlock();

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void FIFO::Open()
{

	/* Open the USRP_Uno pipe to get IF data */
	if(gopt.verbose)
		printf("Opening GPS pipe.\n");

	npipe = open("/tmp/GPSPIPE", O_RDONLY);

	if(gopt.verbose)
		printf("GPS pipe open.\n");

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void FIFO::Wait(int32 _resource)
{
	pthread_mutex_lock(&chan_mutex[_resource]);
}
/*----------------------------------------------------------------------------------------------*/



