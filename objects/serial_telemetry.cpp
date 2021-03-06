/*! \file serial_telemetry.cpp
	Implements member functions of Serial_Telemetry class.
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

#include "serial_telemetry.h"


/*----------------------------------------------------------------------------------------------*/
void lost_gui_pipe(int _sig)
{
	pSerial_Telemetry->SetPipe(false);
	printf("GUI disconnected\n");
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void *Serial_Telemetry_Thread(void *_arg)
{

	Serial_Telemetry *aSerial_Telemetry = pSerial_Telemetry;

	aSerial_Telemetry->SetPid();

	while(grun)
	{
		aSerial_Telemetry->Import();
		usleep(1000);
	}

	pthread_exit(0);

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::Start()
{
	/* With new priority specified */
	Start_Thread(Serial_Telemetry_Thread, NULL);

	if(gopt.verbose)
		printf("Serial_Telemetry thread started\n");
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
Serial_Telemetry::Serial_Telemetry(int32 _serial)
{

	execution_tic = start_tic = stop_tic = 0;
	spipe_open = npipe_open = 0;
	npipe[READ] = npipe[WRITE] = spipe = NULL;

	serial = _serial;

	memset(&stream[0], 0x0, LAST_M_ID*sizeof(int32));

	/* Communicate over the serial port */
	if(serial)
	{
		OpenSerial();
	}
	else /* Communicate over the named pipe */
	{
		/* Everything set, now create a disk thread & pipe, and do some recording! */
		fifo[WRITE] = mkfifo("/tmp/GPS2GUI", S_IRWXG | S_IRWXU | S_IRWXO);
		if ((fifo[WRITE] == -1) && (errno != EEXIST))
			printf("Error creating the named pipe");

		/* Everything set, now create a disk thread & pipe, and do some recording! */
		fifo[READ] = mkfifo("/tmp/GUI2GPS", S_IRWXG | S_IRWXU | S_IRWXO);
		if ((fifo[READ] == -1) && (errno != EEXIST))
			printf("Error creating the named pipe");

		OpenPipe();
	}

	if(gopt.verbose)
		printf("Creating Serial_Telemetry\n");


	signal(SIGPIPE, lost_gui_pipe);

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
Serial_Telemetry::~Serial_Telemetry()
{
	close(npipe[READ]);
	close(npipe[WRITE]);
	close(spipe);

	if(gopt.verbose)
		printf("Destroying Serial_Telemetry\n");
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::Import()
{
	Channel_M temp;
	int32 bread, lcv, num_chans;

	/* Pend on this pipe */
	bread = read(FIFO_2_Telem_P[READ], &fifo_status, sizeof(FIFO_M));
	if(bread == sizeof(FIFO_M))
	{

		/* Lock correlator status */
		for(lcv = 0; lcv < MAX_CHANNELS; lcv++)
		{
			pChannels[lcv]->Lock();
			if(pChannels[lcv]->getActive())
			{
				channel[lcv] = pChannels[lcv]->getPacket();
				active[lcv] = 1;
			}
			else
			{
				channel[lcv].count = 0;
				active[lcv] = 0;
			}
			pChannels[lcv]->Unlock();
		}


		/* Read from PVT */
		//read(PVT_2_Telem_P[READ], &tNav, sizeof(PVT_2_Telem_S));
		read(PVT_2_Telem_P[READ], &sps, 			sizeof(SPS_M));
		read(PVT_2_Telem_P[READ], &clock, 			sizeof(Clock_M));
		read(PVT_2_Telem_P[READ], &sv_positions[0],	MAX_CHANNELS*sizeof(SV_Position_M));
		read(PVT_2_Telem_P[READ], &pseudoranges[0],	MAX_CHANNELS*sizeof(Pseudorange_M));
		read(PVT_2_Telem_P[READ], &measurements[0],	MAX_CHANNELS*sizeof(Measurement_M));

		/* Read from actual acquisition */
		bread = sizeof(Acq_Command_M);
		while(bread == sizeof(Acq_Command_M))
			bread = read(Acq_2_Telem_P[READ],&acq_command, sizeof(Acq_Command_M));

		/* Read from Ephemeris */
		bread = sizeof(Ephemeris_Status_M);
		while(bread == sizeof(Ephemeris_Status_M))
			bread = read(Ephem_2_Telem_P[READ], &ephemeris_status, sizeof(Ephemeris_Status_M));

		/* Read from SV Select */
		bread = sizeof(SV_Select_2_Telem_S);
		while(bread == sizeof(SV_Select_2_Telem_S))
			bread = read(SV_Select_2_Telem_P[READ], &tSelect, sizeof(SV_Select_2_Telem_S));

		IncExecTic();

		Export();

	}

	/* See if any commands have been sent over */
	if(serial)
	{
		if(spipe_open)
			ImportSerial();
	}
	else
	{
		if(npipe_open)
			ImportPipe();
	}

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::Export()
{

	Lock();

	IncStopTic();

	if((execution_tic % gopt.log_decimate) == 0)
	{
		if(serial)
		{
			if(spipe_open)
				SendMessages();
			else
				OpenSerial();
		}
		else
		{
			if(npipe_open)
				SendMessages();
			else
				OpenPipe();
		}
	}

	IncStartTic();

	Unlock();
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::SetPipe(bool _status)
{
	npipe_open = _status;
	if(_status == false)
	{
		close(npipe[READ]);
		close(npipe[WRITE]);
		npipe[READ] = NULL;
		npipe[WRITE] = NULL;
	}
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::SendMessages()
{

	/* Now transmit the normal once/pvt stuff */
	SendFIFO();
	SendTaskHealth();
	SendSPS();
	SendClock();

	/* If the streaming stuff is on/off emit that */
	if(stream[SV_POSITION_M_ID])
		SendSVPositions();

	if(stream[PSEUDORANGE_M_ID])
		SendPseudoranges();

	if(stream[MEASUREMENT_M_ID])
		SendMeasurements();

	if(stream[CHANNEL_M_ID])
		SendChannelHealth();

	/* See if there is any new GEONS data */


	/* If so write out that message */
	//SendEKF();

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::ImportPipe()
{

	uint32 preamble;
	int32 bread;
	int32 bwrote;

	preamble = 0x0;

	bread = 1;
	while(bread > 0)
	{
		bread = read(npipe[READ], &preamble, sizeof(uint32));
		if(bread == sizeof(uint32))
		{
			if(preamble == 0xAAAAAAAA)
			{
				if(npipe_open)
					bread = read(npipe[READ], &command_header, sizeof(CCSDS_Packet_Header)); 	//!< Read in the head
				else
					return;

				DecodeCCSDSPacketHeader(&decoded_header, &command_header);						//!< Decode the packet

				if(npipe_open)
					bread = read(npipe[READ], &command_body, decoded_header.length);			//!< Read in the body
				else
					return;

				/* Forward the command to Commando */
				bwrote = write(Telem_2_Cmd_P[WRITE], &command_header, sizeof(CCSDS_Packet_Header));
				bwrote = write(Telem_2_Cmd_P[WRITE], &command_body, decoded_header.length);
			}
		}
	}

	/* Bent pipe anything coming from Commando */
	if(npipe_open)
	{
		bread = read(Cmd_2_Telem_P[READ], &commando_buff, COMMANDO_BUFF_SIZE);
		if(bread > 0)
		{
			if(npipe_open)
				write(npipe[WRITE], &commando_buff, bread);


		}
	}

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::OpenPipe()
{

	npipe[READ] = npipe[WRITE] = -1;
	npipe[READ] = open("/tmp/GUI2GPS", O_RDONLY | O_NONBLOCK);
	npipe[WRITE] = open("/tmp/GPS2GUI", O_WRONLY | O_NONBLOCK);

	if((npipe[READ] != -1) && (npipe[WRITE] != -1))
	{
		npipe_open = true;
		printf("GUI connected\n");
	}
	else
	{
		close(npipe[READ]);
		close(npipe[WRITE]);
		npipe[READ] = NULL;
		npipe[WRITE] = NULL;
		npipe_open = false;
	}

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::ImportSerial()
{

	uint32 preamble;
	int32 bread;
	int32 bwrote;

	preamble = 0x0;

	bread = 1;
	while(bread > 0)
	{
		bread = read(spipe, &preamble, sizeof(uint32));
		if(bread == sizeof(uint32))
		{
			if(preamble == 0xAAAAAAAA)
			{
				if(spipe_open)
					bread = read(spipe, &command_header, sizeof(CCSDS_Packet_Header));	//!< Read in the head
				else
					return;

				DecodeCCSDSPacketHeader(&decoded_header, &command_header);			//!< Decode the packet

				if(spipe_open)
					bread = read(spipe, &command_body, decoded_header.length);			//!< Read in the body
				else
					return;

				/* Forward the command to Commando */
				bwrote = write(Telem_2_Cmd_P[WRITE], &command_header, sizeof(CCSDS_Packet_Header));
				bwrote = write(Telem_2_Cmd_P[WRITE], &command_body, decoded_header.length);
			}
		}
	}

	/* Bent pipe anything coming from Commando */
	if(spipe_open)
	{
		bread = read(Cmd_2_Telem_P[READ], &commando_buff, COMMANDO_BUFF_SIZE);
		if(bread > 0)
		{
			if(spipe_open)
				write(spipe, &commando_buff, bread);
		}
	}
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::OpenSerial()
{

	spipe = open("/dev/ttyS0", O_RDWR | O_NOCTTY | O_NONBLOCK);
    if(spipe < 0)
    {
    	spipe_open = false;
    	spipe = NULL;
    	return;
    }

    memset(&tty, 0x0, sizeof(tty));		//!< Initialize the port settings structure to all zeros
    tty.c_cflag =  B115200 | CS8 | CLOCAL | CREAD | CRTSCTS;	//!< 8N1
    tty.c_iflag = IGNPAR;
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tty.c_cc[VMIN] = 0;      			//!< 0 means use-vtime
    tty.c_cc[VTIME] = 1;      			//!< Time to wait until exiting read (tenths of a second)

    tcflush(spipe, TCIFLUSH);				//!< flush old data
    tcsetattr(spipe, TCSANOW, &tty);		//!< apply new settings
    fcntl(spipe, F_SETFL, FASYNC);
	fcntl(spipe, F_SETFL, O_NONBLOCK);
	spipe_open = true;

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::SendBoardHealth()
{

	/* Form the packet header */
	FormCCSDSPacketHeader(&packet_header, BOARD_HEALTH_M_ID, 0, sizeof(Board_Health_M), 0, packet_tic++);

	/* Emit the packet */
	EmitCCSDSPacket((void *)&board_health, sizeof(Board_Health_M));

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::SendTaskHealth()
{

	uint32 lcv;

	/* Get execution counters */
	for(lcv = 0; lcv < CORRELATOR_TASK_ID; lcv++)
		task_health.execution_tic[lcv] 					= pCorrelators[lcv]->GetExecTic();
	//task_health.execution_tic[POST_PROCESS_TASK_ID]  	= pPost_Process->GetExecTic();
	task_health.execution_tic[FIFO_TASK_ID]  			= pFIFO->GetExecTic();
	task_health.execution_tic[COMMANDO_TASK_ID]  		= pCommando->GetExecTic();
	//task_health.execution_tic[TELEMETRY_TASK_ID]  	= pTelemetry->GetExecTic();
	task_health.execution_tic[SERIAL_TELEMETRY_TASK_ID] = pSerial_Telemetry->GetExecTic();
	task_health.execution_tic[KEYBOARD_TASK_ID]  		= pKeyboard->GetExecTic();
	task_health.execution_tic[EPHEMERIS_TASK_ID]  		= pEphemeris->GetExecTic();
	task_health.execution_tic[SV_SELECT_TASK_ID]  		= pSV_Select->GetExecTic();
	task_health.execution_tic[ACQUISITION_TASK_ID]  	= pAcquisition->GetExecTic();
	task_health.execution_tic[PVT_TASK_ID]  			= pPVT->GetExecTic();
	//task_health.execution_tic[EKF_TASK_ID]  			= pEKF->GetExecTic();

	/* Get execution counters */
	for(lcv = 0; lcv < CORRELATOR_TASK_ID; lcv++)
		task_health.start_tic[lcv] 						= pCorrelators[lcv]->GetStartTic();
	//task_health.start_tic[POST_PROCESS_TASK_ID]  		= pPost_Process->GetStartTic();
	task_health.start_tic[FIFO_TASK_ID]  				= pFIFO->GetStartTic();
	task_health.start_tic[COMMANDO_TASK_ID]  			= pCommando->GetStartTic();
	//task_health.start_tic[TELEMETRY_TASK_ID]  		= pTelemetry->GetStartTic();
	task_health.start_tic[SERIAL_TELEMETRY_TASK_ID] 	= pSerial_Telemetry->GetStartTic();
	task_health.start_tic[KEYBOARD_TASK_ID]  			= pKeyboard->GetStartTic();
	task_health.start_tic[EPHEMERIS_TASK_ID]  			= pEphemeris->GetStartTic();
	task_health.start_tic[SV_SELECT_TASK_ID]  			= pSV_Select->GetStartTic();
	task_health.start_tic[ACQUISITION_TASK_ID]  			= pAcquisition->GetStartTic();
	task_health.start_tic[PVT_TASK_ID]  				= pPVT->GetStartTic();
	//task_health.start_tic[EKF_TASK_ID]  				= pEKF->GetStartTic();

	/* Get execution counters */
	for(lcv = 0; lcv < CORRELATOR_TASK_ID; lcv++)
		task_health.stop_tic[lcv]						= pCorrelators[lcv]->GetStopTic();
	//task_health.stop_tic[POST_PROCESS_TASK_ID]  		= pPost_Process->GetStopTic();
	task_health.stop_tic[FIFO_TASK_ID]  				= pFIFO->GetStopTic();
	task_health.stop_tic[COMMANDO_TASK_ID]  			= pCommando->GetStopTic();
	//task_health.stop_tic[TELEMETRY_TASK_ID]  			= pTelemetry->GetStopTic();
	task_health.stop_tic[SERIAL_TELEMETRY_TASK_ID]		= pSerial_Telemetry->GetStopTic();
	task_health.stop_tic[KEYBOARD_TASK_ID]  			= pKeyboard->GetStopTic();
	task_health.stop_tic[EPHEMERIS_TASK_ID]  			= pEphemeris->GetStopTic();
	task_health.stop_tic[SV_SELECT_TASK_ID]  			= pSV_Select->GetStopTic();
	task_health.stop_tic[ACQUISITION_TASK_ID]  			= pAcquisition->GetStopTic();
	task_health.stop_tic[PVT_TASK_ID]  					= pPVT->GetStopTic();
	//task_health.stop_tic[EKF_TASK_ID]  				= pEKF->GetStopTic();

	/* Form the packet header */
	FormCCSDSPacketHeader(&packet_header, TASK_HEALTH_M_ID, 0, sizeof(Task_Health_M), 0, packet_tic++);

	/* Emit the packet */
	EmitCCSDSPacket((void *)&task_health, sizeof(Task_Health_M));

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::SendChannelHealth()
{

	int32 lcv;

	for(lcv = 0; lcv < MAX_CHANNELS; lcv++)
	{

		/* Form the packet */
		FormCCSDSPacketHeader(&packet_header, CHANNEL_M_ID, 0, sizeof(Channel_M), 0, packet_tic++);

		/* Emit the packet */
		channel[lcv].chan = lcv;
		EmitCCSDSPacket((void *)&channel[lcv], sizeof(Channel_M));
	}

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::SendFIFO()
{

	/* Form the packet header */
	FormCCSDSPacketHeader(&packet_header, FIFO_M_ID, 0, sizeof(FIFO_M), 0, packet_tic++);

	/* Emit the packet */
	EmitCCSDSPacket((void *)&fifo_status, sizeof(FIFO_M));

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::SendSPS()
{

	/* Form the packet header */
	FormCCSDSPacketHeader(&packet_header, SPS_M_ID, 0, sizeof(SPS_M), 0, packet_tic++);

	/* Emit the packet */
	EmitCCSDSPacket((void *)&sps, sizeof(SPS_M));

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::SendClock()
{

	/* Form the packet header */
	FormCCSDSPacketHeader(&packet_header, CLOCK_M_ID, 0, sizeof(Clock_M), 0, packet_tic++);

	/* Emit the packet */
	EmitCCSDSPacket((void *)&clock, sizeof(Clock_M));

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::SendSVPositions()
{
	int32 lcv;

	for(lcv = 0; lcv < MAX_CHANNELS; lcv++)
	{

		/* Form the packet */
		FormCCSDSPacketHeader(&packet_header, SV_POSITION_M_ID, 0, sizeof(SV_Position_M), 0, packet_tic++);

		/* Emit the packet */
		sv_positions[lcv].chan = lcv;
		EmitCCSDSPacket((void *)&sv_positions[lcv], sizeof(SV_Position_M));
	}
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::SendPseudoranges()
{

	int32 lcv;

	for(lcv = 0; lcv < MAX_CHANNELS; lcv++)
	{

		/* Form the packet */
		FormCCSDSPacketHeader(&packet_header, PSEUDORANGE_M_ID, 0, sizeof(Pseudorange_M), 0, packet_tic++);

		/* Emit the packet */
		pseudoranges[lcv].chan = lcv;
		EmitCCSDSPacket((void *)&pseudoranges[lcv], sizeof(Pseudorange_M));
	}

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::SendMeasurements()
{

	int32 lcv;

	for(lcv = 0; lcv < MAX_CHANNELS; lcv++)
	{

		/* Form the packet */
		FormCCSDSPacketHeader(&packet_header, MEASUREMENT_M_ID, 0, sizeof(Measurement_M), 0, packet_tic++);

		/* Emit the packet */
		measurements[lcv].chan = lcv;
		EmitCCSDSPacket((void *)&measurements[lcv], sizeof(Measurement_M));
	}

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::SendEphemerisStatus()
{
	/* Form the packet */
	FormCCSDSPacketHeader(&packet_header, EPHEMERIS_VALID_M_ID, 0, sizeof(Ephemeris_Status_M), 0, packet_tic++);

	/* Emit the packet */
	EmitCCSDSPacket((void *)&ephemeris_status, sizeof(Ephemeris_Status_M));
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::SendSVPrediction()
{
	/* Form the packet */
	FormCCSDSPacketHeader(&packet_header, SV_PREDICTION_M_ID, 0, sizeof(SV_Prediction_M), 0, packet_tic++);

	/* Emit the packet */
	EmitCCSDSPacket((void *)&tSelect.sv_predicted[(execution_tic % NUM_CODES)], sizeof(SV_Prediction_M));
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::SendAcqCommand()
{
	/* Form the packet */
	FormCCSDSPacketHeader(&packet_header, ACQ_COMMAND_M_ID, 0, sizeof(Acq_Command_M), 0, packet_tic++);

	/* Emit the packet */
	EmitCCSDSPacket((void *)&acq_command, sizeof(Acq_Command_M));
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Serial_Telemetry::EmitCCSDSPacket(void *_buff, uint32 _len)
{

	uint32 lcv, bwrote, preamble;
	uint8 *sbuff;

	preamble = 0xAAAAAAAA;

	if(serial)
	{
		if(spipe_open)
			write(spipe, &preamble, sizeof(uint32));  					//!< Stuff the preamble

		if(spipe_open)
			write(spipe, &packet_header, sizeof(CCSDS_Packet_Header)); 	//!< Stuff the CCSDS header

		if(spipe_open)
			write(spipe, _buff, _len);									//!< Stuff the body
	}
	else
	{
		if(npipe_open)
			write(npipe[WRITE], &preamble, sizeof(uint32));  					//!< Stuff the preamble

		if(npipe_open)
			write(npipe[WRITE], &packet_header, sizeof(CCSDS_Packet_Header)); 	//!< Stuff the CCSDS header

		if(npipe_open)
			write(npipe[WRITE], _buff, _len);									//!< Stuff the body
	}

}
/*----------------------------------------------------------------------------------------------*/

