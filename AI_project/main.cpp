/*
duplex.cpp
by Gary P. Scavone, 2006-2007.

This program opens a duplex stream and passes
input directly through to the output.
*/
/******************************************/

#include "RtAudio.h"
#include <stdio.h>
#include <stdlib.h>
#include <cstdlib>
#include <cstring>
#include <time.h>
#include <io.h>
#include <iostream>
#include "ProcBuffers.h"
#include "sigproc.h"
#include "SNMF.h"
#include "header.h"
/*
typedef char MY_TYPE;
#define FORMAT RTAUDIO_SINT8
*/

SNMF *n_snmf;

typedef signed short MY_TYPE;
#define FORMAT RTAUDIO_SINT16

#define CHANNEL 2
#define BUFFERFRAME 256

int record_num = 0;
int copyend = 0;
/*
typedef S24 MY_TYPE;
#define FORMAT RTAUDIO_SINT24

typedef signed long MY_TYPE;
#define FORMAT RTAUDIO_SINT32

typedef float MY_TYPE;
#define FORMAT RTAUDIO_FLOAT32

typedef double MY_TYPE;
#define FORMAT RTAUDIO_FLOAT64
*/

// Platform-dependent sleep routines.
#if defined( __WINDOWS_ASIO__ ) || defined( __WINDOWS_DS__ ) || defined( __WINDOWS_WASAPI__ )
#include <windows.h>
#define SLEEP( milliseconds ) Sleep( (DWORD) milliseconds ) 
#else // Unix variants
#include <unistd.h>
#define SLEEP( milliseconds ) usleep( (unsigned long) (milliseconds * 1000.0) )
#endif

void usage(void) {
	// Error function in case of incorrect command-line
	// argument specifications
	std::cout << "\nuseage: duplex N fs <iDevice> <oDevice> <iChannelOffset> <oChannelOffset>\n";
	std::cout << "    where N = number of channels,\n";
	std::cout << "    fs = the sample rate,\n";
	std::cout << "    iDevice = optional input device to use (default = 0),\n";
	std::cout << "    oDevice = optional output device to use (default = 0),\n";
	std::cout << "    iChannelOffset = an optional input channel offset (default = 0),\n";
	std::cout << "    and oChannelOffset = optional output channel offset (default = 0).\n\n";
	exit(0);
}

struct InputData {
	MY_TYPE* in_buffer;
	MY_TYPE* out_buffer;
	MY_TYPE* z_buffer;
	unsigned long bufferBytes;
	unsigned long totalFrames;
	unsigned long iframeCounter;
	unsigned long oframeCounter;
	unsigned int channels;
};


// �ǽð����� MIC�� ���� �Է��� ���ۿ� �����ϰ� ������ �����͸� processing���� ó���� �� �ǽð����� ����Ѵ�.
int inout(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
	double /*streamTime*/, RtAudioStreamStatus status, void *data)
{
	InputData *iData = (InputData *)data;

	////�ǽð����� �Է��� �޴� �κ�
	unsigned int frames = nBufferFrames;
	//�ִ� ���ۻ���� ���� ��� ũ�⸦ �Ѵ� �����ʹ� �������� �ʴ´�.
	if (iData->iframeCounter + nBufferFrames > iData->totalFrames) 
	{
		frames = iData->totalFrames - iData->iframeCounter;
		iData->bufferBytes = frames * iData->channels * sizeof(MY_TYPE);
	}
	unsigned long in_offset = iData->iframeCounter * iData->channels;
	//����� ���۰� �����ϸ� �̸� process�ϱ� ���� �����ϸ� ó���� �����Ͱ� �����Ѵٰ� record_num���� Ȯ���Ѵ�.
	memcpy(iData->in_buffer + in_offset, inputBuffer, iData->bufferBytes);
	iData->iframeCounter += frames;
	record_num++;

	//����� �����Ͱ� �ִ� ���ۻ���� �ѱ�� �ٽ� offset�� 0���� �Ͽ� overwriting�Ѵ�.
	//�̹� ���� �����ʹ� process�� ���� ����Ǿ���.
	if (iData->iframeCounter >= iData->totalFrames)
	{
		return 2; //���� �ð�(32��)�� �� �Ǹ� ���α׷��� ���ߵ��� �����Ͽ���.
		iData->iframeCounter = 0;
	}

	//process���� ó���� �����Ͱ� �ǽð� ����� ���� ó�� �� �߻��ϴ� copyend��� ��ȣ�� �߸� callback�Լ��� output���� �����Ѵ�.
	if (copyend)
	{
		if (iData->oframeCounter + nBufferFrames > iData->totalFrames)
		{
			frames = iData->totalFrames - iData->oframeCounter;
			iData->bufferBytes = frames * iData->channels * sizeof(MY_TYPE);
		}
		unsigned long out_offset = iData->oframeCounter * iData->channels;
		memcpy(outputBuffer, iData->out_buffer + out_offset, iData->bufferBytes);
		iData->oframeCounter += frames;
		//ó���� �����Ͱ� ��µǸ� copyend���� ���δ�.
		copyend--;
		if (iData->oframeCounter >= iData->totalFrames)
		{
			iData->oframeCounter = 0;
		}
	}
	//ó���� �����Ͱ� ������ (��ȭ������ ���°��) 0�� ����Ѵ�.
	else if (copyend == 0)
	{
		memcpy(outputBuffer, iData->z_buffer, iData->bufferBytes);
	}
	return 0;
}

void Training()
{
	n_snmf = new SNMF();
	int i, j;
	double *f_stack, *m_stack, *n_stack;
	double **f_tr_out, **m_tr_out, **n_tr_out, **mn_tr_out, **fn_tr_out;
	double **total_basis_x;
	int total_stack = tr_sec * SamplingFreq;
	f_stack = new double[total_stack];
	m_stack = new double[total_stack];
	n_stack = new double[total_stack];

	f_tr_out = new double*[size_basis];
	m_tr_out = new double*[size_basis];
	n_tr_out = new double*[size_basis];
	mn_tr_out = new double*[size_basis];
	fn_tr_out = new double*[size_basis];
	total_basis_x = new double*[size_basis];
	for (i = 0; i < size_basis; i++)
	{
		f_tr_out[i] = new double[R_x];
		m_tr_out[i] = new double[R_x];
		n_tr_out[i] = new double[R_d / 2];
		mn_tr_out[i] = new double[R_d / 2];
		fn_tr_out[i] = new double[R_d / 2];
		total_basis_x[i] = new double[2 * R_x + 3 * R_d / 2];
	}
	//Training�� ���α׷� ���� �� ���� �ѹ� �����Ѵ�. ó���ð��� ��� 1�п��� 2������ �ҿ�ȴ�.
	//���� ������ Training�Ͽ� basis_mat.txt������ �����ϸ� �ٷ� training�Լ��� ������.
	if (_access("basis_mat.txt", 0) != 0)
	{
		//basis training�� �ϱ� ���� 6�� 24�� ������ stack�� �׾� �����´�.
		n_snmf->basis_stack(total_stack, f_stack, m_stack, n_stack);

		//�� basis�� 350���� ����ȭ�� basis 100rank, ����ȭ�� basis 100rank, ���� basis 50rank
		//����ȭ�� + ���� basis 50 + 50 ranks, ����ȭ�� + ���� basis 50 + 50 ranks�� training�Ѵ�.

		n_snmf->SNMF_training(f_stack, total_stack, R_x, f_tr_out); //female data training

		std::cout << "f_tr_out fin!" << std::endl;

		n_snmf->SNMF_training(m_stack, total_stack, R_x, m_tr_out); //male data training

		std::cout << "m_tr_out fin!" << std::endl;

		n_snmf->SNMF_training(n_stack, total_stack, R_d / 2, n_tr_out);

		std::cout << "n_tr_out fin!" << std::endl;

		n_snmf->SNMF_training(m_stack, total_stack, R_d / 2, mn_tr_out);

		std::cout << "mn_tr_out fin!" << std::endl;

		n_snmf->SNMF_training(f_stack, total_stack, R_d / 2, fn_tr_out);

		std::cout << "fn_tr_out fin!" << std::endl;

		for (i = 0; i < size_basis; i++)
		{
			for (j = 0; j < R_x; j++)//fmale_basis 100rank
			{
				total_basis_x[i][j] = f_tr_out[i][j];
			}
			for (; j < 2 * R_x; j++)//male_basis 100rank
			{
				total_basis_x[i][j] = m_tr_out[i][j - R_x];
			}
			for (; j < 2 * R_x + R_d / 2; j++)//noise_basis 50rank
			{
				total_basis_x[i][j] = n_tr_out[i][j - 2 * R_x];
			}
			for (; j < 2 * R_x + R_d; j++)//female_noise_basis 50rank
			{
				total_basis_x[i][j] = fn_tr_out[i][j - (2 * R_x + R_d / 2)];
			}
			for (; j < 2 * R_x + 3 * R_d / 2; j++)//male_noise_basis 50rank
			{
				total_basis_x[i][j] = mn_tr_out[i][j - (2 * R_x + R_d)];
			}
		}
		/*basis �����ϱ�*/
		FILE *pFile;
		pFile = fopen("basis_mat.txt", "wt");
		if (pFile != NULL)
		{
			for (i = 0; i < size_basis; i++)
			{
				for (j = 0; j < 2 * R_x + 3 * R_d / 2; j++)
				{
					fprintf(pFile, "%E\t", total_basis_x[i][j]);
				}
			}
		}
		fclose(pFile);
	}
	for (i = 0; i < size_basis; i++)
	{
		delete[] f_tr_out[i];
		delete[] m_tr_out[i];
		delete[] n_tr_out[i];
		delete[] mn_tr_out[i];
		delete[] fn_tr_out[i];
		delete[] total_basis_x[i];
	}
	delete[] f_tr_out;
	delete[] m_tr_out;
	delete[] n_tr_out;
	delete[] mn_tr_out;
	delete[] fn_tr_out;
	delete[] total_basis_x;
	delete[] f_stack;
	delete[] m_stack;
	delete[] n_stack;
	delete n_snmf;
}

int main(void)
{
	//Device���� 0 : PC, 1 : OCTA-CAPTURE
	unsigned int channels, fs, bufferBytes, oDevice = 1, iDevice = 1, iOffset = 0, oOffset = 0;
	double time = 16.0;
	int in_buffer_cnt = 0;
	int out_buffer_cnt = 0;
	int i, j, ch;
	int proc_end = 0;

	double **input, **proc_output;

	if (_access("basis_mat.txt", 0) != 0)
	{
		Training();
	}

	input = new double *[CHANNEL];
	proc_output = new double *[CHANNEL];
	for (i = 0; i < CHANNEL; i++)
	{
		input[i] = new double[BUFFERFRAME];
		proc_output[i] = new double[3 * BUFFERFRAME];
		for (j = 0; j < BUFFERFRAME; j++)
		{
			input[i][j] = 0.0;
		}
	}

	ProcBuffers *proc;
	proc = new ProcBuffers();

	RtAudio adac;
	if (adac.getDeviceCount() < 1) {
		std::cout << "\nNo audio devices found!\n";
		exit(1);
	}
	channels = 2;
	fs = 48000;

	adac.showWarnings(true);

	// Set the same number of channels for both input and output.
	unsigned int bufferFrames = 512;
	RtAudio::StreamParameters iParams, oParams;
	iParams.deviceId = iDevice;
	iParams.nChannels = channels;
	iParams.firstChannel = iOffset;
	oParams.deviceId = oDevice;
	oParams.nChannels = channels;
	oParams.firstChannel = oOffset;

	if (iDevice == 0)
		iParams.deviceId = adac.getDefaultInputDevice();
	if (oDevice == 0)
		oParams.deviceId = adac.getDefaultOutputDevice();

	RtAudio::StreamOptions options;
	//options.flags |= RTAUDIO_NONINTERLEAVED;

	InputData data;
	data.in_buffer = 0;
	data.out_buffer = 0;
	data.z_buffer = 0;

	//���� inout�̶�� callback�Լ��� ��Ʈ���� �ϱ� ���� ���� argument�� �Է��ϰ� open�Ѵ�.
	try {
		adac.openStream(&oParams, &iParams, FORMAT, fs, &bufferFrames, &inout, (void *)&data, &options);
	}
	catch (RtAudioError& e) {
		std::cout << '\n' << e.getMessage() << '\n' << std::endl;
		exit(1);
	}

	data.bufferBytes = bufferFrames * channels * sizeof(MY_TYPE);
	data.totalFrames = (unsigned long)(fs * time);
	data.iframeCounter = 0;
	data.oframeCounter = 0;
	data.channels = channels;
	unsigned long totalBytes;
	totalBytes = data.totalFrames * channels * sizeof(MY_TYPE);

	// Allocate the entire data buffer before starting stream.
	data.in_buffer = (MY_TYPE*)malloc(totalBytes);
	data.out_buffer = (MY_TYPE*)malloc(totalBytes);
	data.z_buffer = new MY_TYPE[NBufferSize * channels];
	for ( i = 0; i <NBufferSize * channels; i++)
	{
		data.z_buffer[i] = 0.0;
	}

	if (data.in_buffer == 0 || data.out_buffer == 0) {
		std::cout << "Memory allocation error ... quitting!\n";
		goto cleanup;
	}

	// Test RtAudio functionality for reporting latency.
	std::cout << "\nStream latency = " << adac.getStreamLatency() << " frames" << std::endl;

	//�غ�� callback�Լ��� streaming �����Ѵ�.
	try {
		adac.startStream();

	}
	catch (RtAudioError& e) {
		std::cout << '\n' << e.getMessage() << '\n' << std::endl;
		goto cleanup;
	}

	//streaming�� ���鼭 ���� callback(inout)�Լ����� 
	while (adac.isStreamRunning())
	{
		if (record_num)
		{
			if (in_buffer_cnt >= fs * time * channels)
			{
				in_buffer_cnt = 0;
			}
			for (ch = 0; ch < channels; ch++)
			{
				for (i = 0; i < bufferFrames; i++)
				{
					input[ch][i] = (data.in_buffer[channels*i + ch + in_buffer_cnt]) / 32768.0; //input�ڷ����� �°� ��ȯ�Ͽ� ����
				}
			}
			in_buffer_cnt += bufferFrames * channels;
			
			//Process���� ��ȭ���������� proc_end�� 1�� return�Ѵ�. ��ȭ������ �ƴϸ� 0�� return�Ѵ�.
			proc_end = proc->Process(input, proc_output);
			//��ȭ�������� ó���� �����Ϳ� ���� ����� ���� callback�Լ��� outbuffer�� �־��ش�.
			if (proc_end == 1)
			{
				if (out_buffer_cnt >= fs * time * channels)
				{
					out_buffer_cnt = 0;
				}
				for (ch = 0; ch < channels; ch++)
				{
					for (i = 0; i < 3 * bufferFrames; i++)
					{
						data.out_buffer[channels*i + ch + out_buffer_cnt] = (MY_TYPE)proc_output[ch][i]; //input�ڷ����� �°� ��ȯ�Ͽ� ����
					}
				}
				out_buffer_cnt += 3 * bufferFrames * channels;
				copyend += 3;
			}
			record_num--;
		}
		else
		{
			SLEEP(16);
		}
	}



	delete proc;
	for (i = 0; i < CHANNEL; i++)
	{
		delete[] input[i];
	}
	delete[] input;

cleanup:
	if (adac.isStreamOpen()) adac.closeStream();

	return 0;
}
