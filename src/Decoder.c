/***********************************************************************
Copyright (c) 2006-2011, Skype Limited. All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, (subject to the limitations in the disclaimer below)
are permitted provided that the following conditions are met:
- Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
- Neither the name of Skype Limited, nor the names of specific
contributors, may be used to endorse or promote products derived from
this software without specific prior written permission.
NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED
BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
CONTRIBUTORS ''AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***********************************************************************/


/*****************************/
/* Silk decoder test program */
/*****************************/

#ifdef _WIN32
#define _CRT_SECURE_NO_DEPRECATE    1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SKP_Silk_SDK_API.h"
#include "SKP_Silk_SigProc_FLP.h"

/* Define codec specific settings should be moved to h file */
#define MAX_BYTES_PER_FRAME     1024
#define MAX_INPUT_FRAMES        5
#define MAX_FRAME_LENGTH        480
#define FRAME_LENGTH_MS         20
#define MAX_API_FS_KHZ          48
#define MAX_LBRR_DELAY          2


// HELPER MARCOs for wav header
#define WRITE_U32(buf, x) *(buf)     = (unsigned char)((x)&0xff);\
                          *((buf)+1) = (unsigned char)(((x)>>8)&0xff);\
                          *((buf)+2) = (unsigned char)(((x)>>16)&0xff);\
                          *((buf)+3) = (unsigned char)(((x)>>24)&0xff);

#define WRITE_U16(buf, x) *(buf)     = (unsigned char)((x)&0xff);\
                          *((buf)+1) = (unsigned char)(((x)>>8)&0xff);

// inner linkage -----> Wave header 
// ref: http://stackoverflow.com/questions/1460007/wav-file-from-captured-pcm-sample-data
// At this moment, the size are not known.
// We set the fields related to the size zero...
//
static int write_prelim_header(FILE * out, int channels, int samplerate){

	int bits = 16;
	unsigned char headbuf[44];  /* The whole buffer */

	int knownlength = 0;

	int bytespersec = channels * samplerate * bits / 8;
	int align = channels * bits / 8;
	int samplesize = bits;


	/*
	here's a good ref...
	http://www.lightlink.com/tjweber/StripWav/Canon.html
	Based on the link above,
	Actually, we are writting a simplified version of Wav.
	*/
	memcpy(headbuf, "RIFF", 4);
	WRITE_U32(headbuf + 4, 0); // fileLength - 8 (4 bytes)
	memcpy(headbuf + 8, "WAVE", 4);
	memcpy(headbuf + 12, "fmt ", 4);
	WRITE_U32(headbuf + 16, 16); // length of fmt data
	WRITE_U16(headbuf + 20, 1);  /* format , 1==PCM */
	WRITE_U16(headbuf + 22, channels); // 1==miss mono, 2== stereo 
	WRITE_U32(headbuf + 24, samplerate);
	WRITE_U32(headbuf + 28, bytespersec);
	WRITE_U16(headbuf + 32, align);
	WRITE_U16(headbuf + 34, samplesize);  // 16 or 8 
	memcpy(headbuf + 36, "data", 4);
	WRITE_U32(headbuf + 40, 0); // length of data block  fileLength - 44

	if (fwrite(headbuf, 1, 44, out) != 44)
	{
		printf("ERROR: Failed to write wav header: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}


// Update the wave header
// Now....
// The size is known..
static void update_wave_header(FILE * fstream, unsigned int dataSize){

	unsigned char buffer[4]; // four bytes
	fseek(fstream, 4, SEEK_SET);
	WRITE_U32(buffer, dataSize + 44 - 8);
	if (fwrite(buffer, sizeof(unsigned char), 4, fstream) != 4){
		printf("ERROR: Failed to update header info \n");
		exit(-1);
	}


	fseek(fstream, 40, SEEK_SET);
	WRITE_U32(buffer, dataSize);
	if (4 != fwrite(buffer, sizeof(unsigned char), 4, fstream)){
		printf("ERROR: Failed to update header info \n");
		exit(-1);
	}

}


#ifdef _SYSTEM_IS_BIG_ENDIAN
/* Function to convert a little endian int16 to a */
/* big endian int16 or vica verca                 */
void swap_endian(
	SKP_int16       vec[],
	SKP_int         len
	)
{
	SKP_int i;
	SKP_int16 tmp;
	SKP_uint8 *p1, *p2;

	for( i = 0; i < len; i++ ){
		tmp = vec[ i ];
		p1 = (SKP_uint8 *)&vec[ i ]; p2 = (SKP_uint8 *)&tmp;
		p1[ 0 ] = p2[ 1 ]; p1[ 1 ] = p2[ 0 ];
	}
}
#endif

/* Seed for the random number generator, which is used for simulating packet loss */
static SKP_int32 rand_seed = 1;

static void print_usage(char* argv[]) {

	printf("To LiuZiDong@zhihu (.../people/dodolook8).\n"
		"Wechat(Android v6.*) amr to wav\n"
		"Usage 0: \n"
		"    Drag one or more files to this program: %s.\n"
		"Usage 1: \n"
		"    %s <file0.amr> <file1.amr> ....\n"
		"By The Way: Happy New Chinese Year @2016.02.07\n", argv[0], argv[0]);
	printf("Press any key to Continue..\n");
	getchar();
}

int main(int argc, char* argv[])
{
	/* Notice:
	   If you want to parse file Expansion 
	   link with setargv.obj when building with Windows toolChains.
	*/

	size_t    counter;
	SKP_int32 args, totPackets, i, k;
	SKP_int16 ret, len, tot_len;
	SKP_int16 nBytes;
	// 
	SKP_uint8 payload[MAX_BYTES_PER_FRAME * MAX_INPUT_FRAMES * (MAX_LBRR_DELAY + 1)];
	SKP_uint8 *payloadEnd = NULL, *payloadToDec = NULL;
	SKP_uint8 FECpayload[MAX_BYTES_PER_FRAME * MAX_INPUT_FRAMES], *payloadPtr;
	SKP_int16 nBytesFEC;
	SKP_int16 nBytesPerPacket[MAX_LBRR_DELAY + 1], totBytes;
	SKP_int16 out[((FRAME_LENGTH_MS * MAX_API_FS_KHZ) << 1) * MAX_INPUT_FRAMES], *outPtr;
	char      speechOutFileName[260], bitInFileName[260];
	const char* outPutSuffix = ".wav\0";
	size_t pathLen;
	FILE      *bitInFile, *speechOutFile;
	SKP_int32 API_Fs_Hz = 0;
	SKP_int32 decSizeBytes;
	void      *psDec;
	float     loss_prob;
	SKP_int32 frames, lost, quiet;
	SKP_SILK_SDK_DecControlStruct DecControl;
	unsigned int dataSize = 0;

	if (argc < 2) {
		print_usage(argv);
		exit(0);
	}

	printf("To LiuZiDong@zhihu (.../people/dodolook8).\n"
		"Wechat(Android v6.*) amr to wav\n");


	/* default settings */
	quiet = 1;
	loss_prob = 0.0f;



	/* Create a Decoder */
	/* Set the samplingrate that is requested for the output */
	DecControl.API_sampleRate = 24000;

	/* Initialize to one frame per packet, for proper concealment before first packet arrives */
	DecControl.framesPerPacket = 1;

	/* Create a decoder */
	ret = SKP_Silk_SDK_Get_Decoder_Size(&decSizeBytes);
	if (ret) {
		printf("\nSKP_Silk_SDK_Get_Decoder_Size returned %d", ret);
		exit(-1);
	}
	psDec = malloc(decSizeBytes);
	/* Reset decoder */
	ret = SKP_Silk_SDK_InitDecoder(psDec);
	if (ret) {
		printf("\nSKP_Silk_InitDecoder returned %d", ret);
		exit(-1);
	}

	/* get arguments */
	args = 1;

	while (args < argc) { // huge loop!!! -- loop all the input files

		printf("LOOP-[%d]\n", args-1);
		dataSize = 0;
		if (strlen(argv[args]) > 250){
			printf("The path is too long. \n "
				"This may cause sth unexpected.\n"
				"Try to put your *.amr files in a place which has"
				" a relatively simple path name: say, E:\\Wechatfiles\\ \n"
				"Press Any Key to Exit...\n");
			free(psDec);
			getchar();
			exit(-1);
		}

		strcpy(bitInFileName, argv[args]);
		printf("--- input file: %s\n", bitInFileName);
		args++;
		/* Open files */
		bitInFile = fopen(bitInFileName, "rb");
		if (bitInFile == NULL) {
			printf("Warning: could not open input file %s   ---File Escaped!\n", bitInFileName);
			continue;
		}

		/* Check Silk header */
	{
		char header_buf[50];
		// drop the first byte [02] for Wechat voice2 
		// @2016-02-07 Wechat Android 6.*
		// Voice files are in folder voice2
		counter = fread(header_buf, sizeof(char), 1, bitInFile);
		counter = fread(header_buf, sizeof(char), strlen("#!SILK_V3"), bitInFile);
		header_buf[strlen("#!SILK_V3")] = (char)0; /* Terminate with a null character */
		if (strcmp(header_buf, "#!SILK_V3") != 0) {
			/* Non-equal strings */
			printf("--- Warning: Wrong File Header %s.  --- File Escaped.\n", header_buf);
			continue;
		}
	}

	// construct the path of the output file
	strcpy(speechOutFileName, bitInFileName);
	strcpy(&speechOutFileName[strlen(speechOutFileName)], outPutSuffix);
	
	speechOutFile = fopen(speechOutFileName, "wb");
	if (speechOutFile == NULL) {
		printf("Error: could not open output file %s\n", speechOutFileName);
		free(psDec);
		getchar();
		exit(-1);
	}

	printf("--- Writing wav file: %s\n", speechOutFileName);

	// Write a preliminary wave header (no size info)
	write_prelim_header(speechOutFile, 1, 24000);

	totPackets = 0;
	payloadEnd = payload;

	/* Simulate the jitter buffer holding MAX_FEC_DELAY packets */
	for (i = 0; i < MAX_LBRR_DELAY; i++) {
		/* Read payload size */
		counter = fread(&nBytes, sizeof(SKP_int16), 1, bitInFile);
#ifdef _SYSTEM_IS_BIG_ENDIAN
		swap_endian(&nBytes, 1);
#endif
		/* Read payload */
		counter = fread(payloadEnd, sizeof(SKP_uint8), nBytes, bitInFile);

		if( ( SKP_int16 )counter < nBytes ) {
			break;
	}
		nBytesPerPacket[i] = nBytes;
		payloadEnd += nBytes;
	}

	//FIXME: Above, WTF? 
	//  Read first two Packet? 
	//  But ... they are not decoded ? 


	while (1) {
		/* Read payload size */
		counter = fread(&nBytes, sizeof(SKP_int16), 1, bitInFile);
#ifdef _SYSTEM_IS_BIG_ENDIAN
		swap_endian(&nBytes, 1);
#endif
		if (nBytes < 0 || counter < 1) {
			break;
		}

		/* Read payload */
		counter = fread(payloadEnd, sizeof(SKP_uint8), nBytes, bitInFile);
		if ((SKP_int16)counter < nBytes) {
			break;
		}


		// Well.... I don't need to simulate loss
		nBytesPerPacket[MAX_LBRR_DELAY] = nBytes;
		payloadEnd += nBytes;
	
		if (nBytesPerPacket[0] == 0) {
			/* Indicate lost packet */
			lost = 1;

			/* Packet loss. Search after FEC in next packets. Should be done in the jitter buffer */
			payloadPtr = payload;
			for (i = 0; i < MAX_LBRR_DELAY; i++) {
				if (nBytesPerPacket[i + 1] > 0) {
					SKP_Silk_SDK_search_for_LBRR(payloadPtr, nBytesPerPacket[i + 1], i + 1, FECpayload, &nBytesFEC);
					if (nBytesFEC > 0) {
						payloadToDec = FECpayload;
						nBytes = nBytesFEC;
						lost = 0;
						break;
					}
				}
				payloadPtr += nBytesPerPacket[i + 1];
			}
		}
		else {
			lost = 0;
			nBytes = nBytesPerPacket[0];
			payloadToDec = payload;
		}


		//FIMXE , Now there're (MAX_LBRR_DELAY + 1) packets in payload

		/* Silk decoder */
		outPtr = out;
		tot_len = 0;

		if (lost == 0) {
			/* No Loss: Decode all frames in the packet */
			frames = 0;
			do {
				/* Decode 20 ms */
				ret = SKP_Silk_SDK_Decode(psDec, &DecControl, 0, payloadToDec, nBytes, outPtr, &len);
				if (ret) {
					printf("\nSKP_Silk_SDK_Decode returned %d", ret);
				}

				frames++;
				outPtr += len;
				tot_len += len;
				if (frames > MAX_INPUT_FRAMES) {
					/* Hack for corrupt stream that could generate too many frames */
					outPtr = out;
					tot_len = 0;
					frames = 0;
				}
				/* Until last 20 ms frame of packet has been decoded */
			} while (DecControl.moreInternalDecoderFrames);
		}
		else {
			/* Loss: Decode enough frames to cover one packet duration */
			for (i = 0; i < DecControl.framesPerPacket; i++) {
				/* Generate 20 ms */
				ret = SKP_Silk_SDK_Decode(psDec, &DecControl, 1, payloadToDec, nBytes, outPtr, &len);
				if (ret) {
					printf("\nSKP_Silk_Decode returned %d", ret);
				}
				outPtr += len;
				tot_len += len;
			}
		}
		totPackets++;

		/* Write output to file */
#ifdef _SYSTEM_IS_BIG_ENDIAN   
		swap_endian(out, tot_len);
#endif
		fwrite(out, sizeof(SKP_int16), tot_len, speechOutFile);
		dataSize += tot_len;
		/* Update buffer */
		totBytes = 0;
		for (i = 0; i < MAX_LBRR_DELAY; i++) {
			totBytes += nBytesPerPacket[i + 1];
		}
		// drop the first frame... a Cycle buffer??
		SKP_memmove(payload, &payload[nBytesPerPacket[0]], totBytes * sizeof(SKP_uint8));
		payloadEnd -= nBytesPerPacket[0];
		SKP_memmove(nBytesPerPacket, &nBytesPerPacket[1], MAX_LBRR_DELAY * sizeof(SKP_int16));

		if (!quiet) {
			fprintf(stderr, "\rPackets decoded:             %d", totPackets);
		}
		}


	// Now... Still two Packets in the buffer....
	// Empty ...

	/* Empty the recieve buffer */
	for (k = 0; k < MAX_LBRR_DELAY; k++) {
		if (nBytesPerPacket[0] == 0) {
			/* Indicate lost packet */
			lost = 1;

			/* Packet loss. Search after FEC in next packets. Should be done in the jitter buffer */
			payloadPtr = payload;
			for (i = 0; i < MAX_LBRR_DELAY; i++) {
				if (nBytesPerPacket[i + 1] > 0) {
					SKP_Silk_SDK_search_for_LBRR(payloadPtr, nBytesPerPacket[i + 1], i + 1, FECpayload, &nBytesFEC);
					if (nBytesFEC > 0) {
						payloadToDec = FECpayload;
						nBytes = nBytesFEC;
						lost = 0;
						break;
					}
				}
				payloadPtr += nBytesPerPacket[i + 1];
			}
		}
		else {
			lost = 0;
			nBytes = nBytesPerPacket[0];
			payloadToDec = payload;
		}

		/* Silk decoder */
		outPtr = out;
		tot_len = 0;

		if (lost == 0) {
			/* No loss: Decode all frames in the packet */
			frames = 0;
			do {
				/* Decode 20 ms */
				ret = SKP_Silk_SDK_Decode(psDec, &DecControl, 0, payloadToDec, nBytes, outPtr, &len);
				if (ret) {
					printf("\nSKP_Silk_SDK_Decode returned %d", ret);
				}

				frames++;
				outPtr += len;
				tot_len += len;
				if (frames > MAX_INPUT_FRAMES) {
					/* Hack for corrupt stream that could generate too many frames */
					outPtr = out;
					tot_len = 0;
					frames = 0;
				}
				/* Until last 20 ms frame of packet has been decoded */
			} while (DecControl.moreInternalDecoderFrames);
		}
		else {
			/* Loss: Decode enough frames to cover one packet duration */

			/* Generate 20 ms */
			for (i = 0; i < DecControl.framesPerPacket; i++) {
				ret = SKP_Silk_SDK_Decode(psDec, &DecControl, 1, payloadToDec, nBytes, outPtr, &len);
				if (ret) {
					printf("\nSKP_Silk_Decode returned %d", ret);
				}
				outPtr += len;
				tot_len += len;
			}
		}
		totPackets++;

		/* Write output to file */
#ifdef _SYSTEM_IS_BIG_ENDIAN   
		swap_endian(out, tot_len);
#endif
		fwrite(out, sizeof(SKP_int16), tot_len, speechOutFile);
		dataSize += tot_len;
		/* Update Buffer */
		totBytes = 0;
		for (i = 0; i < MAX_LBRR_DELAY; i++) {
			totBytes += nBytesPerPacket[i + 1];
		}
		SKP_memmove(payload, &payload[nBytesPerPacket[0]], totBytes * sizeof(SKP_uint8));
		payloadEnd -= nBytesPerPacket[0];
		SKP_memmove(nBytesPerPacket, &nBytesPerPacket[1], MAX_LBRR_DELAY * sizeof(SKP_int16));

		if (!quiet) {
			fprintf(stderr, "\rPackets decoded:              %d", totPackets);
		}
	}

	if (!quiet) {
		printf("\nDecoding Finished \n");
	}

	// update the header
	update_wave_header(speechOutFile, dataSize);
	/* Close files */
	fclose(speechOutFile);
	fclose(bitInFile);
	printf("--- Done!\n\n");

	} // end of while args < argc

	// free the decoder
	free(psDec);

	printf("\nPress any key to exit..\n"
		"Happy New Chinese Year!\n");
	getchar();
	return 0;
}
