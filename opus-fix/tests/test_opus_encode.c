/* Copyright (c) 2011-2013 Xiph.Org Foundation
Written by Gregory Maxwell */
/*
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <time.h>
#if (!defined WIN32 && !defined _WIN32) || defined(__MINGW32__)
#include <unistd.h>
#else
#include <process.h>
#define getpid _getpid
#endif
#include "opus_multistream.h"
#include "opus.h"
#include "../src/opus_private.h"
#include "test_opus_common.h"
#include "../silk/fixed/main_FIX.h"
#include "../silk/API.h"

#define MAX_PACKET (1500)
#define SAMPLES (48000*30)
#define SSAMPLES (SAMPLES/3)
#define MAX_FRAME_SAMP (5760)

#define PI (3.141592653589793238462643f)

void generate_music(short *buf, opus_int32 len)
{
	opus_int32 a1, b1, a2, b2;
	opus_int32 c1, c2, d1, d2;
	opus_int32 i, j;
	a1 = b1 = a2 = b2 = 0;
	c1 = c2 = d1 = d2 = 0;
	j = 0;
	/*60ms silence*/
	//for(i=0;i<2880;i++)buf[i*2]=buf[i*2+1]=0;
	for (i = 0; i<len; i++)
	{
		opus_uint32 r;
		opus_int32 v1, v2;
		v1 = v2 = (((j*((j >> 12) ^ ((j >> 10 | j >> 12) & 26 & j >> 7))) & 128) + 128) << 15;
		r = fast_rand(); v1 += r & 65535; v1 -= r >> 16;
		r = fast_rand(); v2 += r & 65535; v2 -= r >> 16;
		b1 = v1 - a1 + ((b1 * 61 + 32) >> 6); a1 = v1;
		b2 = v2 - a2 + ((b2 * 61 + 32) >> 6); a2 = v2;
		c1 = (30 * (c1 + b1 + d1) + 32) >> 6; d1 = b1;
		c2 = (30 * (c2 + b2 + d2) + 32) >> 6; d2 = b2;
		v1 = (c1 + 128) >> 8;
		v2 = (c2 + 128) >> 8;
		buf[i * 2] = v1>32767 ? 32767 : (v1<-32768 ? -32768 : v1);
		buf[i * 2 + 1] = v2>32767 ? 32767 : (v2<-32768 ? -32768 : v2);
		if (i % 6 == 0)j++;
	}
}

#if 0
static int save_ctr = 0;
static void int_to_char(opus_uint32 i, unsigned char ch[4])
{
	ch[0] = i >> 24;
	ch[1] = (i >> 16) & 0xFF;
	ch[2] = (i >> 8) & 0xFF;
	ch[3] = i & 0xFF;
}

static OPUS_INLINE void save_packet(unsigned char* p, int len, opus_uint32 rng)
{
	FILE *fout;
	unsigned char int_field[4];
	char name[256];
	snprintf(name, 255, "test_opus_encode.%llu.%d.bit", (unsigned long long)iseed, save_ctr);
	fprintf(stdout, "writing %d byte packet to %s\n", len, name);
	fout = fopen(name, "wb+");
	if (fout == NULL)test_failed();
	int_to_char(len, int_field);
	fwrite(int_field, 1, 4, fout);
	int_to_char(rng, int_field);
	fwrite(int_field, 1, 4, fout);
	fwrite(p, 1, len, fout);
	fclose(fout);
	save_ctr++;
}
#endif

int run_test1(int no_fuzz)
{
	static const int fsizes[6] = { 960 * 3,960 * 2,120,240,480,960 };
	static const char *mstrings[3] = { "    LP","Hybrid","  MDCT" };
	unsigned char mapping[256] = { 0,1,255 };
	unsigned char db62[36];
	opus_int32 i;
	int rc, j, err;
	OpusEncoder *enc;
	OpusMSEncoder *MSenc;
	OpusDecoder *dec;
	OpusMSDecoder *MSdec;
	OpusMSDecoder *MSdec_err;
	OpusDecoder *dec_err[10];
	short *inbuf;
	short *outbuf;
	short *out2buf;
	opus_int32 bitrate_bps;
	unsigned char packet[MAX_PACKET + 257];
	opus_uint32 enc_final_range;
	opus_uint32 dec_final_range;
	int fswitch;
	int fsize;
	int count;

	/*FIXME: encoder api tests, fs!=48k, mono, VBR*/

	fprintf(stdout, "  Encode+Decode tests.\n");

	enc = opus_encoder_create(48000, 2, OPUS_APPLICATION_VOIP, &err);
	if (err != OPUS_OK || enc == NULL)test_failed();

	for (i = 0; i<2; i++)
	{
		int *ret_err;
		ret_err = i ? 0 : &err;
		MSenc = opus_multistream_encoder_create(8000, 2, 2, 0, mapping, OPUS_UNIMPLEMENTED, ret_err);
		if ((ret_err && *ret_err != OPUS_BAD_ARG) || MSenc != NULL)test_failed();

		MSenc = opus_multistream_encoder_create(8000, 0, 1, 0, mapping, OPUS_APPLICATION_VOIP, ret_err);
		if ((ret_err && *ret_err != OPUS_BAD_ARG) || MSenc != NULL)test_failed();

		MSenc = opus_multistream_encoder_create(44100, 2, 2, 0, mapping, OPUS_APPLICATION_VOIP, ret_err);
		if ((ret_err && *ret_err != OPUS_BAD_ARG) || MSenc != NULL)test_failed();

		MSenc = opus_multistream_encoder_create(8000, 2, 2, 3, mapping, OPUS_APPLICATION_VOIP, ret_err);
		if ((ret_err && *ret_err != OPUS_BAD_ARG) || MSenc != NULL)test_failed();

		MSenc = opus_multistream_encoder_create(8000, 2, -1, 0, mapping, OPUS_APPLICATION_VOIP, ret_err);
		if ((ret_err && *ret_err != OPUS_BAD_ARG) || MSenc != NULL)test_failed();

		MSenc = opus_multistream_encoder_create(8000, 256, 2, 0, mapping, OPUS_APPLICATION_VOIP, ret_err);
		if ((ret_err && *ret_err != OPUS_BAD_ARG) || MSenc != NULL)test_failed();
	}

	MSenc = opus_multistream_encoder_create(8000, 2, 2, 0, mapping, OPUS_APPLICATION_AUDIO, &err);
	if (err != OPUS_OK || MSenc == NULL)test_failed();

	/*Some multistream encoder API tests*/
	if (opus_multistream_encoder_ctl(MSenc, OPUS_GET_BITRATE(&i)) != OPUS_OK)test_failed();
	if (opus_multistream_encoder_ctl(MSenc, OPUS_GET_LSB_DEPTH(&i)) != OPUS_OK)test_failed();
	if (i<16)test_failed();

	{
		OpusEncoder *tmp_enc;
		if (opus_multistream_encoder_ctl(MSenc, OPUS_MULTISTREAM_GET_ENCODER_STATE(1, &tmp_enc)) != OPUS_OK)test_failed();
		if (opus_encoder_ctl(tmp_enc, OPUS_GET_LSB_DEPTH(&j)) != OPUS_OK)test_failed();
		if (i != j)test_failed();
		if (opus_multistream_encoder_ctl(MSenc, OPUS_MULTISTREAM_GET_ENCODER_STATE(2, &tmp_enc)) != OPUS_BAD_ARG)test_failed();
	}

	dec = opus_decoder_create(48000, 2, &err);
	if (err != OPUS_OK || dec == NULL)test_failed();

	MSdec = opus_multistream_decoder_create(48000, 2, 2, 0, mapping, &err);
	if (err != OPUS_OK || MSdec == NULL)test_failed();

	MSdec_err = opus_multistream_decoder_create(48000, 3, 2, 0, mapping, &err);
	if (err != OPUS_OK || MSdec_err == NULL)test_failed();

	dec_err[0] = (OpusDecoder *)malloc(opus_decoder_get_size(2));
	memcpy(dec_err[0], dec, opus_decoder_get_size(2));
	dec_err[1] = opus_decoder_create(48000, 1, &err);
	dec_err[2] = opus_decoder_create(24000, 2, &err);
	dec_err[3] = opus_decoder_create(24000, 1, &err);
	dec_err[4] = opus_decoder_create(16000, 2, &err);
	dec_err[5] = opus_decoder_create(16000, 1, &err);
	dec_err[6] = opus_decoder_create(12000, 2, &err);
	dec_err[7] = opus_decoder_create(12000, 1, &err);
	dec_err[8] = opus_decoder_create(8000, 2, &err);
	dec_err[9] = opus_decoder_create(8000, 1, &err);
	for (i = 0; i<10; i++)if (dec_err[i] == NULL)test_failed();

	{
		OpusEncoder *enccpy;
		/*The opus state structures contain no pointers and can be freely copied*/
		enccpy = (OpusEncoder *)malloc(opus_encoder_get_size(2));
		memcpy(enccpy, enc, opus_encoder_get_size(2));
		memset(enc, 255, opus_encoder_get_size(2));
		opus_encoder_destroy(enc);
		enc = enccpy;
	}

	inbuf = (short *)malloc(sizeof(short)*SAMPLES * 2);
	outbuf = (short *)malloc(sizeof(short)*SAMPLES * 2);
	out2buf = (short *)malloc(sizeof(short)*MAX_FRAME_SAMP * 3);
	if (inbuf == NULL || outbuf == NULL || out2buf == NULL)test_failed();

	generate_music(inbuf, SAMPLES);

	/*   FILE *foo;
	foo = fopen("foo.sw", "wb+");
	fwrite(inbuf, 1, SAMPLES*2*2, foo);
	fclose(foo);*/

	if (opus_encoder_ctl(enc, OPUS_SET_BANDWIDTH(OPUS_AUTO)) != OPUS_OK)test_failed();
	if (opus_encoder_ctl(enc, OPUS_SET_FORCE_MODE(-2)) != OPUS_BAD_ARG)test_failed();

	for (rc = 0; rc<3; rc++)
	{
		if (opus_encoder_ctl(enc, OPUS_SET_VBR(rc<2)) != OPUS_OK)test_failed();
		if (opus_encoder_ctl(enc, OPUS_SET_VBR_CONSTRAINT(rc == 1)) != OPUS_OK)test_failed();
		if (opus_encoder_ctl(enc, OPUS_SET_VBR_CONSTRAINT(rc == 1)) != OPUS_OK)test_failed();
		if (opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(rc == 0)) != OPUS_OK)test_failed();
		for (j = 0; j<13; j++)
		{
			int rate;
			/*int modes[13] = { 0,0,0,1,1,1,1,2,2,2,2,2,2 };
			int rates[13] = { 6000,12000,48000,16000,32000,48000,64000,512000,13000,24000,48000,64000,96000 };
			int frame[13] = { 960 * 2,960,480,960,960,960,480,960 * 3,960 * 3,960,480,240,120 };*/
			int modes[13] = { 2,0,0,1,1,1,1,2,2,2,2,2,2 };
			int rates[13] = { 96000,12000,48000,16000,32000,48000,64000,512000,13000,24000,48000,64000,96000 };
			int frame[13] = { 120,960,480,960,960,960,480,960 * 3,960 * 3,960,480,240,120 };
			rate = rates[j] + fast_rand() % rates[j];
			count = i = 0;
			do {
				int bw, len, out_samples, frame_size;
				frame_size = frame[j];
				if ((fast_rand() & 255) == 0)
				{
					if (opus_encoder_ctl(enc, OPUS_RESET_STATE) != OPUS_OK)test_failed();
					if (opus_decoder_ctl(dec, OPUS_RESET_STATE) != OPUS_OK)test_failed();
					if ((fast_rand() & 1) != 0)
					{
						if (opus_decoder_ctl(dec_err[fast_rand() & 1], OPUS_RESET_STATE) != OPUS_OK)test_failed();
					}
				}
				if ((fast_rand() & 127) == 0)
				{
					if (opus_decoder_ctl(dec_err[fast_rand() & 1], OPUS_RESET_STATE) != OPUS_OK)test_failed();
				}
				if (fast_rand() % 10 == 0) {
					int complex = fast_rand() % 11;
					if (opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(complex)) != OPUS_OK)test_failed();
				}
				if (fast_rand() % 50 == 0)opus_decoder_ctl(dec, OPUS_RESET_STATE);
				if (opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(rc == 0)) != OPUS_OK)test_failed();
				if (opus_encoder_ctl(enc, OPUS_SET_FORCE_MODE(MODE_SILK_ONLY + modes[j])) != OPUS_OK)test_failed();
				if (opus_encoder_ctl(enc, OPUS_SET_DTX(fast_rand() & 1)) != OPUS_OK)test_failed();
				if (opus_encoder_ctl(enc, OPUS_SET_BITRATE(rate)) != OPUS_OK)test_failed();
				if (opus_encoder_ctl(enc, OPUS_SET_FORCE_CHANNELS((rates[j] >= 64000 ? 2 : 1))) != OPUS_OK)test_failed();
				if (opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY((count >> 2) % 11)) != OPUS_OK)test_failed();
				if (opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC((fast_rand() & 15)&(fast_rand() % 15))) != OPUS_OK)test_failed();
				bw = modes[j] == 0 ? OPUS_BANDWIDTH_NARROWBAND + (fast_rand() % 3) :
					modes[j] == 1 ? OPUS_BANDWIDTH_SUPERWIDEBAND + (fast_rand() & 1) :
					OPUS_BANDWIDTH_NARROWBAND + (fast_rand() % 5);
				if (modes[j] == 2 && bw == OPUS_BANDWIDTH_MEDIUMBAND)bw += 3;
				if (opus_encoder_ctl(enc, OPUS_SET_BANDWIDTH(bw)) != OPUS_OK)test_failed();
				len = opus_encode(enc, &inbuf[i << 1], frame_size, packet, MAX_PACKET);
				if (len<0 || len>MAX_PACKET)test_failed();
				if (opus_encoder_ctl(enc, OPUS_GET_FINAL_RANGE(&enc_final_range)) != OPUS_OK)test_failed();
				if ((fast_rand() & 3) == 0)
				{
					if (opus_packet_pad(packet, len, len + 1) != OPUS_OK)test_failed();
					len++;
				}
				if ((fast_rand() & 7) == 0)
				{
					if (opus_packet_pad(packet, len, len + 256) != OPUS_OK)test_failed();
					len += 256;
				}
				if ((fast_rand() & 3) == 0)
				{
					len = opus_packet_unpad(packet, len);
					if (len<1)test_failed();
				}
				out_samples = opus_decode(dec, packet, len, &outbuf[i << 1], MAX_FRAME_SAMP, 0);
				if (out_samples != frame_size)test_failed();
				if (opus_decoder_ctl(dec, OPUS_GET_FINAL_RANGE(&dec_final_range)) != OPUS_OK)test_failed();
				if (enc_final_range != dec_final_range)test_failed();
				/*LBRR decode*/
				out_samples = opus_decode(dec_err[0], packet, len, out2buf, frame_size, (fast_rand() & 3) != 0);
				if (out_samples != frame_size)test_failed();
				out_samples = opus_decode(dec_err[1], packet, (fast_rand() & 3) == 0 ? 0 : len, out2buf, MAX_FRAME_SAMP, (fast_rand() & 7) != 0);
				if (out_samples<120)test_failed();
				i += frame_size;
				count++;
			} while (i<(SSAMPLES - MAX_FRAME_SAMP));
			fprintf(stdout, "    Mode %s FB encode %s, %6d bps OK.\n", mstrings[modes[j]], rc == 0 ? " VBR" : rc == 1 ? "CVBR" : " CBR", rate);
		}
	}

	if (opus_encoder_ctl(enc, OPUS_SET_FORCE_MODE(OPUS_AUTO)) != OPUS_OK)test_failed();
	if (opus_encoder_ctl(enc, OPUS_SET_FORCE_CHANNELS(OPUS_AUTO)) != OPUS_OK)test_failed();
	if (opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(0)) != OPUS_OK)test_failed();
	if (opus_encoder_ctl(enc, OPUS_SET_DTX(0)) != OPUS_OK)test_failed();

	for (rc = 0; rc<3; rc++)
	{
		if (opus_multistream_encoder_ctl(MSenc, OPUS_SET_VBR(rc<2)) != OPUS_OK)test_failed();
		if (opus_multistream_encoder_ctl(MSenc, OPUS_SET_VBR_CONSTRAINT(rc == 1)) != OPUS_OK)test_failed();
		if (opus_multistream_encoder_ctl(MSenc, OPUS_SET_VBR_CONSTRAINT(rc == 1)) != OPUS_OK)test_failed();
		if (opus_multistream_encoder_ctl(MSenc, OPUS_SET_INBAND_FEC(rc == 0)) != OPUS_OK)test_failed();
		for (j = 0; j<16; j++)
		{
			int rate;
			int modes[16] = { 0,0,0,0,0,0,0,0,2,2,2,2,2,2,2,2 };
			int rates[16] = { 4000,12000,32000,8000,16000,32000,48000,88000,4000,12000,32000,8000,16000,32000,48000,88000 };
			int frame[16] = { 160 * 1,160,80,160,160,80,40,20,160 * 1,160,80,160,160,80,40,20 };
			if (opus_multistream_encoder_ctl(MSenc, OPUS_SET_INBAND_FEC(rc == 0 && j == 1)) != OPUS_OK)test_failed();
			if (opus_multistream_encoder_ctl(MSenc, OPUS_SET_FORCE_MODE(MODE_SILK_ONLY + modes[j])) != OPUS_OK)test_failed();
			rate = rates[j] + fast_rand() % rates[j];
			if (opus_multistream_encoder_ctl(MSenc, OPUS_SET_DTX(fast_rand() & 1)) != OPUS_OK)test_failed();
			if (opus_multistream_encoder_ctl(MSenc, OPUS_SET_BITRATE(rate)) != OPUS_OK)test_failed();
			count = i = 0;
			do {
				int pred, len, out_samples, frame_size, loss;
				if (opus_multistream_encoder_ctl(MSenc, OPUS_GET_PREDICTION_DISABLED(&pred)) != OPUS_OK)test_failed();
				if (opus_multistream_encoder_ctl(MSenc, OPUS_SET_PREDICTION_DISABLED((int)(fast_rand() & 15)<(pred ? 11 : 4))) != OPUS_OK)test_failed();
				frame_size = frame[j];
				if (opus_multistream_encoder_ctl(MSenc, OPUS_SET_COMPLEXITY((count >> 2) % 11)) != OPUS_OK)test_failed();
				if (opus_multistream_encoder_ctl(MSenc, OPUS_SET_PACKET_LOSS_PERC((fast_rand() & 15)&(fast_rand() % 15))) != OPUS_OK)test_failed();
				if ((fast_rand() & 255) == 0)
				{
					if (opus_multistream_encoder_ctl(MSenc, OPUS_RESET_STATE) != OPUS_OK)test_failed();
					if (opus_multistream_decoder_ctl(MSdec, OPUS_RESET_STATE) != OPUS_OK)test_failed();
					if ((fast_rand() & 3) != 0)
					{
						if (opus_multistream_decoder_ctl(MSdec_err, OPUS_RESET_STATE) != OPUS_OK)test_failed();
					}
				}
				if ((fast_rand() & 255) == 0)
				{
					if (opus_multistream_decoder_ctl(MSdec_err, OPUS_RESET_STATE) != OPUS_OK)test_failed();
				}
				len = opus_multistream_encode(MSenc, &inbuf[i << 1], frame_size, packet, MAX_PACKET);
				if (len<0 || len>MAX_PACKET)test_failed();
				if (opus_multistream_encoder_ctl(MSenc, OPUS_GET_FINAL_RANGE(&enc_final_range)) != OPUS_OK)test_failed();
				if ((fast_rand() & 3) == 0)
				{
					if (opus_multistream_packet_pad(packet, len, len + 1, 2) != OPUS_OK)test_failed();
					len++;
				}
				if ((fast_rand() & 7) == 0)
				{
					if (opus_multistream_packet_pad(packet, len, len + 256, 2) != OPUS_OK)test_failed();
					len += 256;
				}
				if ((fast_rand() & 3) == 0)
				{
					len = opus_multistream_packet_unpad(packet, len, 2);
					if (len<1)test_failed();
				}
				out_samples = opus_multistream_decode(MSdec, packet, len, out2buf, MAX_FRAME_SAMP, 0);
				if (out_samples != frame_size * 6)test_failed();
				if (opus_multistream_decoder_ctl(MSdec, OPUS_GET_FINAL_RANGE(&dec_final_range)) != OPUS_OK)test_failed();
				if (enc_final_range != dec_final_range)test_failed();
				/*LBRR decode*/
				loss = (fast_rand() & 63) == 0;
				out_samples = opus_multistream_decode(MSdec_err, packet, loss ? 0 : len, out2buf, frame_size * 6, (fast_rand() & 3) != 0);
				if (out_samples != (frame_size * 6))test_failed();
				i += frame_size;
				count++;
			} while (i<(SSAMPLES / 12 - MAX_FRAME_SAMP));
			fprintf(stdout, "    Mode %s NB dual-mono MS encode %s, %6d bps OK.\n", mstrings[modes[j]], rc == 0 ? " VBR" : rc == 1 ? "CVBR" : " CBR", rate);
		}
	}

	bitrate_bps = 512000;
	fsize = fast_rand() % 31;
	fswitch = 100;

	debruijn2(6, db62);
	count = i = 0;
	do {
		unsigned char toc;
		const unsigned char *frames[48];
		short size[48];
		int payload_offset;
		opus_uint32 dec_final_range2;
		int jj, dec2;
		int len, out_samples;
		int frame_size = fsizes[db62[fsize]];
		opus_int32 offset = i % (SAMPLES - MAX_FRAME_SAMP);

		opus_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate_bps));

		len = opus_encode(enc, &inbuf[offset << 1], frame_size, packet, MAX_PACKET);
		if (len<0 || len>MAX_PACKET)test_failed();
		count++;

		opus_encoder_ctl(enc, OPUS_GET_FINAL_RANGE(&enc_final_range));

		out_samples = opus_decode(dec, packet, len, &outbuf[offset << 1], MAX_FRAME_SAMP, 0);
		if (out_samples != frame_size)test_failed();

		opus_decoder_ctl(dec, OPUS_GET_FINAL_RANGE(&dec_final_range));

		/* compare final range encoder rng values of encoder and decoder */
		if (dec_final_range != enc_final_range)test_failed();

		/* We fuzz the packet, but take care not to only corrupt the payload
		Corrupted headers are tested elsewhere and we need to actually run
		the decoders in order to compare them. */
		if (opus_packet_parse(packet, len, &toc, frames, size, &payload_offset) <= 0)test_failed();
		if ((fast_rand() & 1023) == 0)len = 0;
		for (j = (frames[0] - packet); j<len; j++)for (jj = 0; jj<8; jj++)packet[j] ^= ((!no_fuzz) && ((fast_rand() & 1023) == 0)) << jj;
		out_samples = opus_decode(dec_err[0], len>0 ? packet : NULL, len, out2buf, MAX_FRAME_SAMP, 0);
		if (out_samples<0 || out_samples>MAX_FRAME_SAMP)test_failed();
		if ((len>0 && out_samples != frame_size))test_failed(); /*FIXME use lastframe*/

		opus_decoder_ctl(dec_err[0], OPUS_GET_FINAL_RANGE(&dec_final_range));

		/*randomly select one of the decoders to compare with*/
		dec2 = fast_rand() % 9 + 1;
		out_samples = opus_decode(dec_err[dec2], len>0 ? packet : NULL, len, out2buf, MAX_FRAME_SAMP, 0);
		if (out_samples<0 || out_samples>MAX_FRAME_SAMP)test_failed(); /*FIXME, use factor, lastframe for loss*/

		opus_decoder_ctl(dec_err[dec2], OPUS_GET_FINAL_RANGE(&dec_final_range2));
		if (len>0 && dec_final_range != dec_final_range2)test_failed();

		fswitch--;
		if (fswitch<1)
		{
			int new_size;
			fsize = (fsize + 1) % 36;
			new_size = fsizes[db62[fsize]];
			if (new_size == 960 || new_size == 480)fswitch = 2880 / new_size*(fast_rand() % 19 + 1);
			else fswitch = (fast_rand() % (2880 / new_size)) + 1;
		}
		bitrate_bps = ((fast_rand() % 508000 + 4000) + bitrate_bps) >> 1;
		i += frame_size;
	} while (i<SAMPLES * 4);
	fprintf(stdout, "    All framesize pairs switching encode, %d frames OK.\n", count);

	if (opus_encoder_ctl(enc, OPUS_RESET_STATE) != OPUS_OK)test_failed();
	opus_encoder_destroy(enc);
	if (opus_multistream_encoder_ctl(MSenc, OPUS_RESET_STATE) != OPUS_OK)test_failed();
	opus_multistream_encoder_destroy(MSenc);
	if (opus_decoder_ctl(dec, OPUS_RESET_STATE) != OPUS_OK)test_failed();
	opus_decoder_destroy(dec);
	if (opus_multistream_decoder_ctl(MSdec, OPUS_RESET_STATE) != OPUS_OK)test_failed();
	opus_multistream_decoder_destroy(MSdec);
	opus_multistream_decoder_destroy(MSdec_err);
	for (i = 0; i<10; i++)opus_decoder_destroy(dec_err[i]);
	free(inbuf);
	free(outbuf);
	free(out2buf);
	return 0;
}

typedef struct {
	silk_decoder_state          channel_state[DECODER_NUM_CHANNELS];
	stereo_dec_state                sStereo;
	opus_int                         nChannelsAPI;
	opus_int                         nChannelsInternal;
	opus_int                         prev_decode_only_middle;
} silk_decoder;

static const opus_int16 frame1[2880] = {
	3150, 1803, 766, 667, 1198, 1981, 2683, 2821, 2724,
	3362, 4588, 5423, 5932, 7040, 8527, 9392, 9351,
	9246, 9771, 10381, 10243, 9719, 9830, 10747, 11676,
	12049, 11923, 11834, 11854, 11609, 11126, 10870, 11050,
	11328, 11175, 10628, 10300, 10548, 10999, 11533, 12260,
	13003, 13552, 13884, 13912, 13649, 13347, 13143, 13162,
	13716, 14769, 15441, 15462, 15493, 15760, 15748, 15432,
	15430, 15874, 16116, 15668, 14781, 14201, 14143, 14369,
	14505, 14438, 14289, 14109, 13825, 13469, 13207, 13191,
	13304, 13219, 12645, 11945, 11556, 11303, 10729, 9907,
	9432, 9606, 10061, 10306, 10213, 9943, 9905, 10261,
	10624, 10425, 9619, 8484, 7473, 7318, 8163, 9033,
	9181, 8950, 8569, 7874, 7190, 7220, 7635, 7189,
	5604, 3950, 2750, 1695, 1258, 2034, 2750, 2279,
	1407, 909, 453, 90, 16, -370, -1417, -3082,
	-5610, -8294, -9525, -9540, -10072, -11778, -14215, -16975,
	-19234, -20033, -19517, -18574, -17965, -18401, -20349, -23409,
	-26320, -27794, -27289, -25528, -24272, -24947, -27191, -29176,
	-29241, -26990, -23618, -21129, -20305, -20232, -20416, -20992,
	-20959, -19245, -16837, -15431, -15421, -16186, -16567, -15563,
	-13571, -11732, -10349, -9627, -9954, -10622, -10483, -9577,
	-8537, -7706, -7447, -7805, -8249, -8151, -7247, -5469,
	-3324, -1717, -1014, -811, -489, 327, 1559, 2902,
	4120, 5255, 6036, 6233, 6400, 7416, 9045, 10084,
	10240, 10367, 10694, 10569, 9805, 9190, 9338, 10106,
	10654, 10234, 9150, 8367, 8288, 8521, 8540, 8196,
	7856, 7719, 7341, 6390, 5494, 5181, 4856, 3873,
	2710, 2237, 2444, 2528, 1992, 1252, 1042, 1559,
	2365, 2793, 2427, 1580, 982, 821, 884, 1511,
	2991, 4454, 4573, 3460, 2447, 2420, 2860, 2649,
	1622, 501, -720, -2593, -4473, -5117, -4614, -4179,
	-4342, -4728, -4799, -4505, -4304, -4279, -3830, -2776,
	-1827, -1615, -1932, -2160, -1906, -1305, -536, 396,
	1275, 1936, 2592, 3283, 3995, 5055, 6253, 6482,
	5690, 4806, 4088, 3470, 3227, 3125, 2668, 2182,
	2186, 2571, 3002, 3312, 3493, 3785, 4260, 4591,
	4641, 4813, 5393, 5863, 5550, 4913, 5006, 5873,
	6809, 7560, 8092, 8347, 8534, 8676, 8561, 8454,
	8556, 8451, 8055, 7656, 7063, 6226, 5748, 5882,
	6207, 6343, 6178, 5854, 5588, 5214, 4468, 3725,
	3232, 3047, 2949, 2951, 3272, 3876, 4125, 3527,
	2598, 2220, 2436, 2498, 1770, 615, -748, -2823,
	-5337, -6759, -6638, -6264, -6640, -8133, -11049, -14506,
	-16823, -17681, -17791, -17485, -17536, -19204, -22388, -25702,
	-28020, -28497, -26809, -24391, -23498, -24713, -26846, -28344,
	-27727, -24524, -20721, -19074, -19738, -20811, -21238, -20717,
	-18603, -15036, -11778, -10442, -11148, -12460, -12492, -10922,
	-9094, -7984, -7825, -8902, -10662, -11563, -10938, -9402,
	-7618, -6242, -5821, -5986, -6014, -5713, -4771, -2774,
	-465, 584, 197, -570, -733, 327, 2618, 5022,
	6526, 7563, 8672, 9501, 10316, 12069, 14428, 16071,
	16486, 15931, 14739, 13507, 12789, 12549, 12401, 12123,
	11810, 11528, 11058, 10439, 10155, 10281, 10319, 10019,
	9476, 8957, 8875, 9000, 8464, 7234, 6216, 5740,
	5708, 6290, 7362, 8259, 8478, 8011, 7227, 6676,
	6639, 7020, 7646, 8383, 8604, 7882, 6632, 5854,
	6101, 7100, 8352, 9408, 9821, 9468, 8500, 7210,
	6135, 6094, 6767, 6931, 6382, 5888, 5541, 4940,
	4318, 4227, 4668, 5169, 4999, 4046, 3072, 2915,
	3578, 4372, 4710, 4710, 4883, 5261, 5483, 5632,
	6315, 7291, 7761, 7391, 6746, 6564, 6921, 7248,
	7077, 6607, 6385, 6529, 6737, 6691, 6596, 6799,
	7415, 8232, 8767, 8664, 8108, 7582, 7351, 7525,
	8347, 9722, 10905, 11033, 10453, 10554, 11676, 12629,
	12750, 12465, 11777, 10364, 8865, 8161, 8179, 8269,
	8021, 7446, 6986, 7084, 7440, 7347, 6848, 6615,
	6742, 6587, 6197, 5825, 5283, 4625, 4273, 4002,
	3423, 3028, 3170, 3242, 3121, 3285, 3786, 3933,
	3486, 2555, 1123, -661, -2640, -5112, -7922, -9911,
	-10538, -10889, -11927, -13844, -16508, -18985, -20049, -20188,
	-20686, -21263, -21589, -22863, -25418, -27913, -29735, -30647,
	-29901, -28135, -27442, -28321, -29557, -30465, -30582, -29195,
	-26900, -25401, -25225, -25627, -26028, -26187, -25751, -24506,
	-22642, -20837, -20021, -20314, -20473, -19518, -18105, -17178,
	-16509, -15763, -15458, -15755, -15810, -14869, -13077, -11402,
	-10798, -10752, -9923, -8569, -7766, -7145, -6009, -4976,
	-4379, -3624, -2637, -1433, 444, 2491, 3708, 4437,
	5489, 6632, 7570, 8448, 9292, 10211, 11252, 11921,
	11978, 11902, 11928, 11951, 12301, 13026, 13471, 13378,
	13332, 13746, 14393, 14825, 14597, 13655, 12684, 12328,
	12536, 13250, 14464, 15299, 14965, 14258, 14176, 14459,
	14861, 15618, 16013, 15016, 13271, 11990, 11340, 10967,
	10814, 10788, 10849, 11130, 11282, 10785, 10038, 9731,
	9844, 9970, 10184, 10208, 9486, 8303, 7399, 6968,
	6986, 7338, 7416, 6849, 6342, 6422, 6897, 7434,
	7916, 8150, 7782, 6601, 5054, 4077, 3923, 3880,
	3510, 3203, 3123, 3153, 3291, 3731, 4704, 5996,
	6724, 6404, 5756, 5618, 5913, 6212, 6062, 5454,
	4868, 4553, 4197, 3825, 3917, 4448, 4909, 4931,
	4360, 3526, 2978, 2872, 2925, 3017, 3061, 2852,
	2471, 2183, 2099, 2389, 3212, 4189, 4600, 4266,
	3852, 3927, 4236, 4273, 4104, 4184, 4667, 5059,
	4842, 4553, 5083, 6004, 6077, 5288, 4417, 3727,
	3447, 3897, 4528, 4588, 4213, 3780, 3110, 2624,
	2931, 3732, 4135, 3759, 2832, 1734, 851, 344,
	-42, -682, -1707, -3423, -5757, -8091, -9420, -9462,
	-9087, -9276, -10345, -12346, -15030, -17360, -18430, -18706,
	-18783, -18778, -19264, -21028, -23476, -25103, -25172, -23840,
	-21812, -20523, -21296, -24000, -26960, -28379, -27598, -25482,
	-23741, -23560, -24557, -25380, -25313, -24486, -22953, -21014,
	-19735, -19676, -20224, -20660, -20591, -19809, -18697, -17962,
	-17789, -17973, -18198, -17902, -16808, -15155, -13479, -12190,
	-11293, -10466, -9727, -9314, -8865, -8032, -7415, -7542,
	-8057, -8045, -7038, -5271, -3681, -2761, -1853, -595,
	394, 973, 1826, 3252, 4653, 5220, 4546, 3472,
	3168, 3660, 4408, 5270, 6105, 6629, 6751, 6440,
	5962, 6025, 6735, 7220, 6959, 6291, 5542, 4736,
	3971, 3509, 3513, 3795, 3966, 3990, 4046, 4113,
	4090, 4130, 4352, 4516, 4434, 4542, 5371, 6450,
	6870, 6921, 7230, 7623, 7853, 8195, 8519, 8489,
	8407, 8450, 8289, 8090, 8341, 8846, 9420, 10059,
	10589, 10670, 10538, 10581, 10704, 10864, 11229, 11594,
	11640, 11285, 10469, 9223, 8460, 8926, 10096, 10861,
	10756, 10332, 10471, 11221, 11923, 12391, 12964, 13154,
	12598, 12115, 12614, 13772, 14727, 15121, 15094, 15041,
	15430, 16208, 16633, 16413, 15833, 15212, 14401, 13444,
	12691, 12246, 12112, 12327, 12540, 12446, 12341, 12364,
	12091, 11884, 12477, 13456, 13833, 13509, 12924, 12220,
	11506, 11148, 11378, 11994, 12412, 12167, 11309, 10500,
	10530, 11427, 12157, 11736, 10463, 9303, 8425, 7387,
	6136, 5287, 5125, 5194, 4881, 4249, 3832, 3872,
	4204, 4476, 4345, 4014, 3738, 3210, 2165, 1238,
	421, -1466, -4340, -6354, -6690, -6342, -6171, -6574,
	-8182, -10674, -12700, -13946, -15006, -15431, -14901, -15068,
	-17228, -20255, -22794, -24510, -24638, -22741, -20475, -19981,
	-21513, -23850, -25577, -25747, -24418, -22375, -20424, -19255,
	-19177, -19817, -20419, -20073, -18175, -15590, -14225, -14661,
	-15649, -16114, -15944, -14958, -13015, -11059, -10293, -10689,
	-11347, -11449, -10399, -8427, -6809, -6350, -6621, -7042,
	-7204, -6755, -5778, -5026, -4769, -4675, -4513, -4280,
	-3871, -2886, -1046, 1099, 2623, 3415, 3798, 3854,
	3621, 3720, 4662, 6085, 7134, 7374, 7073, 6809,
	7171, 8383, 9756, 10324, 9842, 8640, 7096, 5884,
	5531, 5572, 5293, 4700, 4066, 3563, 3298, 3007,
	2261, 1385, 995, 1011, 1053, 1052, 930, 490,
	-264, -1096, -1616, -1435, -690, 198, 1086, 1953,
	2547, 2712, 2648, 2629, 2807, 3207, 3509, 3209,
	1960, 304, -877, -1304, -1199, -627, 314, 1121,
	1354, 1017, 406, 232, 1035, 2379, 3261, 3307,
	2739, 1866, 1228, 1333, 2070, 3052, 4056, 4783,
	4977, 4785, 4733, 5148, 5856, 6471, 6711, 6436,
	5820, 5332, 5112, 4872, 4758, 5131, 5896, 6374,
	6139, 5636, 5561, 5703, 5274, 4349, 3733, 3659,
	3873, 4250, 4616, 4793, 4974, 5121, 4777, 4222,
	4359, 5128, 5685, 5769, 5685, 5637, 5815, 6190,
	6523, 6966, 7788, 8535, 8470, 7616, 6545, 5731,
	5639, 6190, 6578, 5893, 4250, 2526, 1533, 1398,
	1505, 1406, 1315, 1308, 1327, 1537, 2007, 2035,
	1266, 218, -504, -773, -868, -1285, -2553, -4784,
	-7047, -7955, -7310, -6597, -7233, -9557, -13053, -16442,
	-18586, -19629, -20118, -20128, -20355, -21749, -23786, -25412,
	-26555, -26819, -25464, -23363, -22301, -22805, -24143, -25341,
	-25612, -24577, -22411, -20186, -19029, -18831, -18777, -18502,
	-17550, -15359, -12543, -10456, -9668, -9884, -10317, -10274,
	-9723, -9036, -8316, -7770, -7863, -8415, -8516, -7448,
	-5283, -3062, -1970, -1721, -1115, -108, 851, 2014,
	3251, 3910, 4150, 4516, 5138, 6226, 7895, 9579,
	10915, 12035, 13030, 13777, 14403, 15248, 16150, 16527,
	16306, 15898, 15417, 14748, 14156, 13997, 14384, 14898,
	14812, 14017, 13179, 12705, 12318, 11748, 11115, 10603,
	10214, 9844, 9216, 8338, 7715, 7399, 6699, 5475,
	4427, 3942, 3715, 3246, 2463, 1859, 1795, 1707,
	1105, 550, 715, 1416, 2146, 2546, 2186, 1071,
	260, 350, 889, 1169, 753, -327, -1256, -1490,
	-1578, -1775, -1609, -1257, -1145, -1091, -664, -170,
	-235, -853, -1425, -1554, -1188, -444, 245, 630,
	1104, 2065, 3308, 4601, 5614, 5989, 5881, 5687,
	5481, 5360, 5531, 5742, 5536, 5211, 5207, 5409,
	5518, 5553, 5687, 6155, 6879, 7318, 7373, 7440,
	7423, 7137, 6884, 6997, 7533, 8313, 8731, 8428,
	7940, 8067, 9047, 10317, 11083, 11358, 11653, 11644,
	10832, 9680, 9039, 9141, 9643, 10024, 9966, 9784,
	10021, 10390, 10202, 9949, 10350, 10860, 10762, 10299,
	9766, 9078, 8257, 7477, 6927, 6769, 6661, 6353,
	5941, 5779, 5813, 5519, 4918, 4597, 4442, 3796,
	2696, 1553, 407, -1237, -3713, -6660, -8762, -9248,
	-9028, -9468, -10847, -12913, -15729, -18783, -20840, -21688,
	-21887, -21742, -22100, -24017, -26597, -28045, -28306, -28184,
	-27695, -26939, -26799, -27864, -29544, -30489, -29856, -28051,
	-26217, -25158, -24893, -24908, -24503, -23424, -22163, -21162,
	-20270, -19614, -19295, -19137, -19174, -19175, -18336, -16769,
	-15580, -15076, -14686, -14135, -13493, -12460, -10538, -8097,
	-6420, -5886, -5604, -5037, -4121, -2643, -947, 201,
	753, 1492, 2914, 5133, 7805, 9961, 10861, 10867,
	10705, 10688, 11140, 12136, 12929, 13115, 12965, 12659,
	12312, 12236, 12586, 13321, 14208, 14752, 14711, 14251,
	13653, 13289, 13230, 13261, 13402, 13614, 13548, 13205,
	12943, 13023, 13570, 14270, 14473, 14254, 14086, 14011,
	13742, 13251, 12619, 11918, 11372, 10958, 10291, 9632,
	9713, 10421, 10860, 10928, 10850, 10686, 10746, 11178,
	11601, 11618, 11075, 10108, 9095, 8342, 8222, 8651,
	9134, 9377, 9350, 8910, 8340, 8227, 8500, 8680,
	8465, 7778, 7005, 6170, 4906, 3741, 3871, 5124,
	6093, 6152, 5832, 5706, 5882, 6179, 6403, 6522,
	6835, 7229, 7141, 6571, 6097, 6023, 6222, 6321,
	6049, 5619, 5207, 4548, 3599, 2915, 2880, 3184,
	3178, 2657, 1980, 1515, 1452, 1703, 1775, 1362,
	547, -259, -445, 22, 228, -324, -1151, -1907,
	-2601, -2986, -2729, -2124, -1766, -1654, -1253, -712,
	-584, -758, -991, -1245, -1119, -565, -285, -453,
	-894, -1286, -1161, 15, 1761, 2829, 2715, 1847,
	539, -919, -1770, -1765, -1922, -2654, -3763, -5220,
	-7037, -8999, -11033, -12869, -13694, -13351, -13114, -14132,
	-16307, -18881, -21363, -23198, -24149, -24400, -24079, -23992,
	-25430, -28002, -29879, -30238, -29628, -28617, -27485, -26532,
	-26444, -27667, -29308, -29783, -28578, -26655, -25366, -25127,
	-25158, -24618, -23511, -22277, -20940, -19183, -17333, -16265,
	-16105, -16185, -15981, -15195, -13762, -12224, -11134, -10651,
	-10531, -10282, -9502, -8114, -6319, -4579, -3360, -2816,
	-2730, -2664, -1939, -882, -557, -1018, -1007, -71,
	765, 1210, 2218, 3782, 4950, 5624, 5964, 6015,
	6238, 7084, 8016, 8297, 7965, 7604, 7605, 7800,
	8035, 8509, 8969, 8740, 7672, 6447, 5891, 6234,
	6922, 7177, 6742, 6100, 5813, 5703, 5415, 4979,
	4357, 3330, 2374, 2048, 1873, 1240, 435, -205,
	-878, -1365, -1101, -371, -38, 303, 1329, 2524,
	3328, 3903, 4259, 4329, 4452, 4879, 5525, 6417,
	7052, 7043, 6855, 7423, 8942, 10767, 12274, 13126,
	13262, 13100, 12889, 12559, 12152, 11955, 11687, 11155,
	10666, 10304, 10105, 10399, 11087, 11629, 11876, 12086,
	12571, 13285, 14148, 14959, 15355, 15368, 15539, 15933,
	16071, 16074, 16509, 17245, 17851, 18143, 18385, 18707,
	18940, 18779, 18133, 17418, 17098, 16877, 16145, 15014,
	14261, 14046, 13984, 13650, 13053, 12524, 12308, 12463,
	12801, 12885, 12520, 11830, 11021, 10345, 10240, 10661,
	11118, 11305, 11369, 11535, 11600, 11428, 11466, 11732,
	11581, 11110, 11015, 10904, 9999, 8580, 7457, 6966,
	6886, 6806, 6606, 6489, 6566, 6504, 5936, 4777,
	3540, 2803, 2448, 1943, 1097, -29, -1610, -3941,
	-6754, -8931, -9402, -8867, -9184, -11156, -14036, -16612,
	-18090, -18680, -19056, -19131, -18886, -19389, -21130, -23073,
	-24441, -25232, -25206, -24354, -23687, -24258, -25947, -27785,
	-28839, -28413, -26408, -24225, -23612, -24403, -24996, -24507,
	-22968, -20618, -18114, -16504, -16467, -17730, -19024, -18880,
	-17120, -14776, -12817, -11575, -10938, -10487, -9849, -8898,
	-7467, -5551, -3876, -3406, -3965, -4676, -4920, -4624,
	-3980, -3170, -2421, -2058, -2125, -2163, -1563, -266,
	1433, 2861, 3635, 3805, 3569, 3380, 3906, 5054,
	5689, 5142, 4161, 3509, 3305, 3471, 4127, 5110,
	5867, 5719, 4419, 2821, 2077, 2112, 2048, 1582,
	941, 291, -270, -693, -1102, -1627, -2134, -2410,
	-2511, -2510, -2315, -2203, -2540, -3128, -3487, -3650,
	-3656, -3138, -1956, -580, 339, 637, 819, 1216,
	1614, 1893, 2554, 3530, 3962, 3483, 2622, 2080,
	2301, 3150, 3918, 4282, 4921, 6094, 7089, 7421,
	7578, 7990, 8431, 8607, 8403, 7917, 7806, 8535,
	9442, 9968, 10501, 11049, 10970, 10451, 10290, 10792,
	11864, 13079, 13704, 13576, 13219, 13122, 13430, 13886,
	13943, 13565, 13184, 12897, 12593, 12330, 12016, 11396,
	10454, 9527, 8928, 8636, 8270, 7637, 6980, 6534,
	6178, 5588, 4723, 3975, 3488, 2934, 2424, 2346,
	2412, 2006, 1305, 913, 1055, 1749, 2966, 4331,
	5249, 5470, 5086, 4289, 3324, 2603, 2226, 1720,
	789, -182, -710, -727, -547, -457, -565, -588,
	-392, 198, 1172, 1742, 1223, 214, -360, -605,
	-1175, -2288, -3744, -5801, -8750, -11695, -12729, -11780,
	-11152, -12412, -15060, -18038, -20526, -22112, -23042, -23433,
	-23245, -23522, -25438, -28079, -29557, -29550, -28841, -27790,
	-26567, -25681, -25627, -26218, -26486, -25248, -22594, -20090,
	-18950, -18654, -17849, -16107, -14091, -12139, -9829, -7505,
	-6515, -7155, -7639, -6619, -4775, -3254, -2283, -1605,
	-952, -195, 474, 1101, 2323, 4439, 6406, 7179,
	7194, 7515, 8226, 8797, 9232, 9636, 9608, 9289,
	9409, 10306, 11354, 12307, 13310, 14070, 14218, 13941,
	13808, 14334, 15435, 16167, 15539, 14054, 13037, 12796,
	12283, 11063, 10177, 10073, 9560, 8004, 6412, 5484,
	4767, 3840, 2766, 1769, 1226, 997, 295, -893,
	-1654, -1906, -2571, -3863, -5143, -6074, -6911, -7631,
	-8157, -8586, -8808, -8803, -8943, -8997, -8013, -6086,
	-4649, -4231, -4209, -4011, -3904, -4249, -4828, -5185,
	-5280, -5336, -5405, -5418, -5213, -4615, -3782, -3170,
	-2563, -1312, 636, 2553, 3698, 4107, 4386, 4693,
	4733, 4592, 4549, 4527, 4646, 5222, 6116, 7071,
	8202, 9333, 9806, 9579, 9528, 10090, 10721, 10893,
	10682, 10266, 9841, 9561, 9174, 8568, 8212, 8286,
	8487, 8768, 9129, 9431, 9629, 9635, 9401, 9358,
	9652, 9823, 9968, 10485, 10809, 10322, 9651, 9592,
	9627, 9176, 8786, 8931, 8972, 8521, 7992, 7339,
	6545, 6346, 6619, 6558, 6639, 7473, 8334, 8761,
	8832, 8234, 7349, 7024, 6830, 6209, 5681, 5550,
	5361, 5295, 5378, 5620, 6379, 7327, 7400, 6426,
	5381, 5092, 5475, 5829, 5398, 4169, 2474, -21,
	-4010, -8207, -9738, -8518, -7814, -9505, -12789, -16749,
	-20554, -23085, -24034, -23399, -21748, -21559, -24480, -28448,
	-30809, -31321, -30471, -28438, -26273, -25743, -27207, -28965,
	-29007, -26785, -23228, -20194, -19223, -19540, -19045, -17243,
	-14766, -11536, -8330, -6841, -7606, -9542, -11139, -11420,
	-10587, -9443, -8360, -7579, -7404, -7650, -7605, -6797,
	-5213, -3331, -1528, 295, 2021, 3134, 3596, 4082,
	5144, 6078, 6127, 5957, 6769, 8160, 9233, 10264,
	11580, 12898, 14070, 14791, 14891, 15234, 16279, 16809,
	15752, 13891, 12547, 11808, 11005, 10134, 9660, 9408,
	8791, 7665, 6580, 6287, 6722, 6886, 6417, 5765,
	4938, 4149, 4128, 4523, 4402, 3848, 3257, 2411,
	1780, 1828, 2162, 2443, 2644, 2623, 2467, 2451,
	2707, 3349, 4193, 4940, 5196, 5207, 5634, 6790,
	8100, 9054, 9800, 10204, 10089, 10092, 10551, 11156,
	11847, 12737, 13522, 14245, 15152, 16042, 16772, 17358,
	17482, 17101, 16842, 17002, 16973, 16301, 15594, 15521,
	15593, 15166, 14540, 14171, 14140, 14367, 14348, 13796,
	13234, 12862, 12086, 11004, 10198, 9636, 9047, 8199,
	6842, 5342, 4293, 3635, 3140, 2697, 1898, 622,
	-563, -1140, -1064, -783, -1112, -2281, -3467, -3899,
	-3667, -3454, -3554, -3828, -4319, -5322, -6765, -8169,
	-9150, -9734, -10400, -11374, -12062, -12038, -11910, -12148,
	-12256, -11838, -11232, -10862, -10720, -10448, -9898, -9526,
	-9628, -9943, -9733, -9041, -8395, -7920, -7181, -5909,
	-4440, -3242, -2316, -1501, -944, -1038, -1503, -2098,
	-3483, -6624, -11030, -14587, -15781, -15468, -15773, -17788,
	-20907, -23974, -26122, -27284, -27407, -26205, -24481, -24269,
	-26360, -29131, -30616, -30355, -29160, -28024, -27280, -26875,
	-27007, -27305, -26461, -23833, -20447, -17893, -16843, -16542,
	-15782, -14300, -12305, -9850, -7482, -6313, -6478, -6907,
	-6678, -5751, -4572, -3422, -2351, -1668, -1817, -2301,
	-2448, -2024, -1088, 9, 926, 2066, 3774, 5212,
	5495, 5172, 5387, 6083, 6478, 6234, 6278, 7448,
	9069, 9979, 10144, 10170, 10201, 10041, 9823, 10033,
	11003, 11892, 11372, 9424, 7310, 5713, 4264, 2806,
	1743, 1219, 819, 45, -1202, -2410, -3001, -3080,
	-3195, -3437, -3409, -3154, -3309, -3963, -4297, -4022,
	-3646, -3732, -4042, -4011, -3257, -2201, -1674, -1521,
	-1132, -997, -1631, -2234, -1695, -220, 1681, 3277,
	3839, 3708, 3868, 4498, 5331, 6594, 8241, 9344,
	9517, 9424, 9876, 10690, 11390, 12059, 12966, 14128,
	15472, 16876, 18269, 19654, 20786, 21231, 21144, 21009,
	20913, 20693, 20327, 19976, 19924, 20096, 19951, 19398,
	18951, 18852, 18967, 19388, 19915, 20138, 20292, 20498,
	19922, 18411, 17242, 16857, 16241, 15260, 14617, 14282,
	13783, 13168, 12432, 11534, 11041, 11315, 11718, 11572,
	10952, 10377, 9729, 8701, 7401, 6409, 6083, 6245,
	6433, 6240, 5698, 5181, 4652, 3854, 3282, 3360,
	3661, 3541, 3060, 2610, 2368, 2353, 2626, 3063,
	2932, 1704, 74, -695, -593, -277, 154, 789,
	1390, 1709, 1730, 1846, 2598, 3876, 4873, 5419,
	5732, 5548, 4560, 3217, 2114, 1202, 247, -1095,
	-3816, -8139, -11830, -12806, -12142, -12138, -13481, -16144,
	-19739, -22673, -23770, -23584, -23053, -22986, -24228, -26596,
	-28685, -29508, -28828, -26920, -24777, -23536, -23573, -24287,
	-24429, -23221, -21103, -18974, -17638, -17534, -17875, -17190,
	-15370, -13368, -11382, -9435, -8462, -8994, -9997, -10133,
	-9216, -7773, -6297, -5350, -5305, -5802, -6299, -6597,
	-6312, -5140, -3679, -2626, -2078, -1684, -1535, -1538,
	-1234, -604, -358, -794, -1228, -1136, -537, 682,
	1954, 2582, 3052, 4160, 5073, 5087, 5144, 5592,
	5438, 4440, 3413, 2796, 2044, 474, -1647, -3084,
	-3266, -2851, -2658, -2915, -3118, -2778, -2536, -3321,
	-4719, -5566, -5773, -6139, -7273, -8782, -9715, -9929,
	-9929, -9754, -9328, -8906, -8607, -8298, -7948, -6922,
	-4983, -3169, -2196, -1461, -562, 222, 1124, 2596,
	4039, 4704, 4894, 5540, 6619, 7427, 7809, 8273,
	9314, 10882, 12368, 13321, 13956, 14632, 15468, 16166,
	16486, 16594, 16718, 16598, 16182, 16169, 16778, 17163,
	16696, 15732, 14926, 14696, 15127, 16204, 17723, 18933,
	19069, 18319, 17493, 17123, 17198, 17262, 16916, 16286,
	15665, 15039, 14596, 14748, 15308, 15547, 15123, 14348,
	13636, 13003, 12234, 11356, 10357, 9099, 7521, 5970,
	4871, 4088, 3212, 2399, 2030, 1822, 1307, 713,
	219, -264, -724, -1002, -1004, -733, -531, -925,
	-1907, -2804, -2900, -2188, -1348, -873, -704, -848,
	-1363, -1828, -1827, -1515, -1241, -1186, -1386 };


	static const opus_int16 frame2[2880] = {
	-1677, -2024, -1924, -935, 564, 1599, 1770, 1388, 943,
	729, 527, 94, -536, -2312, -6181, -10574, -12897,
	-13371, -13901, -15052, -16729, -19117, -21735, -23802, -25100,
	-25344, -24562, -24173, -25610, -28183, -29969, -30170, -29309,
	-27965, -26525, -25577, -25262, -24737, -23328, -21193, -18627,
	-16093, -14483, -13833, -12977, -11297, -9050, -6212, -3090,
	-1162, -1380, -2680, -3147, -2194, -756, 388, 1425,
	2314, 2545, 2479, 2912, 3700, 4321, 4976, 5496,
	5802, 6487, 7369, 7437, 6913, 6731, 6652, 6213,
	5966, 5958, 5936, 5930, 6196, 6755, 7695, 8769,
	9028, 8066, 6896, 6628, 7147, 7600, 7317, 6245,
	4567, 2436, 308, -784, -264, 956, 1539, 1131,
	-79, -1989, -3947, -5184, -5790, -6422, -7419, -8833,
	-10419, -11535, -11959, -12198, -12436, -12415, -12256, -12328,
	-12264, -11410, -9663, -7501, -5692, -4809, -4721, -4849,
	-4810, -4341, -3082, -984, 1000, 1993, 2091, 1975,
	2099, 2596, 3392, 4257, 4919, 5122, 4943, 4948,
	5244, 5263, 5027, 5083, 5114, 4657, 4463, 5248,
	6359, 6854, 6621, 6037, 5498, 5031, 4632, 4770,
	5874, 7513, 8759, 9144, 8995, 8932, 9186, 9559,
	9908, 10161, 10271, 10001, 9120, 7949, 7012, 6274,
	5702, 5661, 5974, 6080, 6211, 6481, 6488, 6236,
	6004, 5558, 4879, 4499, 4441, 4267, 4168, 4656,
	5451, 5478, 4655, 4027, 4161, 4742, 5443, 6089,
	6692, 7466, 8081, 7979, 7551, 7450, 7521, 7461,
	7339, 7337, 7645, 7992, 7697, 6747, 5844, 5569,
	6185, 7124, 7699, 7880, 7907, 7691, 7545, 7796,
	8079, 8237, 8240, 7520, 6030, 3932, 760, -2730,
	-4731, -5699, -7287, -9200, -10748, -12330, -13866, -14985,
	-15915, -16445, -16734, -17993, -20645, -23381, -24926, -25147,
	-24731, -24437, -24542, -25046, -25718, -25604, -23743, -20668,
	-17567, -15050, -13178, -11718, -10221, -8316, -5912, -3284,
	-1074, 161, 464, 426, 985, 2392, 3786, 4535,
	5089, 5771, 6199, 6350, 6568, 6681, 6432, 6206,
	5900, 5122, 4416, 4395, 4530, 4419, 4516, 4810,
	4752, 4354, 3978, 3712, 3868, 4407, 4990, 5572,
	6137, 6409, 6374, 6237, 6083, 6195, 6811, 7532,
	7743, 7380, 6555, 5309, 3998, 3065, 2409, 1806,
	1579, 1848, 1771, 1001, 94, -489, -850, -941,
	-997, -1645, -2599, -3036, -3318, -4117, -4676, -4143,
	-3151, -2866, -2948, -2277, -572, 1532, 3456, 5049,
	6290, 7133, 7721, 8511, 10023, 11939, 13460, 14118,
	14126, 13975, 13978, 14168, 14479, 14807, 14932, 14814,
	14794, 14959, 14975, 14552, 13591, 12258, 11296, 11364,
	12053, 12428, 12195, 11641, 10881, 9913, 8844, 7970,
	7541, 7490, 7530, 7569, 7854, 8516, 9170, 9424,
	9476, 9644, 9731, 9476, 9015, 8502, 7727, 6437,
	4838, 3442, 2347, 1208, 138, -250, 2, 79,
	-466, -1246, -1969, -2884, -4000, -4813, -4998, -4700,
	-4506, -5270, -7012, -8324, -8354, -7995, -7963, -7712,
	-7049, -6593, -6487, -6386, -6383, -6712, -7325, -7947,
	-8526, -8943, -9004, -8930, -9195, -9794, -10366, -10817,
	-11097, -10705, -9658, -8595, -8286, -8344, -7860, -7099,
	-6985, -7235, -6946, -6326, -6101, -6253, -6682, -7119,
	-7627, -8992, -11388, -13474, -14675, -16223, -18513, -20280,
	-21196, -22477, -24490, -26083, -26634, -26839, -27091, -27299,
	-27899, -28929, -29470, -29398, -29571, -30008, -30119, -29795,
	-28679, -26438, -23890, -21975, -20363, -18626, -17192, -16083,
	-14375, -11744, -9037, -6786, -4650, -2899, -2139, -1785,
	-961, 1, 725, 1560, 2293, 2385, 2221, 2402,
	2657, 2897, 3213, 3161, 2284, 1216, 997, 1677,
	2713, 3788, 4315, 3747, 2532, 1755, 1516, 1721,
	2325, 2598, 2180, 1846, 1953, 2141, 2659, 3746,
	4728, 5127, 5362, 5629, 5638, 5397, 4925, 3866,
	2324, 1128, 600, 55, -681, -1018, -644, -402,
	-934, -1698, -2057, -2030, -1770, -1409, -1045, -417,
	508, 1239, 1378, 1587, 2384, 3359, 4285, 5781,
	8040, 10340, 12401, 14580, 16445, 17323, 17495, 18045,
	19222, 20461, 21415, 22095, 22508, 22638, 22425, 21998,
	21905, 22429, 22756, 22401, 21935, 21820, 21753, 21278,
	20247, 18971, 18051, 17661, 17397, 16962, 16634, 16647,
	16788, 16637, 16246, 15933, 15733, 15562, 15427, 15306,
	15226, 15290, 15583, 15886, 15954, 15756, 15656, 15908,
	16095, 15853, 15602, 15616, 15307, 14375, 13446, 12739,
	12248, 11986, 11630, 11015, 10480, 9931, 9008, 8070,
	7430, 6613, 5264, 3841, 2930, 2538, 2044, 1238,
	483, -221, -871, -1041, -909, -1008, -1103, -1090,
	-1112, -797, 74, 678, 275, -921, -2270, -3021,
	-2665, -1587, -512, 252, 661, 250, -1026, -2143,
	-2233, -1796, -1820, -2649, -4144, -5939, -7663, -9541,
	-11549, -12549, -12510, -13256, -15529, -18383, -21191, -23526,
	-24993, -26047, -26887, -27269, -28037, -29785, -31172, -30927,
	-29467, -27923, -27008, -26605, -26377, -26308, -25782, -23878,
	-20911, -17960, -15767, -14783, -14415, -13282, -11177, -8812,
	-6376, -4081, -2418, -1277, -444, 158, 688, 1252,
	1652, 1715, 1413, 1040, 1131, 1476, 1373, 1064,
	1205, 1261, 1145, 1327, 1760, 2124, 2487, 2603,
	1970, 816, -130, -591, -844, -1397, -2067, -2645,
	-3051, -3141, -3058, -3096, -3063, -3018, -3675, -5032,
	-6248, -6921, -7130, -7015, -6912, -7100, -7579, -8304,
	-9124, -9651, -9652, -9415, -9449, -10144, -11552, -13108,
	-13743, -13369, -12736, -11896, -10981, -10934, -11540, -11391,
	-10360, -9293, -8339, -7679, -7567, -7364, -6339, -4739,
	-2938, -860, 1195, 2545, 3245, 3974, 4928, 5817,
	6678, 7629, 8358, 8703, 8941, 8989, 8530, 8043,
	8130, 8370, 8352, 8456, 8650, 8501, 8182, 8093,
	8138, 7991, 7552, 7104, 6940, 6975, 7083, 7280,
	7465, 7741, 8185, 8442, 8411, 8444, 8574, 8569,
	8403, 8310, 8606, 9192, 9277, 8755, 8429, 8651,
	9024, 9384, 9973, 10829, 11596, 11811, 11328, 10615,
	10507, 10818, 10788, 10251, 9629, 9131, 8806, 8834,
	9170, 9362, 9302, 9572, 10364, 10688, 10218, 9738,
	9497, 8970, 7980, 6956, 6615, 7107, 7595, 7657,
	7694, 8042, 8581, 9165, 9636, 10200, 11133, 11947,
	12150, 12108, 12247, 12577, 12917, 13063, 12728, 11885,
	10788, 9738, 8929, 8274, 7104, 4645, 1230, -1870,
	-4247, -6430, -8648, -10795, -13421, -16768, -19832, -21765,
	-22769, -23165, -23305, -23861, -24885, -25639, -26037, -26133,
	-25508, -24135, -22736, -21851, -21158, -19884, -17778, -15170,
	-12504, -10278, -8908, -7716, -6047, -4163, -2253, -218,
	1625, 2809, 3243, 3417, 4106, 5337, 6480, 7232,
	7624, 7742, 7839, 8050, 8290, 8319, 7694, 6463,
	5465, 5297, 5659, 6343, 6992, 7261, 7216, 7163,
	7198, 7136, 6964, 6897, 6679, 6000, 5492, 5970,
	6789, 7213, 7598, 8022, 7996, 7745, 7601, 7083,
	6131, 5411, 4826, 3963, 2865, 1606, 164, -985,
	-1558, -1883, -2146, -2398, -3015, -4228, -5598, -6262,
	-6183, -5813, -5359, -5024, -5091, -5205, -4858, -4121,
	-3183, -2349, -1923, -1341, -90, 1360, 2671, 4171,
	5814, 6981, 7262, 7120, 7431, 8206, 8835, 9330,
	9963, 10656, 11242, 11506, 11359, 11190, 11376, 11817,
	11998, 11344, 10114, 9156, 8267, 6905, 5514, 4732,
	4219, 3520, 2733, 2101, 1844, 1919, 1757, 1066,
	169, -815, -2099, -3151, -3467, -3509, -3576, -3425,
	-3335, -3738, -4370, -4910, -5389, -5735, -5986, -6622,
	-7613, -8083, -7883, -7844, -7980, -7682, -7425, -7834,
	-8307, -8324, -8531, -9224, -9771, -10047, -10565, -10908,
	-10327, -9596, -9615, -9760, -9596, -9500, -9121, -8539,
	-8662, -9236, -9237, -8626, -7840, -7123, -6575, -6090,
	-5710, -5805, -5739, -4989, -3849, -2767, -1945, -1272,
	-360, 612, 953, 642, 180, -239, -739, -1180,
	-1553, -2022, -3075, -5164, -7722, -9562, -10435, -11142,
	-12472, -14907, -18194, -21372, -23961, -26130, -27485, -27677,
	-27634, -28386, -29568, -30050, -29242, -27602, -25936, -24645,
	-23650, -22748, -21720, -20302, -18150, -15194, -12023, -9363,
	-7409, -5782, -3855, -1199, 2061, 5535, 8402, 9969,
	10524, 10647, 10590, 10825, 11568, 12285, 12624, 12498,
	11898, 11260, 11203, 11585, 11829, 11787, 11907, 12559,
	13323, 13642, 13784, 13894, 13793, 13262, 12405, 11425,
	10596, 10127, 9901, 9738, 9838, 10322, 10552, 9991,
	9184, 8589, 8053, 7815, 8114, 8222, 7505, 6376,
	5328, 4413, 3768, 3459, 3465, 3421, 3060, 1947,
	173, -1276, -1793, -1829, -1763, -1474, -1163, -1221,
	-1075, -215, 1041, 1998, 2530, 2662, 2454, 2567,
	3545, 5133, 7049, 8925, 10214, 10917, 11930, 13657,
	15549, 16874, 17686, 18636, 19565, 19739, 19372, 19038,
	18551, 17807, 17122, 16779, 16695, 16540, 16200, 15887,
	15561, 15130, 14748, 14307, 13677, 13127, 12642, 12093,
	11898, 12163, 12226, 11827, 11408, 11219, 11137, 11105,
	11036, 10683, 9954, 9317, 8903, 8237, 7364, 6897,
	6665, 5963, 5021, 4555, 4355, 3836, 3304, 3105,
	3000, 2697, 2358, 2086, 1611, 869, 180, -438,
	-1099, -1522, -1788, -2315, -2413, -1649, -1147, -1235,
	-971, -691, -1248, -1975, -2066, -1773, -1692, -1865,
	-2061, -2049, -1763, -1181, -548, -263, -86, 417,
	1021, 1430, 1657, 1257, 496, 4, -371, -1141,
	-1912, -2388, -3674, -6567, -9737, -11438, -11850, -12287,
	-13254, -14885, -17413, -20460, -23189, -25328, -26756, -27088,
	-26961, -27845, -29645, -30960, -31541, -31660, -30850, -29486,
	-29027, -29530, -29626, -28783, -27247, -24922, -22157, -20038,
	-18745, -17555, -16282, -14845, -12697, -10139, -8049, -6422,
	-5060, -4253, -3719, -2773, -1497, -145, 1260, 2716,
	3951, 4544, 4410, 4047, 4025, 4420, 4854, 5032,
	4731, 4131, 3363, 2802, 2628, 2555, 2357, 1896,
	1012, 120, -90, 204, 177, -72, -343, -922,
	-1729, -2165, -2167, -2022, -1622, -983, -400, 156,
	535, 321, -515, -1571, -2753, -3948, -4740, -5031,
	-5357, -6218, -7388, -8288, -8842, -9080, -9061, -8736,
	-8233, -7824, -7508, -6757, -5491, -4519, -4121, -3598,
	-2689, -1635, -580, 686, 2467, 4840, 7259, 9100,
	10484, 11803, 12915, 13610, 14289, 15339, 16630, 17675,
	18215, 18157, 17881, 18028, 18516, 18699, 18594, 18866,
	19602, 20070, 19851, 19269, 18748, 18254, 17526, 16622,
	16047, 16048, 16144, 15965, 15762, 15777, 15989, 16168,
	16093, 15933, 15957, 15929, 15663, 15394, 15113, 14715,
	14540, 14671, 14623, 14391, 14236, 14225, 14312, 14480,
	14386, 13697, 12708, 11807, 10963, 10246, 9821, 9638,
	9325, 8926, 8543, 7997, 7159, 6288, 5447, 4248,
	2917, 2293, 2278, 2184, 1944, 1652, 1214, 1057,
	1369, 1608, 1653, 1754, 1789, 1834, 2113, 2399,
	2347, 2282, 2341, 2528, 2935, 3474, 3946, 4292,
	4528, 4696, 4858, 5072, 5163, 4718, 3340, 1360,
	-774, -3125, -5356, -6781, -7789, -9202, -10874, -12588,
	-14630, -16855, -18834, -20432, -21605, -22612, -23944, -25545,
	-27068, -28373, -29256, -29591, -29478, -29035, -28496, -28120,
	-27531, -26075, -23712, -20977, -18527, -16804, -15877, -15294,
	-14050, -11616, -8887, -6788, -5046, -3469, -2519, -1965,
	-967, 586, 2109, 3072, 3328, 3134, 3157, 3445,
	3439, 2878, 2235, 1984, 1825, 1214, 468, 299,
	589, 382, -342, -1184, -2027, -2883, -3448, -3439,
	-3022, -2506, -1920, -1436, -1446, -1688, -1561, -1007,
	-412, 57, 89, -467, -1064, -1469, -2171, -3102,
	-3616, -3788, -4083, -4531, -4908, -5111, -5033, -4795,
	-4776, -5180, -5932, -6734, -7145, -7153, -7145, -7073,
	-6784, -6689, -6931, -6997, -6690, -6287, -5898, -5393,
	-4603, -3565, -2531, -1574, -702, -112, 342, 1079,
	2034, 2866, 3351, 3287, 2887, 2752, 2965, 3164,
	3148, 2785, 2021, 1092, 335, 91, 454, 886,
	897, 650, 323, 43, 247, 812, 966, 635,
	325, 85, -52, 281, 1014, 1668, 2208, 2948,
	3934, 4705, 5046, 5272, 5593, 5722, 5455, 5008,
	4605, 4218, 3761, 3271, 3069, 3267, 3778, 4267,
	4361, 4153, 4165, 4588, 5272, 6144, 6755, 6542,
	5944, 5806, 5845, 5645, 5747, 6313, 6737, 7154,
	8191, 9486, 10334, 10838, 11208, 11470, 11762, 12114,
	12206, 11977, 11760, 11702, 11627, 11354, 10864, 9991,
	8226, 5693, 3542, 2502, 1953, 1261, 262, -1533,
	-4303, -7183, -9253, -10191, -10121, -9672, -10145, -11794,
	-13419, -14014, -14016, -14002, -13544, -12819, -13157, -14377,
	-14629, -13122, -10568, -7528, -5027, -3842, -2984, -1376,
	773, 3363, 6345, 8839, 10316, 11040, 11213, 11469,
	12557, 14075, 15184, 15952, 16754, 17395, 17509, 17193,
	16825, 16369, 15504, 14655, 14002, 13030, 11842, 11059,
	10507, 9857, 9192, 8078, 6144, 4161, 2759, 1875,
	1338, 917, 59, -1497, -3354, -4794, -5382, -5314,
	-5000, -4707, -4743, -5078, -5307, -5243, -5043, -4826,
	-4884, -5437, -6064, -6312, -6242, -5866, -5166, -4271,
	-3621, -3296, -3232, -3338, -3379, -3136, -2824, -2942,
	-3461, -3933, -4438, -5163, -5735, -5696, -5201, -4482,
	-3630, -2826, -2020, -763, 1161, 3256, 4816, 5876,
	6963, 8069, 8755, 9267, 10132, 11205, 12092, 12789,
	13428, 14047, 14623, 14950, 14767, 14154, 13464, 12867,
	12132, 11064, 9834, 8619, 7440, 6368, 5334, 4166,
	3061, 2395, 2008, 1666, 1401, 1017, 351, -196,
	-406, -675, -1015, -874, -318, -104, -410, -601,
	-266, 267, 505, 453, 576, 1147, 1695, 1533,
	846, 600, 929, 969, 410, -105, -376, -727,
	-1008, -1175, -1778, -2772, -3474, -4101, -4984, -5705,
	-5922, -5972, -6140, -6454, -6790, -6822, -6504, -6216,
	-6011, -5886, -5654, -5213, -4633, -4208, -3970, -3809,
	-3802, -3791, -3473, -3181, -3465, -4116, -4776, -5405,
	-6217, -7226, -8650, -10997, -14072, -16808, -18660, -20060,
	-21308, -22508, -24092, -25980, -27346, -28010, -28545, -29080,
	-29511, -30202, -31192, -31807, -31616, -30899, -29972, -28944,
	-27851, -26729, -25519, -23986, -21952, -19659, -17638, -16095,
	-14762, -13301, -11613, -9770, -7898, -5945, -3882, -1897,
	-178, 1278, 2533, 3728, 5083, 6549, 7762, 8506,
	9193, 10167, 11109, 11495, 11448, 11409, 11439, 11510,
	11743, 12109, 12639, 13265, 13524, 12843, 11657, 10760,
	10161, 9433, 8494, 7695, 7426, 7550, 7519, 7230,
	7167, 7522, 7971, 8262, 8587, 9201, 9848, 10013,
	9672, 9215, 8821, 8362, 8059, 8006, 8178, 8323,
	8402, 8686, 9066, 9031, 8658, 8562, 8630, 8012,
	6925, 6292, 6502, 6916, 7211, 7617, 7885, 7804,
	8044, 8947, 9911, 10707, 11648, 12291, 12445, 12876,
	13976, 15232, 16315, 17270, 17925, 18181, 18370, 18680,
	18733, 18352, 17851, 17494, 17146, 16824, 16688, 16585,
	16350, 16126, 16000, 15856, 15509, 14975, 14298, 13659,
	13078, 12401, 11420, 10044, 8443, 6865, 5458, 4399,
	3657, 3042, 2352, 1735, 1249, 1025, 1316, 2015,
	2673, 3056, 3253, 3484, 3776, 3884, 3929, 4245,
	4931, 5752, 6579, 7403, 8267, 9178, 9857, 10055,
	9942, 9775, 9520, 9165, 8701, 8203, 7921, 7860,
	7757, 7519, 7138, 6648, 6383, 6661, 6918, 6753,
	6241, 5642, 4920, 3945, 2958, 2248, 1526, 489,
	-471, -1071, -1527, -2080, -3128, -5686, -9413, -12331,
	-13690, -14724, -16259, -17961, -19708, -21811, -24200, -26161,
	-27049, -27056, -27254, -28336, -29811, -30850, -31278, -31313,
	-30963, -30298, -29896, -30087, -30324, -29705, -28169, -26294,
	-24364, -22304, -20293, -18769, -17569, -15666, -12648, -9632,
	-7720, -6577, -5666, -4993, -4305, -3239, -2033, -953,
	175, 1389, 2561, 3763, 4630, 5009, 5005, 4789,
	4447, 4227, 4197, 4043, 3653, 3152, 2447, 1628,
	766, -165, -1484, -2781, -3447, -3757, -4229, -4712,
	-5014, -5360, -5682, -5711, -5833, -6288, -6647, -6699,
	-6824, -7020, -7081, -7439, -8451, -9624, -10431, -10733,
	-10408, -9552, -8898, -8678, -8573, -8449, -8173, -7549,
	-6943, -6866, -7166, -7467, -7710, -7696, -7420, -6985,
	-6413, -5744, -5075, -4181, -2731, -857, 949, 2374,
	3449, 4311, 4826, 5043, 5468, 6318, 7137, 7782,
	8755, 10052, 10873, 10982, 11278, 12421, 13830, 14664,
	15145, 15795, 16385, 16552, 16475, 16448, 16519, 16526,
	16175, 15503, 15168, 15438, 15686, 15361, 14708, 14093,
	13494, 12885, 12269, 11513, 10838, 10702, 10945, 10798,
	10243, 9765, 9521, 9573, 10040, 10726, 11327, 11793,
	12058, 11975, 11679, 11714, 12159, 12690, 12987, 12927,
	12538, 12172, 12279, 12761, 13238, 13643, 13831, 13449,
	12520, 11392, 10387, 9983, 10266, 10494, 10123, 9450,
	8925, 8812, 9204, 9655, 9768, 9494, 8726, 7514,
	6561, 6371, 6469, 6222, 5569, 4561, 3246, 1982,
	857, -719, -2928, -5050, -6725, -8234, -9507, -10434,
	-11504, -12908, -14165, -15006, -15334, -15450, -15984, -17166,
	-18410, -19352, -20100, -20501, -20386, -20123, -20125, -20343,
	-20324, -19736, -18459, -16650, -14679, -12918, -11365, -9699,
	-7840, -6040, -4465, -2986, -1453, -67, 759, 1172,
	1734, 2595, 3642, 4904, 6275, 7514, 8486, 8998,
	9089, 9052, 9018, 8482, 7336, 5979, 4391, 2465,
	867, 171, -293, -1325, -2387, -3031, -3513, -4062,
	-4312, -4358, -4650, -5135, -5644, -6244, -6766, -6992,
	-7231, -7899, -8623, -8739, -8340, -8076, -8096, -8096,
	-8053, -8028, -8010, -8248, -8829, -9258, -9246, -9385,
	-10024, -10645, -10753, -10714, -10985, -11245, -11186, -11132,
	-11137, -10883, -10465, -10001, -9135, -8022, -7280, -6915,
	-6580, -6119, -5277, -3969, -2597, -1430, -387, 517,
	1042, 1096, 941, 1058, 1585, 2135, 2413, 2607,
	3030, 3810, 4865, 5925, 6778, 7302, 7312, 6742,
	6106, 6090, 6584, 6916, 6917, 6781, 6407, 5863,
	5691, 5909, 5923, 5596, 5292, 5058, 4789, 4670,
	4653, 4306, 3736, 3558, 3637, 3492, 3351, 3448,
	3297, 2776, 2482, 2599, 2753, 2796, 2813, 2835,
	2833, 2959, 3371, 3882, 4073, 4017, 4134, 4433,
	4761, 5163, 5477, 5658, 5898, 6229, 6138, 5740,
	5689, 5954, 5937, 5567, 5336, 5376, 5279, 4874,
	4242, 3414, 2249, 921, -433, -2118, -4339, -6723,
	-8744, -10379, -12204, -14587, -16875, -18279, -19076, -19938,
	-20828, -21615, -22585, -23659, -24425, -24838, -24912, -24708,
	-24880, -25455, -25584, -24726, -23274, -21512, -19691, -18383,
	-17554, -16610, -15463, -14261, -12864, -11145, -9473, -8061,
	-6796, -5321, -3438, -1355, 829, 3129, 5219, 6905,
	8519, 10084, 11338, 12297, 13244, 14379, 15608, 16413,
	16625, 16871, 17436, 17791, 17779, 17834, 18093, 18480,
	18667, 18283, 17504, 16817, 16297, 15883, 15664, 15421,
	14833, 14042, 13312, 12779, 12595, 12640, 12421, 11787,
	11052, 10394, 9825, 9538, 9651, 9825, 9733, 9312,
	8986, 8929, 8752, 8234, 7747, 7535, 7377, 7202,
	7149, 7065, 6756, 6696, 7101, 7665, 7948, 7963,
	7911, 7775, 7622, 7857, 8552, 9095, 9001, 8515,
	7868, 7349, 7337, 7791, 8286, 8679, 8978, 9119,
	9355, 9958, 10542, 10676, 10499, 10268, 9884, 9301,
	8805, 8669, 8837, 9045, 9208, 9443, 9750, 9912,
	9823, 9470, 8935, 8373, 7770, 6802, 5242, 3384,
	1771, 654, -91, -671, -1158, -1707, -2302, -2711,
	-2864, -2966, -3026, -2936, -2946, -3241, -3422, -3251,
	-3035, -3050, -3193, -3336, -3201, -2514, -1529, -675,
	82, 938, 1764, 2299, 2440, 2492, 2642, 2725,
	2699, 2703, 2564, 2181, 1890, 1765, 1523, 1329,
	1585, 1788, 1610, 1083, 359, -359, -788, -1126,
	-1910, -3034, -3989, -4954, -6267, -7676, -8851, -10254,
	-11980, -13650, -15564, -18056, -20445, -22204, -23773, -25341,
	-26542, -27372, -28183, -28807, -28866, -28644, -28831, -29444,
	-29944, -30182, -30274, -29939, -29031, -28027, -27164, -26214,
	-24999, -23467, -21572, -19560, -17847, -16359, -14584, -12368,
	-10110, -8125, -6471, -4948, -3442, -1925, -404, 1037,
	2247, 3188, 3986, 4703, 5429, 6427, 7212, 7261,
	6745, 6362, 6161, 5804, 5293, 4694, 3940, 2986,
	1921, 1076, 616, -69, -1016, -1798, -2490, -3428,
	-4342, -5024, -5676, -6191, -6367, -6371, -6280, -6006,
	-5686, -5596, -5843, -6247, -6524, -6855, -7711, -8981,
	-10082, -10694, -10878, -10649, -10295, -10017, -9866, -9693,
	-9267, -8576, -7754, -6865, -6129, -5861, -5924, -5908,
	-5469, -4811, -4188, -3686, -2990, -1701, 53, 1700,
	3069, 4609, 6384, 7679, 8400, 9248, 10385, 11253,
	11924, 12868, 13860, 14500, 15029, 15748, 16435, 16848,
	17242, 17943, 18834, 19469, 19665, 19778, 20103, 20276,
	19864, 19156, 18706, 18487, 18095, 17306, 16448, 16042,
	16040, 15896, 15391, 14695, 13943, 13271, 12790, 12488,
	12368, 12408, 12172, 11670, 11361, 11403, 11609, 11805,
	11881, 11779, 11418, 10894, 10691, 11073, 11279, 11005,
	10780, 10828, 10973, 11161, 11156, 10748, 10212, 9776,
	9385, 9072, 8771, 8371, 8143, 8376, 8814, 9091,
	9213, 9269, 9150, 8752, 8299, 8198, 8287, 8040,
	7210, 6230, 5455, 4638, 3667, 2808, 1906, 598,
	-951, -2605, -4470, -6277, -7769, -9292, -11084, -13001,
	-14972, -16923, -18670, -19954, -20435, -20339, -20293, -20523,
	-20869, -21455, -22064, -22117, -21548, -20687, -20005, -20096,
	-20775, -21006, -20237, -18637, -16681, -14879, -13500, -12380,
	-11228, -9632, -7251, -4427, -1927, -211, 649, 887,
	1196, 2053, 3126, 3769, 4075, 4628, 5338, 5787,
	6294, 7191, 8031, 8260, 8016, 7354, 6401, 5447,
	4652, 4078, 3575, 2726, 1484, 281, -622 };

static const opus_int16 frame3[2880] = {
	-1172, -1399, -1101, -364, 171, -85, -764, -1152, -1203,
	-1280, -1579, -1882, -1860, -1849, -2372, -3020, -3262,
	-3223, -2960, -2692, -3054, -3859, -4376, -4691, -5023,
	-4958, -4602, -4433, -4586, -4858, -4847, -4391, -3901,
	-3675, -3657, -3935, -4397, -4639, -4593, -4378, -4016,
	-3627, -3113, -2214, -1121, -214, 362, 544, 634,
	1075, 1625, 1870, 2077, 2516, 2948, 3267, 3708,
	4313, 4731, 4635, 4186, 3832, 3817, 3947, 3906,
	3610, 3194, 2631, 1875, 1235, 892, 510, -98,
	-606, -639, -250, 221, 541, 810, 1073, 1208,
	1281, 1514, 1975, 2462, 2681, 2849, 3530, 4591,
	5606, 6712, 7767, 8160, 8191, 8655, 9407, 9871,
	9873, 9532, 8960, 8442, 8017, 7821, 8107, 8747,
	9263, 9298, 8926, 8589, 8557, 8695, 8509, 7818,
	6859, 5933, 4702, 2900, 1290, 543, 163, -741,
	-2557, -4920, -7049, -8601, -9895, -11046, -12308, -14172,
	-16419, -18471, -20176, -21261, -21520, -21520, -21861, -22383,
	-23031, -23410, -22815, -21319, -19812, -18723, -17660, -16295,
	-14900, -13703, -12343, -10361, -8142, -6242, -4817, -3513,
	-1780, 470, 2813, 4990, 6928, 8379, 9204, 9671,
	10101, 10625, 11143, 11543, 11844, 12106, 12464, 13026,
	13594, 13909, 13996, 13983, 13829, 13748, 13543, 12906,
	12048, 11278, 10606, 10066, 9575, 8832, 7855, 6910,
	6039, 5436, 5129, 4718, 4072, 3553, 3201, 2821,
	2646, 2826, 2866, 2338, 1617, 888, 157, -402,
	-809, -1477, -2440, -3323, -3976, -4272, -4153, -4087,
	-4317, -4131, -3474, -2995, -2807, -2456, -1998, -1869,
	-1962, -1768, -1306, -942, -432, 738, 2201, 3614,
	5253, 7077, 8558, 9501, 10133, 10842, 11818, 12571,
	12573, 12067, 11478, 10870, 10327, 9987, 9857, 9867,
	9796, 9538, 9456, 9797, 10274, 10604, 10674, 10507,
	10201, 9901, 9543, 8888, 7806, 6500, 5381, 4604,
	3906, 3097, 2408, 1998, 1593, 989, 418, -61,
	-718, -1536, -2416, -3414, -4245, -4649, -4987, -5530,
	-5980, -6279, -6470, -6371, -6114, -5888, -5425, -4673,
	-3935, -3269, -2673, -2365, -1903, -1043, -412, -388,
	-324, 261, 933, 1295, 1497, 1516, 1133, 242,
	-680, -1468, -2113, -2661, -3342, -4225, -5076, -5861,
	-6752, -7669, -8461, -9633, -11466, -13592, -15485, -17357,
	-19593, -21737, -23068, -23765, -24501, -25459, -26392, -27247,
	-28052, -28561, -28574, -28397, -28482, -29067, -30110, -30896,
	-30646, -29816, -29185, -28536, -27742, -27428, -27212, -26276,
	-24817, -23195, -21515, -20212, -19177, -17802, -16359, -15156,
	-13511, -11194, -8934, -6986, -5134, -3357, -1590, 115,
	1502, 2801, 4354, 5727, 6352, 6932, 7995, 9156,
	10293, 11791, 13319, 14093, 14063, 13690, 13368, 13340,
	13371, 12838, 11918, 11029, 10026, 8750, 7546, 6764,
	6354, 6054, 5729, 5510, 5452, 5441, 5383, 5052,
	4478, 4303, 4638, 4585, 3874, 3271, 3049, 2922,
	2920, 3275, 3710, 4023, 4205, 4312, 4429, 4617,
	4868, 5155, 5335, 5150, 4539, 3842, 3604, 3585,
	3512, 3663, 4309, 5171, 5942, 6565, 7040, 7633,
	8622, 9581, 10050, 10224, 10645, 11171, 11333, 11242,
	11368, 11794, 12333, 12940, 13581, 14104, 14509, 14613,
	14463, 14377, 14369, 14127, 13688, 13372, 13269, 13340,
	13546, 13756, 13776, 13589, 13034, 12414, 12160, 12149,
	11975, 11708, 11723, 12002, 12293, 12626, 12946, 12994,
	12681, 12487, 12675, 13047, 13315, 13316, 13176, 13253,
	13710, 14570, 15800, 16870, 17143, 16895, 16801, 17089,
	17682, 18289, 18388, 17761, 16640, 15379, 14404, 13994,
	13752, 13368, 12775, 11801, 10274, 8619, 7508, 6950,
	6428, 5742, 4915, 3707, 2093, 533, -871, -2314,
	-3696, -5126, -6952, -8842, -10365, -11841, -13235, -13970,
	-14323, -15383, -17139, -18806, -19913, -20397, -20626, -21109,
	-21707, -22069, -22518, -23131, -22857, -21424, -19912, -19018,
	-18245, -17100, -15675, -14233, -12972, -11813, -10536, -9316,
	-8296, -7019, -5074, -2992, -1349, -44, 1005, 1506,
	1497, 1531, 1871, 2154, 2196, 2169, 2452, 2828,
	2868, 2590, 2445, 2520, 2264, 1394, 413, -281,
	-868, -1781, -2910, -4239, -5657, -6818, -7498, -7865,
	-8219, -8644, -8980, -9085, -9047, -9021, -9044, -9040,
	-9197, -9701, -10365, -10870, -11128, -11177, -11119, -11150,
	-11223, -11100, -10844, -10725, -10563, -10123, -9579, -9147,
	-8879, -8782, -8713, -8474, -8020, -7363, -6682, -6359,
	-6409, -6385, -6058, -5496, -4745, -3972, -3460, -3199,
	-2884, -2154, -874, 493, 1495, 2111, 2523, 2814,
	3168, 3775, 4427, 4638, 4235, 3676, 3478, 3445,
	3297, 3190, 3178, 2924, 2471, 2263, 2425, 2678,
	2649, 2149, 1343, 424, -631, -1583, -2227, -2881,
	-3617, -3913, -3610, -3025, -2354, -1741, -1200, -493,
	316, 757, 927, 1114, 1103, 793, 501, 378,
	442, 772, 1185, 1519, 1989, 2880, 4091, 5278,
	6269, 7308, 8499, 9293, 9383, 9222, 9032, 8742,
	8593, 8818, 8901, 8632, 8303, 7940, 7515, 7092,
	6535, 5952, 5557, 4887, 3462, 2000, 1088, 303,
	-886, -2501, -4261, -5899, -7505, -9223, -10691, -11539,
	-12087, -12801, -13927, -15286, -16504, -17417, -17935, -17899,
	-17779, -18352, -19299, -19818, -20015, -19764, -18825, -17727,
	-17199, -17021, -16806, -16432, -15536, -14021, -12333, -10686,
	-9085, -7470, -5554, -3124, -650, 1522, 3582, 5455,
	6810, 7758, 8688, 9762, 10904, 12030, 13100, 14154,
	15017, 15605, 16153, 16834, 17465, 17818, 17810, 17427,
	16927, 16684, 16410, 15894, 15350, 14732, 13592, 12158,
	11150, 10658, 10251, 9590, 8564, 7483, 6696, 6005,
	5159, 4528, 4220, 3586, 2423, 1485, 1323, 1468,
	1307, 1037, 894, 568, -158, -568, -385, -258,
	-535, -807, -647, -69, 606, 1141, 1528, 2021,
	2408, 2466, 2246, 2032, 1997, 2026, 1918, 1744,
	1781, 2038, 2304, 2662, 3296, 4285, 5510, 6612,
	7206, 7447, 7900, 8578, 8941, 8958, 8965, 8896,
	8800, 9060, 9625, 10157, 10674, 11189, 11491, 11511,
	11287, 10953, 10753, 10482, 9651, 8361, 7315, 6598,
	5690, 4433, 3151, 2261, 1873, 1563, 1035, 702,
	928, 1455, 1982, 2334, 2437, 2550, 2854, 2997,
	2826, 2762, 2869, 2824, 2819, 3207, 3734, 4213,
	4763, 5187, 5316, 5450, 5726, 5842, 5646, 5311,
	4994, 4547, 4233, 4111, 3781, 3062, 2248, 1325,
	72, -1257, -2441, -3745, -4965, -5741, -6227, -6883,
	-7651, -8537, -9683, -11031, -12822, -15101, -17100, -18672,
	-20652, -22916, -24506, -25465, -26393, -27179, -27597, -27862,
	-27950, -27642, -27295, -27275, -27268, -27145, -27243, -27433,
	-27137, -26614, -26489, -26367, -25536, -24141, -22569, -20872,
	-19045, -17011, -14888, -13137, -11673, -9716, -7250, -5328,
	-4420, -3695, -2406, -873, 410, 1520, 2477, 3245,
	3626, 3645, 3876, 4650, 5191, 4821, 4166, 3782,
	3209, 2480, 2307, 2291, 1800, 1271, 1181, 1139,
	970, 988, 1271, 1684, 2138, 2444, 2432, 2356,
	2573, 2988, 3280, 3467, 3740, 4107, 4505, 4949,
	5375, 5646, 5758, 5863, 5599, 4989, 4671, 4844,
	5031, 5002, 4881, 4902, 5421, 6445, 7448, 8113,
	8789, 9545, 10194, 10698, 11085, 11340, 11674, 12150,
	12440, 12562, 12897, 13444, 14135, 15028, 16060, 17031,
	17906, 18599, 18857, 18721, 18531, 18308, 17888, 17334,
	16807, 16282, 16010, 15995, 15805, 15248, 14501, 13691,
	12861, 12114, 11460, 10628, 9668, 8814, 8198, 7732,
	7454, 7316, 7050, 6634, 6328, 6215, 6216, 6347,
	6413, 6248, 6136, 6448, 6998, 7786, 8856, 9555,
	9633, 9779, 10191, 10546, 11020, 11663, 12107, 12355,
	12524, 12300, 11786, 11569, 11502, 11052, 10438, 10013,
	9658, 9152, 8722, 8369, 7749, 6672, 5441, 4191,
	2602, 799, -619, -1545, -2324, -3208, -4350, -5716,
	-7313, -9198, -11226, -13163, -15000, -16854, -18684, -20483,
	-22386, -24282, -25756, -27031, -28325, -29083, -28912, -28632,
	-28942, -29160, -28747, -28216, -27478, -26130, -24904, -24351,
	-23517, -21985, -20219, -18107, -15550, -13114, -11094, -9403,
	-8036, -6589, -4502, -2209, -462, 721, 1578, 2117,
	2572, 3141, 3526, 3576, 3650, 3938, 4279, 4417,
	4712, 5138, 5463, 5615, 5301, 4373, 3538, 3221,
	2831, 2144, 1687, 1153, 263, -739, -1378, -1661,
	-1788, -1808, -1900, -2293, -2838, -3289, -3815, -4733,
	-6001, -7269, -8429, -9367, -9911, -10223, -10574, -11067,
	-11792, -12562, -13193, -14096, -15282, -16068, -16324, -16515,
	-16587, -16348, -16099, -16079, -16125, -16122, -16048, -15707,
	-15056, -14470, -13943, -13029, -11708, -10376, -9088, -7698,
	-6473, -5536, -4393, -2885, -1487, -308, 779, 1547,
	1805, 1791, 1860, 2054, 2196, 2439, 3023, 3682,
	4229, 4824, 5396, 5615, 5520, 5327, 5142, 5040,
	4923, 4509, 3819, 3298, 3091, 2879, 2618, 2506,
	2556, 2728, 3040, 3431, 3757, 3929, 3958, 3756,
	3158, 2515, 2273, 2247, 1977, 1735, 2040, 2665,
	3225, 3826, 4403, 4891, 5756, 6981, 7912, 8558,
	9099, 9257, 9170, 9264, 9563, 10056, 10766, 11452,
	11932, 12352, 12805, 13325, 13769, 13897, 13510, 12774,
	12184, 11907, 11530, 10804, 10074, 9466, 8297, 6093,
	3597, 1766, 514, -752, -2184, -3802, -5679, -7395,
	-8657, -9815, -10639, -10574, -10861, -12194, -13210, -13353,
	-13459, -13345, -12639, -12216, -12574, -12942, -12679, -11704,
	-10345, -8767, -7115, -5420, -3633, -1895, -197, 1840,
	4164, 6194, 7865, 9415, 10372, 10810, 11575, 12491,
	12907, 13374, 14232, 14751, 14886, 15292, 15916, 16335,
	16379, 16122, 15585, 14695, 13708, 13052, 12562, 11854,
	10847, 9536, 8221, 7361, 6810, 6220, 5758, 5569,
	5373, 5085, 4900, 4732, 4320, 3663, 2910, 2171,
	1712, 1585, 1719, 2016, 2402, 2648, 2575, 2590,
	2977, 3105, 2755, 2641, 3045, 3465, 4009, 4699,
	5242, 5767, 6340, 6590, 6506, 6501, 6597, 6381,
	5720, 5180, 5097, 5142, 4769, 4148, 3981, 4471,
	5253, 6077, 6873, 7560, 8057, 8422, 8844, 9219,
	9262, 9198, 9277, 9247, 9177, 9477, 10087, 10775,
	11405, 11647, 11483, 11468, 11616, 11307, 10579, 9954,
	9375, 8638, 7839, 6836, 5495, 4167, 3166, 2326,
	1412, 535, 91, 55, -123, -659, -1224, -1543,
	-1661, -1712, -1829, -2129, -2594, -2914, -2985, -2861,
	-2273, -1300, -592, -268, -10, 296, 732, 1523,
	2146, 2095, 1664, 1141, 508, 43, -94, -245,
	-621, -1271, -2154, -3226, -4343, -5711, -7144, -8397,
	-9623, -10846, -11939, -13023, -14225, -15452, -16629, -18106,
	-20039, -22143, -24213, -26256, -27906, -28918, -29587, -30081,
	-30450, -30974, -31545, -31567, -31107, -30736, -30549, -30293,
	-30004, -29743, -29264, -28459, -27589, -26878, -26197, -25328,
	-24148, -22478, -20170, -17601, -15237, -13205, -11427, -9596,
	-7524, -5616, -4197, -2997, -1883, -873, 86, 1168,
	2301, 3194, 3921, 4747, 5512, 6218, 7254, 8426,
	8931, 8909, 8772, 8608, 8256, 7551, 6655, 6065,
	5880, 5660, 5319, 5225, 5290, 5440, 5662, 5842,
	5827, 5653, 5450, 5250, 4963, 4671, 4584, 4497,
	4133, 3807, 3492, 3031, 2731, 2618, 2149, 1238,
	347, -135, -227, -349, -752, -905, -632, -297,
	-219, -51, 782, 2080, 3005, 3399, 3834, 4410,
	4821, 5413, 6474, 7751, 8899, 9773, 10428, 11242,
	12390, 13543, 14541, 15403, 15871, 15823, 15771, 16121,
	16663, 16855, 16546, 15884, 15097, 14624, 14586, 14584,
	14554, 14629, 14563, 14102, 13525, 12992, 12332, 11518,
	10789, 10315, 10002, 9700, 9415, 9074, 8598, 8042,
	7699, 7532, 7279, 6970, 6750, 6678, 6921, 7496,
	8056, 8379, 8621, 8888, 8994, 9112, 9654, 10312,
	10526, 10628, 10985, 11114, 10766, 10328, 10037, 9741,
	9497, 9350, 9379, 9591, 9648, 9180, 8353, 7650,
	7153, 6525, 5569, 4308, 2954, 1570, 178, -956,
	-1812, -2984, -4782, -6811, -8774, -10577, -11897, -12871,
	-14239, -16267, -18504, -20690, -22570, -23807, -24645, -25303,
	-25543, -25677, -26160, -26473, -26028, -25055, -23654, -21927,
	-20539, -19847, -19166, -17918, -16518, -15224, -13520, -11546,
	-10191, -9415, -8406, -6966, -5210, -3179, -1457, -583,
	-188, 171, 521, 1054, 1553, 1571, 1278, 1054,
	595, -90, -298, 119, 618, 822, 644, 169,
	-216, -411, -404, -311, -224, -421, -1019, -1668,
	-1977, -1761, -1114, -556, -405, -499, -718, -1144,
	-1410, -1326, -1456, -2126, -2923, -3488, -3753, -3717,
	-3653, -3710, -3688, -3773, -4473, -5436, -5988, -6358,
	-6912, -7213, -6876, -6294, -5895, -5537, -4967, -4323,
	-3746, -3289, -3035, -2964, -2961, -2903, -2528, -1640,
	-575, 78, 326, 596, 1056, 1633, 2418, 3327,
	3918, 4092, 4274, 4598, 4782, 4914, 5306, 5527,
	5113, 4544, 4410, 4580, 4815, 5055, 5100, 4908,
	4835, 5049, 5209, 5201, 5204, 5293, 5364, 5496,
	5760, 6067, 6429, 6820, 7036, 7133, 7462, 8111,
	8712, 9006, 9033, 9007, 9049, 8979, 8679, 8428,
	8445, 8631, 8942, 9278, 9602, 10023, 10660, 11422,
	12051, 12296, 12248, 12179, 12000, 11576, 11150, 10875,
	10397, 9632, 8680, 7721, 7096, 6936, 6668, 5661,
	4074, 2468, 1069, -296, -1779, -3474, -5351, -7487,
	-9897, -12203, -13881, -14778, -15169, -15517, -16216, -17282,
	-18117, -18478, -18389, -17749, -16644, -15793, -15712, -15966,
	-15912, -15313, -14046, -12161, -10118, -8807, -8096, -7060,
	-5487, -3830, -2046, -73, 1403, 2160, 3108, 4758,
	6527, 8171, 10008, 11756, 12609, 12662, 12718, 12911,
	12875, 12845, 13227, 13699, 13898, 13978, 14284, 14568,
	14646, 14811, 15150, 15143, 14511, 13680, 13026, 12514,
	12170, 11909, 11563, 10979, 10268, 9710, 9480, 9387,
	9125, 8688, 8190, 7479, 6372, 5092, 4049, 3004,
	1595, 109, -1041, -1974, -2743, -3275, -3957, -4940,
	-5647, -5910, -6026, -6114, -6081, -6205, -6560, -6719,
	-6432, -5924, -5399, -4752, -3985, -3460, -3080, -2395,
	-1369, -552, -121, 283, 861, 1430, 1767, 1876,
	2018, 2498, 3207, 3623, 3611, 3671, 4020, 4385,
	4730, 5147, 5502, 5735, 5756, 5319, 4666, 4340,
	4299, 4107, 3731, 3468, 3491, 3695, 3918, 4108,
	4068, 3646, 3238, 3128, 2857, 2192, 1555, 1097,
	556, -118, -675, -869, -752, -543, -209, 284,
	574, 668, 951, 1527, 2174, 2622, 2698, 2439,
	1940, 1433, 1467, 2264, 3123, 3750, 4071, 3908,
	3552, 3535, 3667, 3530, 3107, 2424, 1571, 710,
	-421, -1430, -1938, -2507, -3623, -4795, -5839, -7258,
	-9038, -10626, -11887, -13147, -14802, -16727, -18702, -20604,
	-22293, -23726, -24909, -25675, -26378, -27812, -29600, -30607,
	-30929, -31229, -31411, -31492, -31727, -31790, -31443, -30921,
	-30100, -28925, -28008, -27514, -26788, -25644, -24550, -23498,
	-22170, -20966, -20358, -19762, -18538, -17080, -15699, -14045,
	-12184, -10553, -8835, -7019, -5777, -5099, -4324, -3531,
	-3097, -2615, -1602, -341, 728, 1584, 2293, 3132,
	3969, 4309, 4038, 3710, 3649, 3509, 3024, 2408,
	1908, 1546, 1215, 1091, 1178, 1271, 1321, 1521,
	1886, 2171, 2251, 2271, 2354, 2244, 1734, 1220,
	1200, 1432, 1523, 1595, 1732, 1891, 2288, 2818,
	2871, 2452, 2205, 2484, 2767, 2868, 3151, 3830,
	4506, 4779, 4871, 5120, 5454, 5793, 6298, 7223,
	8268, 9150, 9955, 10874, 11797, 12596, 13362, 14001,
	14288, 14387, 14607, 15115, 15807, 16593, 17213, 17469,
	17586, 17949, 18517, 18885, 18888, 18651, 18227, 17686,
	17467, 17569, 17787, 18087, 18313, 18172, 17902, 17912,
	17928, 17637, 17303, 16906, 16238, 15553, 15274, 15175,
	15097, 15333, 15848, 16291, 16606, 16786, 16674, 16440,
	16375, 16391, 16415, 16513, 16465, 16089, 15786, 15874,
	15993, 15870, 15773, 15743, 15337, 14557, 13779, 13034,
	12177, 11192, 9959, 8467, 7109, 6012, 4739, 3196,
	1905, 966, -46, -1301, -2701, -4440, -6660, -9145,
	-11770, -14375, -16419, -17816, -19295, -20897, -22085, -22691,
	-23027, -23217, -23413, -23803, -24185, -24398, -24743, -25266,
	-25560, -25364, -25030, -24412, -23278, -22114, -21220, -19863,
	-17645, -15196, -12788, -10079, -7305, -5110, -3560, -2249,
	-792, 915, 2670, 4278, 5688, 6984, 8402, 9857,
	10763, 11072, 11291, 11660, 11717, 11430, 11260, 11279,
	11057, 10376, 9494, 8629, 7745, 6934, 6095, 5380,
	4852, 4349, 3713, 3203, 2924, 2592, 2231, 1926,
	1378, 652, 169, -229, -652, -831, -998, -1693,
	-2616, -3092, -3210, -3465, -3910, -4368, -4983, -5701,
	-6200, -6450, -6727, -6868, -6790, -6939, -7262, -7218,
	-6965, -7019, -7287, -7386, -7351, -7360, -7392, -7227,
	-6874, -6510, -5920, -4867, -3703, -2821, -2275, -2038,
	-1859, -1368, -608, 230, 1040, 1532, 1634, 1731,
	1998, 2288, 2654, 3173, 3581, 3626, 3454, 3377,
	3270, 2827, 2154, 1445, 721, 244, 146, 35,
	-218, -208, 131, 545, 824, 787, 489, 94,
	-506, -1195, -1450, -1183, -780, -414, -65, 322,
	857, 1537, 2173, 2549, 2570, 2364, 2060, 1556,
	865, 186, -153, -42, 333, 690, 1021, 1446,
	1934, 2447, 2939, 3399, 4018, 4460, 4183, 3415,
	2997, 2919, 2628, 2143, 1636, 1003, 419, 132,
	-249, -1279, -2799, -4388, -6046, -7735, -9077, -10059,
	-11129, -12414, -13740, -15030, -16154, -16962, -17261, -17090,
	-17021, -17616, -18277, -18153, -17519, -16709, -15319, -13573,
	-12411, -11965, -11479, -10647, -9381, -7518, -5402, -3808,
	-2778, -1609, -124, 1356, 2933, 4580, 5688, 6107,
	6352, 6711, 7260, 8009, 8574, 8743, 8900, 9108,
	8996, 8735, 8775, 8792, 8403, 7907, 7809, 7915,
	7764, 7457, 7269, 7104, 6860, 6558, 6135, 5621,
	5172, 4632, 4062, 3833, 3811, 3577, 3226, 2995,
	2890, 2982, 3183, 3295, 3323, 3349, 3086, 2536,
	2179, 1999, 1423, 408, -471, -976, -1296, -1497,
	-1555, -1726, -1776, -1297, -631, -167, 479, 1278,
	1440, 1003, 721, 665, 764, 1151, 1631, 2046,
	2854, 4074, 5045, 5759, 6628, 7335, 7547, 7631,
	7682, 7141, 6301, 6005, 5986, 5538, 5091, 5192,
	5376, 5395, 5678, 6128, 6428, 6782, 7169, 7179,
	7019, 7178, 7474, 7462, 7096, 6676, 6561, 6847,
	7336, 7741, 7707, 7180, 6620, 6290, 6043, 6024,
	6383, 6690, 6669, 6528, 6310, 6035, 6151, 6621,
	6803, 6555, 6283, 6027, 5653, 5536, 5942, 6301,
	6191, 5719, 4984, 3899, 3152, 3131, 3055, 2481,
	1731, 735, -425, -1091, -1386, -2036, -3197, -4619,
	-6482, -8553, -10168, -11193, -12118, -13409, -15137, -17201,
	-19449, -21445, -22871, -24141, -25467, -26573, -27691, -29054,
	-30069, -30529, -30826, -30717, -30044, -29541, -29341, -29034,
	-28643, -28247, -27449, -26120, -24718, -23646, -22718, -21455,
	-19814, -17958, -15781, -13407, -11321, -9610, -8291, -7218,
	-5690, -3712, -2239, -1267, -207, 635, 1253, 2147,
	3228, 4085, 4998, 6056, 6990, 7803, 8533, 9118,
	9469, 9407, 9175, 9036, 9016, 8902, 8767, 8543,
	8261, 8239, 8419, 8457, 8230, 7721, 7118, 6680,
	6179, 5406, 4544, 3696, 2873, 2044, 929, -282,
	-1000, -1496, -2259, -2869, -2864, -2692, -2626, -2759,
	-3135, -3379, -3205, -2863, -2673, -2723, -2756, -2405,
	-1561, -625, 359, 1271, 1937, 2533, 3141, 3729,
	4556, 5611, 6545, 7250, 7831, 8175, 8656, 9509,
	10457, 11387, 12316, 13090, 13768, 14449, 15064, 15781,
	16652, 17379, 17664, 17597, 17431, 17491, 17802, 18130,
	18394, 18460, 18207, 18013, 18037, 17938, 17605, 17312,
	16888, 16217, 15714, 15669, 15828, 15749, 15359, 15028,
	14818, 14517, 14136, 13924, 13690, 13255, 12843, 12694,
	12858, 13141, 13248, 13219, 13253, 13249, 13098, 12990,
	13006, 12977, 12872, 12824, 12854, 12860, 12836, 12897,
	12990, 12989, 12891, 12704, 12198, 11372, 10619, 10069,
	9368, 8439, 7567, 6523, 4815, 2916, 1606, 505,
	-1092, -3248, -5825, -8346, -10014, -10956, -11759, -12220,
	-12531, -13747, -15715, -17027, -17437, -17572, -17668, -17909,
	-18583, -19281, -19231, -18442, -17516, -16545, -15484, -14883,
	-14989, -14891, -14245, -13601, -12793, -11571, -10742, -10569,
	-10120, -9094, -7941, -6540, -4628, -2776, -1488, -522,
	223, 477, 639, 1303, 1841, 1525, 1098, 1394,
	1908, 2487, 3229, 3684, 3595, 3252, 2644, 1805,
	1251, 1006, 600, 12, -784, -1784, -2803, -3546,
	-4050, -4427, -4629, -4785, -5215, -5828, -6196, -6364,
	-6671, -6870, -6986, -7636, -8703, -9373, -9589, -9708,
	-9669, -9655, -10057, -10531, -10534, -10268, -10003, -9639,
	-9313, -9397, -9716, -9675, -9212, -8779, -8497, -8206,
	-7976, -7819, -7530, -7080, -6547, -5983, -5569, -5249,
	-4622, -3751, -3043, -2337, -1327, -398, 216, 871,
	1611, 1870, 1614, 1568, 1846, 1788, 1432, 1395,
	1476, 1199, 1015, 1238, 1461, 1583, 1735, 1641,
	1338, 1359, 1654, 1661, 1299, 890, 527, 158,
	-209, -520, -729, -711, -353, 248, 1005, 1971,
	2888, 3307, 3259, 3280, 3481, 3776, 4154, 4359,
	4221, 4082, 4182, 4412, 4776, 5356, 5738, 5875,
	5944, 5866, 5411, 4734, 4109, 3366, 2247, 874,
	-592, -2133, -3437, -4473, -5499, -6503, -6994, -7329,
	-8488, -10240, -11581, -12676, -14330, -16344, -18165, -19763,
	-21152, -22135, -22984, -24004, -24944, -25678, -26390, -26909,
	-26959, -26806, -26940, -27206, -27305, -27011, -26395, -25456,
	-24077, -22523, -21214, -19940, -18215, -16073, -13792 };

void encode_three_frames()
{
	int error = 0;
	OpusEncoder* encoder = opus_encoder_create(48000, 1, 2049, &error);
	opus_encoder_ctl(encoder, OPUS_SET_BITRATE_REQUEST, 16 * 1024);
	opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY_REQUEST, 10);

	const int bufSize = 200;

	unsigned char outputBuffer[200];
	int thisPacketSize;

	memset(outputBuffer, 0, bufSize * sizeof(unsigned char));
	thisPacketSize = opus_encode(encoder, frame1, 2880, outputBuffer, bufSize);
	
	/*printf("%d\n", thisPacketSize);
	for (int c = 0; c < thisPacketSize; c++)
	{
		printf("0x%x,\n", outputBuffer[c]);
	}
	printf("\n");*/

	memset(outputBuffer, 0, bufSize * sizeof(unsigned char));
	thisPacketSize = opus_encode(encoder, frame2, 2880, outputBuffer, bufSize);
	
	/*printf("%d\n", thisPacketSize);
	for (int c = 0; c < thisPacketSize; c++)
	{
		printf("0x%x,\n", outputBuffer[c]);
	}
	printf("\n");*/

	memset(outputBuffer, 0, bufSize * sizeof(unsigned char));
	thisPacketSize = opus_encode(encoder, frame3, 2880, outputBuffer, bufSize);
	
	/*printf("%d\n", thisPacketSize);
	for (int c = 0; c < thisPacketSize; c++)
	{
		printf("0x%x,\n", outputBuffer[c]);
	}
	printf("\n");*/
}

int main(int _argc, char **_argv)
{
	/*fprintf(stdout, "%lx\n", silk_INVERSE32_varQ(99, 16));
	fprintf(stdout, "%lx\n", silk_INVERSE32_varQ(5, 16));
	fprintf(stdout, "%lx\n", silk_INVERSE32_varQ(322, 10));
	fprintf(stdout, "%lx\n", silk_INVERSE32_varQ(-2, 30));
	fprintf(stdout, "%lx\n", silk_INVERSE32_varQ(-123, 12));

	fprintf(stdout, "2f 0x%x\n", (unsigned int)n_AR_Q14);*/

	encode_three_frames();

	return 0;

	const char * oversion;
	const char * env_seed;
	int env_used;

	if (_argc>2)
	{
		fprintf(stderr, "Usage: %s [<seed>]\n", _argv[0]);
		return 1;
	}

	env_used = 0;
	env_seed = getenv("SEED");
	if (_argc>1)iseed = atoi(_argv[1]);
	else if (env_seed)
	{
		iseed = atoi(env_seed);
		env_used = 1;
	}
	else iseed = (opus_uint32)time(NULL) ^ ((getpid() & 65535) << 16);

	iseed = 13371337;

	Rw = Rz = iseed;

	oversion = opus_get_version_string();
	if (!oversion)test_failed();
	fprintf(stderr, "Testing %s encoder. Random seed: %u (%.4X)\n", oversion, iseed, fast_rand() % 65535);
	if (env_used)fprintf(stderr, "  Random seed set from the environment (SEED=%s).\n", env_seed);

	/*Setting TEST_OPUS_NOFUZZ tells the tool not to send garbage data
	into the decoders. This is helpful because garbage data
	may cause the decoders to clip, which angers CLANG IOC.*/
	run_test1(getenv("TEST_OPUS_NOFUZZ") != NULL);

	fprintf(stderr, "Tests completed successfully.\n");

	return 0;
}
