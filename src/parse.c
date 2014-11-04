/**
 * tinysmf: a midifile parsing library
 * Copyright (c) 2014 William Light.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* for ntohs, ntohl */
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <tinysmf.h>

#define SWAB16(x) x = ntohs(x)
#define SWAB32(x) x = ntohl(x)

#define PACKED __attribute__((__packed__))

#define SMF_CHUNK_FIELDS													\
	char type[4];															\
	uint32_t length

struct chunk {
	SMF_CHUNK_FIELDS;
} PACKED;

struct header_chunk {
	SMF_CHUNK_FIELDS;

	uint16_t format;
	uint16_t num_tracks;
	uint16_t division;
} PACKED;

static ssize_t
read_vlv(FILE *f, size_t avail, uint32_t *value)
{
	ssize_t ret;
	uint8_t buf;

	for (ret = 0, *value = 0, buf = 0x80;
			avail && buf & 0x80; ret++, avail--) {
		if (!fread(&buf, 1, 1, f))
			return -1;

		*value = (*value << 7) | (buf & 0x7F);
	}

	return ret;
}

static ssize_t
read_sysex_event(struct tinysmf_parser_ctx *ctx, FILE *f,
		size_t avail, int delta)
{
	ssize_t res, ret;
	uint32_t length;
	uint8_t *buf;

	ret = 0;

	res = read_vlv(f, avail, &length);
	if (res < 0)
		return -1;

	ret += res;
	avail -= res;
	if (!(buf = malloc(length)))
		return -1;

	if (!fread(buf, length, 1, f))
		goto err_fread;

	ret += length;
	free(buf);
	return ret;

err_fread:
	free(buf);
	return -1;
}

static void
cook_meta_event(struct tinysmf_meta_event *ev)
{
	switch (ev->meta_type) {
	case TINYSMF_META_TYPE_MIDI_CHANNEL:
		ev->cooked.midi_channel = ev->raw[0];
		break;

	case TINYSMF_META_TYPE_SET_TEMPO:
		ev->cooked.bpm =
			60.0 / (((ev->raw[0] << 16)
					| (ev->raw[1] << 8)
					| ev->raw[2]) * 1e-06);
		break;

	default:
		break;
	}
}

static ssize_t
read_meta_event(struct tinysmf_parser_ctx *ctx, FILE *f,
		size_t avail, int delta)
{
	struct tinysmf_meta_event ev, *fat_ev;
	ssize_t res, ret;
	uint32_t length;
	uint8_t type, *buf;

	if (!fread(&type, sizeof(type), 1, f))
		return -1;

	ret = 1;

	res = read_vlv(f, avail, &length);
	if (res < 0)
		return -1;

	ret += res;
	avail -= res;

	ev.event_type = TINYSMF_META_EVENT;
	ev.delta = delta;

	ev.meta_type = type;
	ev.nbytes = length;

	ret += length;

	if (!ctx->meta_event_cb) {
		/* won't waste the time allocating memory etc */
		fseek(f, length, SEEK_CUR);
		return ret;
	}

	if (length > 0) {
		if (length > avail
				|| !(fat_ev = malloc(sizeof(*fat_ev) + length + 1)))
			return -1;

		*fat_ev = ev;
		buf = (void *) &fat_ev[1];

		if (!fread(buf, length, 1, f)) {
			free(fat_ev);
			return -1;
		}

		/* null-terminate the buffer in case this is a string */
		buf[length] = '\0';

		cook_meta_event(fat_ev);
		ctx->meta_event_cb(ctx, fat_ev);

		free(fat_ev);
	} else {
		ctx->meta_event_cb(ctx, &ev);
	}

	return ret;
}

static ssize_t
midi_msg_size(uint8_t status_byte)
{
	switch (status_byte & 0xF0) {
	case 0x80:
	case 0x90:
	case 0xA0:
	case 0xB0:
	case 0xE0:
		return 2;

	case 0xC0:
	case 0xD0:
		return 1;

	default:
		return 0;
	}
}

static ssize_t
read_midi_event(struct tinysmf_parser_ctx *ctx, FILE *f,
		size_t avail, int delta, uint8_t *status_buf, uint8_t first_byte)
{
	unsigned to_read;
	uint8_t *buf;

	struct tinysmf_midi_event ev = {
		.event_type = TINYSMF_MIDI_EVENT,
		.delta      = delta,

		.bytes = {0, 0, 0, 0}
	};

	/* handle running status */
	if (first_byte & 0x80) {
		ev.bytes[0] = first_byte;
		*status_buf = first_byte;
		buf = &ev.bytes[1];

		to_read = midi_msg_size(ev.bytes[0]);
	} else {
		ev.bytes[0] = *status_buf;
		ev.bytes[1] = first_byte;
		buf = &ev.bytes[2];

		to_read = midi_msg_size(ev.bytes[0]);

		/* this is here to protect us against wrap-around of an unsigned
		 * type in the event midi_msg_size() returns 0 here, even though that
		 * shouldn't be able to happen. it would mean that we're trying to
		 * process running status on a one-byte message, and I'm not even
		 * sure MIDI has those. regardless, corruption is possible so it's
		 * better to catch this here and propagate an error back out. */
		if (!to_read)
			return -1;

		to_read -= 1;
	}

	if (to_read > avail)
		return -1;

	if (to_read > 0 && !fread(buf, to_read, 1, f))
		return -1;

	/* treat note-on messages with 0 velocity as note-offs */
	if ((ev.bytes[0] & 0xF0) == 0x90 && ev.bytes[2] == 0x00)
		ev.bytes[0] &= ~(0x80 ^ 0x90);

	if (ctx->midi_event_cb)
		ctx->midi_event_cb(ctx, &ev);

	return to_read;
}

static int
read_track(struct tinysmf_parser_ctx *ctx, FILE *f, ssize_t avail)
{
	uint8_t ev_type, status_buf;
	uint32_t delta;
	ssize_t res;

	status_buf = 0;

	while (avail > 0) {
		res = read_vlv(f, avail, &delta);
		if (res < 0)
			return -1;

		avail -= res;

		if (!fread(&ev_type, sizeof(ev_type), 1, f))
			return -1;

		avail -= 1;

		switch (ev_type) {
		case 0xF0:
		case 0xF7:
			puts("sysex");
			res = read_sysex_event(ctx, f, avail, delta);

			/* fall-through */

		case 0xF1:
		case 0xF2:
		case 0xF3:
		case 0xF4:
		case 0xF5:
		case 0xF6:
			status_buf = 0;
			break;

		case 0xFF:
			res = read_meta_event(ctx, f, avail, delta);
			break;

		default:
			res = read_midi_event(ctx, f, avail, delta, &status_buf, ev_type);
			break;
		}

		if (res < 0)
			return -1;

		avail -= res;
	}

	if (avail < 0)
		return -1;

	return 0;
}

static void
cook_file_info(struct tinysmf_parser_ctx *ctx, struct header_chunk *hdr)
{
	struct tinysmf_file_info *info = (void *) &ctx->file_info;

	info->format = hdr->format;
	info->num_tracks = hdr->num_tracks;

	if (hdr->division & 0x80) {
		uint8_t *div = (void *) &hdr->division;
		info->division_type = SMF_SMPTE;
		info->division.smpte.format = -div[0];
		info->division.smpte.ticks  = div[1];
	} else {
		info->division_type = SMF_PPQN;
		info->division.ppqn = ntohs(hdr->division);
	}

	if (ctx->file_info_cb)
		ctx->file_info_cb(ctx);
}

int
tinysmf_parse_stream(struct tinysmf_parser_ctx *ctx, FILE *f)
{
	struct header_chunk hdr;
	struct chunk chunk;
	int skip_chunk, i;

	if (!fread(&hdr, sizeof(hdr), 1, f) || memcmp(hdr.type, "MThd", 4)) {
		fprintf(stderr, " :: failed to read header\n");
		goto err_header;
	}

	SWAB32(hdr.length);
	SWAB16(hdr.format);
	SWAB16(hdr.num_tracks);

	cook_file_info(ctx, &hdr);

	for (i = 0; i < hdr.num_tracks; i++) {
		if (!fread(&chunk, sizeof(chunk), 1, f)) {
			fprintf(stderr, " :: failed to read track %d\n", i);
			goto err_track;
		}

		SWAB32(chunk.length);

		if (!memcmp(chunk.type, "MTrk", 4)) {
			skip_chunk = 0;

			if (ctx->track_start_cb)
				skip_chunk = ctx->track_start_cb(ctx, i);

			if (skip_chunk) {
				fseek(f, chunk.length, SEEK_CUR);
			} else {
				if (read_track(ctx, f, chunk.length)) {
					fprintf(stderr, " :: failed reading track %d\n", i);
					return 1;
				}
			}

			if (ctx->track_end_cb)
				ctx->track_end_cb(ctx, i);
		} else {
			/* skip unknown chunk.
			 * XXX: in the future, have a callback for handling unknown
			 *      chunks at client code discretion. */

			fseek(f, chunk.length, SEEK_CUR);
			i--;
		}
	}

	return 0;

err_track:
err_header:
	return -1;
}
