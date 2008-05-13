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

#include "includes.h"
#include "prn_codes.h"	//!< Include the pre-fftd PRN codes (done with MATLAB)

//#define ACQ_DEBUG

/*----------------------------------------------------------------------------------------------*/
void *Acquisition_Thread(void *_arg)
{
	
	Acquisition *aAcquisition = pAcquisition;

	while(grun)
	{
		aAcquisition->Inport();
		aAcquisition->Acquire();
		aAcquisition->Export(NULL);
		aAcquisition->UpdateState();
	}
	
	pthread_exit(0);
	
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Acquisition::Start()
{
	pthread_attr_t tattr;
	pthread_t tid;
	int32 ret;
	sched_param param;
	
	/* Unitialized with default attributes */
	ret = pthread_attr_init (&tattr);
	
	/*Ssafe to get existing scheduling param */
	ret = pthread_attr_getschedparam (&tattr, &param);
	
	/* Set the priority; others are unchanged */
	param.sched_priority = ACQ_PRIORITY;
	
	/* Setting the new scheduling param */
	ret = pthread_attr_setschedparam(&tattr, &param);
	ret = pthread_attr_setschedpolicy(&tattr, SCHED_FIFO);
	
	/* With new priority specified */
	pthread_create(&thread, NULL, Acquisition_Thread, NULL);
	
	if(gopt.verbose)	
		printf("Acquisition thread started\n");	
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
void Acquisition::Stop()
{
	pthread_join(thread, NULL);
	
	if(gopt.verbose)
		printf("Acquisition thread stopped\n");
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
/*! 
 * Acquisition(): Constructor
 * */
Acquisition::Acquisition(float _fsample, float _fif)
{

	int32 lcv, lcv2;
	CPX *p;
	int32 R1[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	int32 R2[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};

	/* Acq state */
	sv = 0;
	state = ACQ_STRONG;

	/* Grab some constants */
	fif = _fif;
	fbase = SAMPLE_FREQUENCY;
	fsample = _fsample; 
	samps_ms = (int32)ceil(fsample/1000.0);

	/* Resample to the nearest 2^N */
	resamps_ms = SAMPS_MS; //Always decimate to 2048 samples/ms for C/A code
	
	/* Step one, grab pre-fftd codes from header file */
	for(lcv = 0; lcv < NUM_CODES; lcv++)
		fft_codes[lcv] = (CPX *)&PRN_Codes[2*lcv*resamps_ms];
		
	/* Allocate some buffers that will be used later on */
	buff	 = new CPX[310 * resamps_ms];
	rotate   = new CPX[resamps_ms];
	msbuff   = new CPX[resamps_ms];
	power    = new CPX[10 * resamps_ms];	
	coherent = new CPX[10 * resamps_ms];
	wipeoff  = new CPX[310 * resamps_ms];
	baseband = new CPX[4 * 310 * resamps_ms];
	_250Hzwipeoff = new CPX[310 * resamps_ms];
	_500Hzwipeoff = new CPX[310 * resamps_ms];
	_750Hzwipeoff = new CPX[310 * resamps_ms];

	/* Allocate baseband shift vector and map of the row pointers */
	baseband_shift = new CPX[4 * 310 * (resamps_ms+201)];
	baseband_rows = new CPX *[1240];
	for(lcv = 0; lcv < 1240; lcv++)
		baseband_rows[lcv] = &baseband_shift[lcv*(resamps_ms+201)]; 
	
	/* Allocate baseband shift vector and map of the row pointers */
	dft = new MIX[10*10];
	dft_rows = new MIX *[10];
	for(lcv = 0; lcv < 10; lcv++)
		dft_rows[lcv] = &dft[lcv*10]; 
	
	/* Generate sinusoid */
	for(lcv = 0; lcv < 10; lcv++)
		wipeoff_gen(dft_rows[lcv], (float)lcv*-25.0, 1000.0, 10);
	
	/* Generate mix to baseband */
	sine_gen(wipeoff, -fif, SAMPLE_FREQUENCY, 310*resamps_ms);
	 
	/* Generate 250 Hz offset wipeoff */
	sine_gen(_250Hzwipeoff, -250.0, SAMPLE_FREQUENCY, 310*resamps_ms);
	sine_gen(_500Hzwipeoff, -500.0, SAMPLE_FREQUENCY, 310*resamps_ms);
	sine_gen(_750Hzwipeoff, -750.0, SAMPLE_FREQUENCY, 310*resamps_ms);
	
	/* Allocate the FFT */
	pFFT = new FFT(resamps_ms, R1);
	piFFT = new FFT(resamps_ms, R2);		
	pcFFT = new FFT(32);

	#ifdef ACQ_DEBUG
		lcv = mkfifo("/tmp/ACQPIPE", 0666);
		if((lcv == -1) && (errno != EEXIST))
	        printf("Error creating the named pipe");
	    else
	    	printf("Named pipe created\n");
	    	
		printf("Waiting for client\n");
		acq_pipe = open("/tmp/GPSPIPE", O_WRONLY);
		fcntl(acq_pipe, F_SETFL, O_NONBLOCK);		
		printf("Client connected\n");
	#endif		

	if(gopt.verbose)	
		printf("Creating Acquisition\n");

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
/*! 
 * ~Acquisition(): Deconstructor
 * */
Acquisition::~Acquisition()
{
	
	int32 lcv;

	delete pFFT;
	delete piFFT;
	delete pcFFT;
	
	delete [] buff;
	delete [] rotate;
	delete [] msbuff;
	delete [] wipeoff;
	delete [] baseband;
	delete [] baseband_shift;
	delete [] baseband_rows;
	delete [] coherent;
	delete [] power;
	delete [] dft;
	delete [] dft_rows;
	delete [] _250Hzwipeoff;
	delete [] _500Hzwipeoff;	
	delete [] _750Hzwipeoff;	
	
	#ifdef ACQ_DEBUG
		close(acq_pipe);
	#endif
	
	if(gopt.verbose)
		printf("Destructing Acquisition\n");	
	
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
/*! 
 * doPrepIF: Complete all of the upfront IF processing, iAGC_BITSncludes: reampling the buffer, mixiing to baseband, 25-Hz, 500 Hz, and 750 Hz, computing the forward FFT, then
 * copying the FFTd data into a 2-D matrix. The matrix is created in such a way to allow the circular-rotation trick to be carried out without repeatedly calling "doRotate"
 * */
void Acquisition::doPrepIF(int32 _type, CPX *_buff)
{
	
	int32 lcv, ms;
	CPX *p;
	int *pp;
	
	switch(_type)
	{
		case 0:
			ms = 1;
			break;
		case 1:
			ms = 20;
			break;
		case 2:
			ms = 310;
			break;
		default:
			ms = 1;
	}	
	
	/* 1) Import data */
	memcpy(baseband, _buff, ms*resamps_ms*sizeof(CPX));
	
	/* Mix down to baseband */
	sse_cmuls(baseband, wipeoff, ms*resamps_ms, 14);

	/* Do the 250 Hz offsets */
	sse_cmulsc(&baseband[0], _250Hzwipeoff, &baseband[ms*resamps_ms],   ms*resamps_ms, 14);
	sse_cmulsc(&baseband[0], _500Hzwipeoff, &baseband[2*ms*resamps_ms], ms*resamps_ms, 14);
	sse_cmulsc(&baseband[0], _750Hzwipeoff, &baseband[3*ms*resamps_ms], ms*resamps_ms, 14);
	
	/* Compute forward FFT of IF data */
	for(lcv = 0; lcv < 4*ms; lcv++)
		pFFT->doFFT(&baseband[lcv*resamps_ms], true);
		
	/* Now copy into the rows *//*----------------------------------------------------------------------------------------------*/
	for(lcv = 0; lcv < 4*ms; lcv++)
	{
		p = baseband_rows[lcv];
		memcpy(p, 				 &baseband[(lcv+1)*resamps_ms-100], 100*sizeof(CPX));
		memcpy(p+100,	 		 &baseband[lcv*resamps_ms],			resamps_ms*sizeof(CPX));
		memcpy(p+100+resamps_ms, &baseband[lcv*resamps_ms],			100*sizeof(CPX));
	}
				
}	
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
/*! 
 * doAcqStrong: Acquire using a 1 ms coherent integration
 * */		
Acq_Result_S Acquisition::doAcqStrong(int32 _sv, int32 _doppmin, int32 _doppmax)
{
	
	FILE *fp;
	int32 lcv, lcv2, mag, magt, index, indext;
	Acq_Result_S *result = &results[_sv];
	CPX *p;
	
	#ifdef ACQ_DEBUG
		mag = ACQ_PREFIX;
		write(acq_pipe,&mag,sizeof(int32));
		mag = ACQ_STRONG;
		write(acq_pipe,&mag,sizeof(int32));
		write(acq_pipe,&_sv,sizeof(int32));
	#endif			
	
	index = indext = mag = magt = 0;

	/* Covers the 250 Hz spacing */
	for(lcv = (_doppmin/1000); lcv <=  (_doppmax/1000); lcv++)
	{
		/* Sweep through the doppler range */
		for(lcv2 = 0; lcv2 < 4; lcv2++)
		{
			
			if(gopt.realtime)
				usleep(1000);
			
			/* Multiply in frequency domain, shifting appropiately */
			sse_cmulsc(&baseband_rows[lcv2][100-lcv], fft_codes[_sv], msbuff, resamps_ms, 4);
			
			/* Compute iFFT */
			piFFT->doiFFT(msbuff, true);

			/* Convert to a power */
			x86_cmag(msbuff, resamps_ms);
			
			#ifdef ACQ_DEBUG
				write(acq_pipe,msbuff,sizeof(int32)*resamps_ms);
			#endif			
			
			/* Find the maximum */
			x86_max((int32 *)msbuff, &indext, &magt, resamps_ms);
			
			/* Found a new maximum */
			if(magt > mag)
			{
				mag = magt;
				index = indext;
				result->delay = CODE_CHIPS - (float)index*CODE_RATE/fbase;
				result->doppler = (float)(-lcv*1000) + (float)lcv2*250;
				result->magnitude = (float)mag;
			}
			
		}
	}
	
	#ifdef ACQ_DEBUG
		mag = ACQ_POSTFIX;
		write(acq_pipe,&mag,sizeof(int32));
	#endif			
	
	result->sv = _sv;
	
	result->type = ACQ_STRONG;
	
	if(result->magnitude > THRESH_STRONG)
		result->success = 1;
	else
		result->success = 0;
	
	return(results[_sv]);
	
}
/*----------------------------------------------------------------------------------------------*/



void sse_dft(CPX *A, MIX *B, CPX *C)
{
	__asm 
	(
		".intel_syntax noprefix			\n\t" //Set up for loop
		"mov edi, [ebp+8]				\n\t" //Address of A
		"mov esi, [ebp+12]				\n\t" //Address of B
		"mov ebx, [ebp+16]				\n\t" //Address of C
		"movd mm0, [edi]				\n\t"
		"movd mm1, [edi+4]				\n\t"
		"movd mm2, [edi+8]				\n\t"					
		"movq mm3, [esi]				\n\t"
		"movq mm4, [esi+8]				\n\t"
		"movq mm5, [esi+16]				\n\t"
		"punpckldq mm0, mm0				\n\t" 
		"punpckldq mm1, mm1				\n\t"
		"punpckldq mm2, mm2				\n\t"
		"mov	ecx, 0x0				\n\t"
		"S%=:							\n\t"																			
			"pmaddwd mm0, mm3			\n\t"
			"pmaddwd mm1, mm4			\n\t"
			"pmaddwd mm2, mm5			\n\t"
			"paddd	mm0, mm7			\n\t"
			"paddd	mm1, mm7			\n\t"
			"paddd	mm2, mm7			\n\t"			
			"psrad	mm0, mm6			\n\t" //Back to original magnitude
			"psrad	mm1, mm6			\n\t" //Back to original magnitude
			"psrad	mm2, mm6			\n\t" //Back to original magnitude
			"movd	eax, mm0			\n\t" 
			"add	[ebx], eax			\n\t" //Add into accum
			"movd	eax, mm0			\n\t"
			"add	[ebx+4],eax			\n\t" //Add into accum
			"movd	eax, mm0			\n\t"
			"add	[ebx+8], eax		\n\t" //Add into accum
			"add	ecx, 0x1			\n\t"
			"cmp	ecx, 10				\n\t"
		"jne		S%=					\n\t"
		"movd mm0, [edi+12]				\n\t"
		"movd mm1, [edi+16]				\n\t"
		"movd mm2, [edi+20]				\n\t"					
		"movq mm3, [esi+24]				\n\t"
		"movq mm4, [esi+32]				\n\t"
		"movq mm5, [esi+40]				\n\t"
		"punpckldq mm0, mm0				\n\t" 
		"punpckldq mm1, mm1				\n\t"
		"punpckldq mm2, mm2				\n\t"
		"mov	ecx, 0x0				\n\t"
		"SS%=:							\n\t"																			
			"pmaddwd mm0, mm3			\n\t"
			"pmaddwd mm1, mm4			\n\t"
			"pmaddwd mm2, mm5			\n\t"
			"paddd	mm0, mm7			\n\t"
			"paddd	mm1, mm7			\n\t"
			"paddd	mm2, mm7			\n\t"			
			"psrad	mm0, mm6			\n\t" //Back to original magnitude
			"psrad	mm1, mm6			\n\t" //Back to original magnitude
			"psrad	mm2, mm6			\n\t" //Back to original magnitude
			"movd	eax, mm0			\n\t" 
			"add	[ebx+12], eax			\n\t" //Add into accum
			"movd	eax, mm0			\n\t"
			"add	[ebx+16],eax			\n\t" //Add into accum
			"movd	eax, mm0			\n\t"
			"add	[ebx+20], eax		\n\t" //Add into accum
			"add	ecx, 0x1			\n\t"
			"cmp	ecx, 10				\n\t"
		"jne		SS%=					\n\t"			
		"movd mm0, [edi+24]				\n\t"
		"movd mm1, [edi+28]				\n\t"
		"movd mm2, [edi+32]				\n\t"					
		"movq mm3, [esi+48]				\n\t"
		"movq mm4, [esi+56]				\n\t"
		"movq mm5, [esi+64]				\n\t"
		"punpckldq mm0, mm0				\n\t" 
		"punpckldq mm1, mm1				\n\t"
		"punpckldq mm2, mm2				\n\t"
		"mov	ecx, 0x0				\n\t"
		"SSS%=:							\n\t"																			
			"pmaddwd mm0, mm3			\n\t"
			"pmaddwd mm1, mm4			\n\t"
			"pmaddwd mm2, mm5			\n\t"
			"paddd	mm0, mm7			\n\t"
			"paddd	mm1, mm7			\n\t"
			"paddd	mm2, mm7			\n\t"			
			"psrad	mm0, mm6			\n\t" //Back to original magnitude
			"psrad	mm1, mm6			\n\t" //Back to original magnitude
			"psrad	mm2, mm6			\n\t" //Back to original magnitude
			"movd	eax, mm0			\n\t" 
			"add	[ebx+24], eax			\n\t" //Add into accum
			"movd	eax, mm0			\n\t"
			"add	[ebx+28],eax			\n\t" //Add into accum
			"movd	eax, mm0			\n\t"
			"add	[ebx+32], eax		\n\t" //Add into accum
			"add	ecx, 0x1			\n\t"
			"cmp	ecx, 10				\n\t"
		"jne		SSS%=					\n\t"			
		"movd mm0, [edi+36]				\n\t"
		"movq mm3, [esi+72]				\n\t"
		"punpckldq mm0, mm0				\n\t" 
		"mov	ecx, 0x0				\n\t"
		"SSSS%=:							\n\t"																			
			"pmaddwd mm0, mm3			\n\t"
			"paddd	mm0, mm7			\n\t"
			"psrad	mm0, mm6			\n\t" //Back to original magnitude
			"movd	eax, mm0			\n\t"
			"add	[ebx+36], eax 		\n\t" //Add into accum
			"add	ecx, 0x1			\n\t"
			"cmp	ecx, 10				\n\t"
		"jne		SSSS%=					\n\t"
		"EMMS						\n" //Done with MMX
		".att_syntax				\n" //Back to ATT syntax
		:
		: "m" (A), "m" (B), "m" (C)
		: "%eax", "%ebx", "%ecx", "%edi", "%esi"
	);//end __asm
}




/*----------------------------------------------------------------------------------------------*/
/*! 
 * doAcqMedium: Acquire using a 10 ms coherent integrationACQ_WEAK
 * */		
Acq_Result_S Acquisition::doAcqMedium(int32 _sv, int32 _doppmin, int32 _doppmax)
{
	Acq_Result_S *result;
	int32 lcv, lcv2, lcv3, mag, magt, index, indext, k;
	int32 iaccum, qaccum;
	int32 data[32];
	CPX *dp = (CPX *)&data[0];
	CPX temp[10];
	int32 *dt = (int32 *)&temp[0];
	int32 *p;
	
	result = &results[_sv];
	index = indext = mag = magt = 0;
	
	#ifdef ACQ_DEBUG
		mag = ACQ_PREFIX;
		write(acq_pipe,&mag,sizeof(int32));
		mag = ACQ_MEDIUM;
		write(acq_pipe,&mag,sizeof(int32));
		write(acq_pipe,&_sv,sizeof(int32));
	#endif	
	
	/* Sweeps through the doppler range */
	for(lcv = (_doppmin/1000); lcv <=  (_doppmax/1000); lcv++)
	{
		/* Covers the 250 Hz spacing */
		for(lcv2 = 0; lcv2 < 4; lcv2++)
		{
			
			/* Do both even and odd */
			for(k = 0; k < 2; k++)
			{
				
				if(gopt.realtime)
					usleep(1000);
				
				/* Do the 10 ms of coherent integration */
				for(lcv3 = 0; lcv3 < 10; lcv3++)
				{
					/* Multiply in frequency domain, shifting appropiately */
					sse_cmulsc(&baseband_rows[20*lcv2+lcv3+k*10][100-lcv], fft_codes[_sv], &coherent[lcv3*resamps_ms], resamps_ms, 4);
				
					/* Compute iFFT */
					piFFT->doiFFT(&coherent[lcv3*resamps_ms], true);
					
				}
					
				/* For each delay do the post-corr FFT, this REALLY needs sped up */
				for(lcv3 = 0; lcv3 < resamps_ms; lcv3++)
				{
					/* Zero it out */
					//memset(&temp[0], 0x0, sizeof(int32)*10);
						
					/* Copy over the relevant data pts */
					p = (int32 *)&coherent[lcv3];
					data[0] = *p; p += resamps_ms;
					data[1] = *p; p += resamps_ms;
					data[2] = *p; p += resamps_ms;
					data[3] = *p; p += resamps_ms;
					data[4] = *p; p += resamps_ms;
					data[5] = *p; p += resamps_ms;
					data[6] = *p; p += resamps_ms;
					data[7] = *p; p += resamps_ms;
					data[8] = *p; p += resamps_ms;
					data[9] = *p;
									
					/* Do the post-correlation dft */
					sse_cacc(dp, dft_rows[0], 10, &iaccum, &qaccum); temp[0].i = iaccum >> 16; temp[0].q = qaccum >> 16;
					sse_cacc(dp, dft_rows[1], 10, &iaccum, &qaccum); temp[1].i = iaccum >> 16; temp[1].q = qaccum >> 16;
					sse_cacc(dp, dft_rows[2], 10, &iaccum, &qaccum); temp[2].i = iaccum >> 16; temp[2].q = qaccum >> 16;
					sse_cacc(dp, dft_rows[3], 10, &iaccum, &qaccum); temp[3].i = iaccum >> 16; temp[3].q = qaccum >> 16;
					sse_cacc(dp, dft_rows[4], 10, &iaccum, &qaccum); temp[4].i = iaccum >> 16; temp[4].q = qaccum >> 16;
					sse_cacc(dp, dft_rows[5], 10, &iaccum, &qaccum); temp[5].i = iaccum >> 16; temp[5].q = qaccum >> 16;
					sse_cacc(dp, dft_rows[6], 10, &iaccum, &qaccum); temp[6].i = iaccum >> 16; temp[6].q = qaccum >> 16;
					sse_cacc(dp, dft_rows[7], 10, &iaccum, &qaccum); temp[7].i = iaccum >> 16; temp[7].q = qaccum >> 16;
					sse_cacc(dp, dft_rows[8], 10, &iaccum, &qaccum); temp[8].i = iaccum >> 16; temp[8].q = qaccum >> 16;
					sse_cacc(dp, dft_rows[9], 10, &iaccum, &qaccum); temp[9].i = iaccum >> 16; temp[9].q = qaccum >> 16;
					
					//sse_dft(dp, dft_rows[0], &temp[0]);
					
					/* Put into the power matrix */
					p = (int32 *)&power[lcv3];
					*p = dt[0]; p += resamps_ms;
					*p = dt[1]; p += resamps_ms;
					*p = dt[2]; p += resamps_ms;
					*p = dt[3]; p += resamps_ms;
					*p = dt[4]; p += resamps_ms;
					*p = dt[5]; p += resamps_ms;
					*p = dt[6]; p += resamps_ms;
					*p = dt[7]; p += resamps_ms;
					*p = dt[8]; p += resamps_ms;
					*p = dt[9];
					
					
				}
	
				/* Convert to a power */
				x86_cmag(&power[0], 10*resamps_ms);
				
				#ifdef ACQ_DEBUG
					write(acq_pipe,msbuff,10*sizeof(int32)*resamps_ms);
				#endif			
		
				/* Find the maximum */
				x86_max((int32 *)power, &indext, &magt, 10*resamps_ms);
			
				/* Found a new maximum */
				if(magt > mag)
				{
					mag = magt;
					index = indext % resamps_ms;
					result->delay = CODE_CHIPS - (float)index*CODE_RATE/ fbase;
					result->doppler = (float)(-lcv*1000) + (float)(lcv2*250) + (indext/resamps_ms)*25.0;
					result->magnitude = (float)mag;
				}
			
			}//end k
				
		}//end lcv2
		
	}//end lcv
	
	#ifdef ACQ_DEBUG
		mag = ACQ_POSTFIX;
		write(acq_pipe,&mag,sizeof(int32));
	#endif			
	
	
	result->sv = _sv;
	
	result->type = ACQ_MEDIUM;
	
	if(result->magnitude > THRESH_MEDIUM)
		result->success = 1;
	else
		result->success = 0;

	return(results[_sv]);
	
}		
/*----------------------------------------------------------------------------------------------*/

	
/*----------------------------------------------------------------------------------------------*/
/*! 
 * doAcqWeak: Acquire using a 10 ms coherent integration and 15 incoherent integrations
 * */		
Acq_Result_S Acquisition::doAcqWeak(int32 _sv, int32 _doppmin, int32 _doppmax)
{
	
	Acq_Result_S *result;
	int32 lcv, lcv2, lcv3, mag, magt, index, indext, k, i;
	int32 iaccum, qaccum;
	int32 data[32];
	CPX *dp = (CPX *)&data[0];
	CPX temp[10];
	int32 *dt = (int32 *)&temp[0];
	int32 *p;
	
	result = &results[_sv];
	index = indext = mag = magt = 0;
	
	#ifdef ACQ_DEBUG
		mag = ACQ_PREFIX;
		write(acq_pipe,&mag,sizeof(int32));
		mag = ACQ_WEAK;
		write(acq_pipe,&mag,sizeof(int32));
		write(acq_pipe,&_sv,sizeof(int32));
	#endif	
	
	/* Sweeps through the doppler range */
	for(lcv = (_doppmin/1000); lcv <=  (_doppmax/1000); lcv++)
	{
		
		/* Covers the 250 Hz spacing */
		for(lcv2 = 0; lcv2 < 4; lcv2++)
		{
			/* Do both even and odd */
			for(k = 0; k < 2; k++)
			{
				
				/* Clear out incoherent int */
				memset(power, 0x0, 10*resamps_ms*sizeof(CPX));
				
				/* Loop over 15 incoherent integrations */
				for(i = 0; i < 15; i++)
				{
					
					/* Do the 10 ms of coherent integration */
					for(lcv3 = 0; lcv3 < 10; lcv3++)
					{
						/* Multiply in frequency domain, shifting appropiately */
						sse_cmulsc(&baseband_rows[lcv2*310 + lcv3 + i*20 + k*10][100-lcv], fft_codes[_sv], &coherent[lcv3*resamps_ms], resamps_ms, 4);
					
						/* Compute iFFT */
						piFFT->doiFFT(&coherent[lcv3*resamps_ms], true);
					}
						
					/* For each delay do the post-corr FFT, this REALLY needs sped up */
					for(lcv3 = 0; lcv3 < resamps_ms; lcv3++)
					{
						/* Zero it out */
						//memset(&temp[0], 0x0, sizeof(int32)*10);
							
						/* Copy over the relevant data pts */
						p = (int32 *)&coherent[lcv3];
						data[0] = *p; p += resamps_ms;
						data[1] = *p; p += resamps_ms;
						data[2] = *p; p += resamps_ms;
						data[3] = *p; p += resamps_ms;
						data[4] = *p; p += resamps_ms;
						data[5] = *p; p += resamps_ms;
						data[6] = *p; p += resamps_ms;
						data[7] = *p; p += resamps_ms;
						data[8] = *p; p += resamps_ms;
						data[9] = *p;
										
						/* Do the post-correlation dft */
						sse_cacc(dp, dft_rows[0], 10, &iaccum, &qaccum); temp[0].i = iaccum >> 16; temp[0].q = qaccum >> 16;
						sse_cacc(dp, dft_rows[1], 10, &iaccum, &qaccum); temp[1].i = iaccum >> 16; temp[1].q = qaccum >> 16;
						sse_cacc(dp, dft_rows[2], 10, &iaccum, &qaccum); temp[2].i = iaccum >> 16; temp[2].q = qaccum >> 16;
						sse_cacc(dp, dft_rows[3], 10, &iaccum, &qaccum); temp[3].i = iaccum >> 16; temp[3].q = qaccum >> 16;
						sse_cacc(dp, dft_rows[4], 10, &iaccum, &qaccum); temp[4].i = iaccum >> 16; temp[4].q = qaccum >> 16;
						sse_cacc(dp, dft_rows[5], 10, &iaccum, &qaccum); temp[5].i = iaccum >> 16; temp[5].q = qaccum >> 16;
						sse_cacc(dp, dft_rows[6], 10, &iaccum, &qaccum); temp[6].i = iaccum >> 16; temp[6].q = qaccum >> 16;
						sse_cacc(dp, dft_rows[7], 10, &iaccum, &qaccum); temp[7].i = iaccum >> 16; temp[7].q = qaccum >> 16;
						sse_cacc(dp, dft_rows[8], 10, &iaccum, &qaccum); temp[8].i = iaccum >> 16; temp[8].q = qaccum >> 16;
						sse_cacc(dp, dft_rows[9], 10, &iaccum, &qaccum); temp[9].i = iaccum >> 16; temp[9].q = qaccum >> 16;
						
						//sse_dft(dp, dft_rows[0], &temp[0]);
					
						x86_cmag(&temp[0], 10);
							
						/* Put into the power matrix */
						p = (int32 *)&power[lcv3];
						*p += dt[0]; p += resamps_ms;
						*p += dt[1]; p += resamps_ms;
						*p += dt[2]; p += resamps_ms;
						*p += dt[3]; p += resamps_ms;
						*p += dt[4]; p += resamps_ms;
						*p += dt[5]; p += resamps_ms;
						*p += dt[6]; p += resamps_ms;
						*p += dt[7]; p += resamps_ms;
						*p += dt[8]; p += resamps_ms;
						*p += dt[9];
						
						
					}
			
				}//end i
			
				#ifdef ACQ_DEBUG
					write(acq_pipe,msbuff,10*sizeof(int32)*resamps_ms);
				#endif			
			
				/* Find the maximum */
				x86_max((int32 *)power, &indext, &magt, 10*resamps_ms);
				
				/* Found a new maximum */
				if(magt > mag)
				{
					mag = magt;
					index = indext % resamps_ms;
					result->delay = CODE_CHIPS - (float)index*CODE_RATE/ fbase;
					result->doppler = (float)(-lcv*1000) + (float)(lcv2*250) + (indext/resamps_ms)*25.0;
					result->magnitude = (float)mag;
				}
			
			}//end k
				
		}//end lcv2
		
	}//end lcv
	
	#ifdef ACQ_DEBUG
		mag = ACQ_POSTFIX;
		write(acq_pipe,&mag,sizeof(int32));
	#endif			
	
	result->sv = _sv;
	
	result->type = ACQ_WEAK;
	
	if(result->magnitude > THRESH_WEAK)
		result->success = 1;
	else
		result->success = 0;

	return(results[_sv]);
	
}			
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
/*! 
 * Export: 
 * */	
void Acquisition::Acquire()
{
	int32 lcv;
			
	switch(state)
	{
		case ACQ_STRONG:
			doPrepIF(ACQ_STRONG, buff);
			doAcqStrong(sv, -DOPPLER_STRONG, DOPPLER_STRONG);
			break;
		case ACQ_MEDIUM:
			doPrepIF(ACQ_MEDIUM, buff);
			doAcqMedium(sv, -DOPPLER_MEDIUM, DOPPLER_MEDIUM);
			break;
		case ACQ_WEAK:
			doPrepIF(ACQ_WEAK, buff);
			doAcqWeak(sv, -DOPPLER_WEAK, DOPPLER_WEAK);
			break;
		default:
			doAcqStrong(sv, -DOPPLER_STRONG, DOPPLER_STRONG);
	}

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
/*! 
 * UpdateState: drive the acquisition SV and state
 * */	
void Acquisition::UpdateState()
{
	
	sv++;
	if(sv >= NUM_CODES)
	{
		sv = 0;
		switch(state)
		{
			case ACQ_STRONG:
				state = ACQ_MEDIUM;
				break;
			case ACQ_MEDIUM:
				state = ACQ_STRONG;
				break;
			case ACQ_WEAK:
				state = ACQ_STRONG;
				break;
			default:
				state = ACQ_STRONG;
		}
	}
	
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
/*! 
 * Inport: 
 * */	
void Acquisition::Inport()
{
	int32 last;
	int32 bread;
	int32 lastcount;
	int32 ms;
	int32 ms_per_read;
	ms_packet *p = NULL;
	timespec ret;
	
	ret.tv_sec = 0;
	ret.tv_nsec = 100000;
	
	switch(state)	
	{
		case ACQ_STRONG:
			ms_per_read = 100;
			break;			
		case ACQ_MEDIUM:
			ms_per_read = 100;
			break;			
		case ACQ_WEAK:
			ms_per_read = 310;
			break;			
		default:
			ms_per_read = 310;
	}

	/* First wait for a request */
	bread = read(Trak_2_Acq_P[READ], &request, sizeof(Acq_Request_S));
	//printf("Got request %d\n",request.corr);

	/* Set the flag to high to let FIFO know the acq will be collecting data */
	pthread_mutex_lock(&mAcq);
	gAcq_high = true;
	pthread_mutex_unlock(&mAcq);
	
	/* Collect necessary data */
	lastcount = 0; ms = 0;
	while((ms < ms_per_read) && grun)
	{
		/* Get the tail */
		last = packet.count;
		pFIFO->Dequeue(MAX_CHANNELS, &packet);	
		while(packet.count == last)
		{
			usleep(250);
			pFIFO->Dequeue(MAX_CHANNELS, &packet);
		}
		
		memcpy(&buff[SAMPS_MS*ms], &packet.data, SAMPS_MS*sizeof(CPX));
				
		/* Detect broken packets */			
		if(ms > 0)
		{
			if((packet.count - lastcount) != 1)
			{
				//printf("Broken GPS stream %d,%d\n",packet.count,lastcount);
				ms = 0; /* Recollect data */
			}
		}
		else
			request.count = packet.count;

		ms++;
		lastcount = packet.count;
		
	}

	/* Done collecting data */	
	pthread_mutex_lock(&mAcq);
	gAcq_high = false;
	pthread_mutex_unlock(&mAcq);
		
}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
/*! 
 * Export: 
 * */	
void Acquisition::Export(char * _fname)
{
	
	int32 lcv;
	FILE *fp;
	Acq_Result_S *p;
	
	if(_fname == NULL)
	{
		fp = fopen("Acq.txt","wa");
		if(fp == NULL)
			return;
		fseek(fp, 0x0, SEEK_SET);
		
	} 
	else
	{
		fp = fopen(_fname,"wt");
		if(fp == NULL)
			return;
	}
		

	for(lcv = 0; lcv < NUM_CODES; lcv++)
	{
		p = &results[lcv];
		fprintf(fp, "%02d,%02d,%10.2f,%10.0f,%15.0f,%1d\n",p->type,lcv+1,p->delay,p->doppler,p->magnitude,p->success);
	}
	fclose(fp);
	
	/* Write result to the tracking task */
	results[sv].count = request.count;
	write(Acq_2_Trak_P[WRITE], &results[sv], sizeof(Acq_Result_S));
	write(Acq_2_Telem_P[WRITE], &results[sv], sizeof(Acq_Result_S));

}
/*----------------------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------------------------*/
/*! 
 * doRotate: 
 * 	
void Acquisition::doRotate(CPX *_in, int32 _pts)
{
	
	if(_pts > 0)
	{
		memcpy(rotate, _in, resamps_ms*sizeof(CPX));
		memcpy(_in, &rotate[resamps_ms-_pts], _pts*sizeof(CPX));
		memcpy(&_in[_pts], rotate, (resamps_ms-_pts)*sizeof(CPX)); 
	}
	else
	{
		_pts = -_pts;
		memcpy(rotate, _in, resamps_ms*sizeof(CPX));
		memcpy(_in, &rotate[_pts], (resamps_ms-_pts)*sizeof(CPX));
		memcpy(&_in[_pts], rotate, _pts*sizeof(CPX));
	}
	
}	
/*----------------------------------------------------------------------------------------------*/
