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

#include <tinysmf.h>

struct test_parser {
	struct tinysmf_parser_ctx ctx;

	int midi_events;
	int noteon_events;
	int noteoff_events;
};

static const char *
str_meta_type(tinysmf_meta_event_type_t meta_type)
{
	switch (meta_type) {
	case TINYSMF_META_TYPE_SEQUENCE_NUMBER:
		return "TINYSMF_META_TYPE_SEQUENCE_NUMBER";
	case TINYSMF_META_TYPE_TEXT_EVENT:
		return "TINYSMF_META_TYPE_TEXT_EVENT";
	case TINYSMF_META_TYPE_COPYRIGHT_NOTICE:
		return "TINYSMF_META_TYPE_COPYRIGHT_NOTICE";
	case TINYSMF_META_TYPE_TRACK_NAME:
		return "TINYSMF_META_TYPE_TRACK_NAME";
	case TINYSMF_META_TYPE_INSTRUMENT_NAME:
		return "TINYSMF_META_TYPE_INSTRUMENT_NAME";
	case TINYSMF_META_TYPE_LYRIC:
		return "TINYSMF_META_TYPE_LYRIC";
	case TINYSMF_META_TYPE_MARKER:
		return "TINYSMF_META_TYPE_MARKER";
	case TINYSMF_META_TYPE_CUE_POINT:
		return "TINYSMF_META_TYPE_CUE_POINT";

	case TINYSMF_META_TYPE_MIDI_CHANNEL:
		return "TINYSMF_META_TYPE_MIDI_CHANNEL";
	case TINYSMF_META_TYPE_END_OF_TRACK:
		return "TINYSMF_META_TYPE_END_OF_TRACK";

	case TINYSMF_META_TYPE_SET_TEMPO:
		return "TINYSMF_META_TYPE_SET_TEMPO";
	case TINYSMF_META_TYPE_SMPTE_OFFSET:
		return "TINYSMF_META_TYPE_SMPTE_OFFSET";
	case TINYSMF_META_TYPE_TIME_SIGNATURE:
		return "TINYSMF_META_TYPE_TIME_SIGNATURE";

	case TINYSMF_META_TYPE_KEY_SIGNATURE:
		return "TINYSMF_META_TYPE_KEY_SIGNATURE";

	case TINYSMF_META_TYPE_SEQ_SPECIFIC:
		return "TINYSMF_META_TYPE_SEQ_SPECIFIC";
	}

	return "~it is a mystery~";
}

static void
on_meta_event(struct tinysmf_parser_ctx *ctx,
		const struct tinysmf_meta_event *ev)
{
	printf("      - meta event, type %s", str_meta_type(ev->meta_type));

	switch (ev->meta_type) {
	case TINYSMF_META_TYPE_TEXT_EVENT:
	case TINYSMF_META_TYPE_COPYRIGHT_NOTICE:
	case TINYSMF_META_TYPE_TRACK_NAME:
	case TINYSMF_META_TYPE_INSTRUMENT_NAME:
	case TINYSMF_META_TYPE_LYRIC:
	case TINYSMF_META_TYPE_MARKER:
	case TINYSMF_META_TYPE_CUE_POINT:
		printf(": \"%s\"\n", ev->raw);
		break;

	case TINYSMF_META_TYPE_SET_TEMPO:
		printf(": %f\n", ev->cooked.bpm);
		break;

	default:
		puts("");
		break;
	}
}

static void
on_midi_event(struct tinysmf_parser_ctx *ctx,
		const struct tinysmf_midi_event *ev)
{
	struct test_parser *p = (void *) ctx;

	p->midi_events++;

	switch (ev->bytes[0] & 0xF0) {
	case 0x80:
		p->noteoff_events++;
		break;

	case 0x90:
		p->noteon_events++;
		break;
	}
}

static tinysmf_chunk_cb_ret_t
on_track_start(struct tinysmf_parser_ctx *ctx, int track_idx)
{
	struct test_parser *p = (void *) ctx;

	printf(" :: parsing track %d {\n", track_idx);

	p->midi_events        =
		p->noteon_events  =
		p->noteoff_events = 0;

	return TINYSMF_PARSE_CHUNK;
}

static tinysmf_chunk_cb_ret_t
on_track_end(struct tinysmf_parser_ctx *ctx, int track_idx)
{
	struct test_parser *p = (void *) ctx;

	printf("    }\n\n    %d MIDI events (%d note-on, %d note-off)\n\n",
			p->midi_events, p->noteon_events, p->noteoff_events);

	return 0;
}

static void
on_file_info(struct tinysmf_parser_ctx *ctx)
{
	printf(" :: reading a format %d midi file with %d tracks "
			"and a division of %d\n", ctx->file_info.format,
			ctx->file_info.num_tracks, ctx->file_info.division.ppqn);
}

int
main(int argc, char **argv)
{
	struct test_parser p = {
		.ctx = {
			.file_info_cb   = on_file_info,

			.track_start_cb = on_track_start,
			.track_end_cb   = on_track_end,

			.midi_event_cb  = on_midi_event,
			.meta_event_cb  = on_meta_event
		}
	};

	tinysmf_parse_stream((void *) &p, stdin);
	return 0;
}
