#include "stdafx.h"
#pragma hdrstop

#include <msacm.h>

#include "SoundRender_Core.h"
#include "SoundRender_Source.h"

namespace
{
constexpr size_t OggPageHeaderSize = 27;
constexpr size_t OggMaximumPageSize = OggPageHeaderSize + 255 + 255 * 255;

bool ReadOggPage(const u8* data, size_t size, size_t offset, ogg_page& page, size_t& next)
{
	if (offset > size || size - offset < OggPageHeaderSize || memcmp(data + offset, "OggS", 4) || data[offset + 4])
		return false;

	const size_t segment_count = data[offset + 26];
	const size_t header_size = OggPageHeaderSize + segment_count;
	if (header_size > size - offset)
		return false;

	size_t body_size = 0;
	for (size_t i = 0; i < segment_count; ++i)
		body_size += data[offset + OggPageHeaderSize + i];
	if (body_size > size - offset - header_size)
		return false;

	page.header = const_cast<unsigned char*>(data + offset);
	page.header_len = static_cast<long>(header_size);
	page.body = const_cast<unsigned char*>(data + offset + header_size);
	page.body_len = static_cast<long>(body_size);
	next = offset + header_size + body_size;

	unsigned char checked_header[OggPageHeaderSize + 255];
	CopyMemory(checked_header, page.header, header_size);
	ogg_page checked_page = page;
	checked_page.header = checked_header;
	ogg_page_checksum_set(&checked_page);
	if (memcmp(checked_header + 22, page.header + 22, 4))
		return false;
	return true;
}

bool ReadSingleStreamPcmTotal(
	const u8* data, size_t size, const OggVorbis_File& vorbis_file, vorbis_info& info, s64& pcm_total)
{
	if (!data || size < OggPageHeaderSize)
		return false;

	const int serial = vorbis_file.current_serialno;
	ogg_stream_state stream{};
	if (ogg_stream_init(&stream, serial))
		return false;

	bool valid = true;
	u32 header_packets = 0;
	long last_block = -1;
	s64 accumulated = 0;
	s64 initial_pcm = -1;
	size_t offset = 0;
	while (offset < size)
	{
		ogg_page page{};
		size_t next = 0;
		if (!ReadOggPage(data, size, offset, page, next))
		{
			valid = false;
			break;
		}
		offset = next;
		if (header_packets >= 3 && ogg_page_bos(&page))
		{
			valid = false;
			break;
		}
		if (ogg_page_serialno(&page) != serial)
			continue;
		if (ogg_stream_pagein(&stream, &page))
		{
			valid = false;
			break;
		}

		bool setup_completed_on_page = false;
		for (;;)
		{
			ogg_packet packet{};
			const int packet_result = ogg_stream_packetout(&stream, &packet);
			if (!packet_result)
				break;
			if (packet_result < 0)
			{
				valid = false;
				break;
			}
			if (header_packets < 3)
			{
				++header_packets;
				setup_completed_on_page = header_packets == 3;
				continue;
			}
			if (setup_completed_on_page)
			{
				valid = false;
				break;
			}

			const long block = vorbis_packet_blocksize(&info, &packet);
			if (block < 0)
			{
				valid = false;
				break;
			}
			if (last_block != -1)
				accumulated += (last_block + block) >> 2;
			last_block = block;
		}
		if (!valid)
			break;

		const s64 granule = ogg_page_granulepos(&page);
		if (header_packets >= 3 && last_block != -1 && granule >= 0)
		{
			initial_pcm = _max(s64(0), granule - accumulated);
			break;
		}
	}
	ogg_stream_clear(&stream);
	if (!valid || initial_pcm < 0)
		return false;

	const size_t tail_begin = size > OggMaximumPageSize ? size - OggMaximumPageSize : 0;
	s64 final_pcm = -1;
	for (size_t tail = size - OggPageHeaderSize;; --tail)
	{
		if (data[tail] == 'O')
		{
			ogg_page page{};
			size_t next = 0;
			if (ReadOggPage(data, size, tail, page, next) && next == size && ogg_page_eos(&page))
			{
				if (ogg_page_serialno(&page) != serial)
					return false;
				final_pcm = ogg_page_granulepos(&page);
				break;
			}
		}
		if (tail == tail_begin)
			break;
	}
	if (final_pcm < initial_pcm)
		return false;

	pcm_total = final_pcm - initial_pcm;
	return true;
}
}

//	SEEK_SET	0	File beginning
//	SEEK_CUR	1	Current file pointer position
//	SEEK_END	2	End-of-file
int ov_seek_func(void* datasource, s64 offset, int whence)
{
	switch (whence)
	{
	case SEEK_SET: ((IReader*)datasource)->seek((int)offset);
		break;
	case SEEK_CUR: ((IReader*)datasource)->advance((int)offset);
		break;
	case SEEK_END: ((IReader*)datasource)->seek((int)offset + ((IReader*)datasource)->length());
		break;
	}
	return 0;
}

size_t ov_read_func(void* ptr, size_t size, size_t nmemb, void* datasource)
{
	IReader* F = (IReader*)datasource;
	size_t exist_block = _max(0ul, iFloor(F->elapsed() / (float)size));
	size_t read_block = _min(exist_block, nmemb);
	F->r(ptr, (int)(read_block * size));
	return read_block;
}

int ov_close_func(void* datasource)
{
	return 0;
}

long ov_tell_func(void* datasource)
{
	return ((IReader*)datasource)->tell();
}

void CSoundRender_Source::decompress(u32 line, OggVorbis_File* ovf)
{
	VERIFY(ovf);
	// decompression of one cache-line
	u32 line_size = SoundRender->cache.get_linesize();
	char* dest = (char*)SoundRender->cache.get_dataptr(CAT, line);
	u32 buf_offs = (line * line_size) / 2 / m_wformat.nChannels;
	u32 left_file = dwBytesTotal - buf_offs;
	u32 left = (u32)_min(left_file, line_size);

	// seek
	u32 cur_pos = u32(ov_pcm_tell(ovf));
	if (cur_pos != buf_offs)
		ov_pcm_seek(ovf, buf_offs);

	// decompress
	i_decompress_fr(ovf, dest, left);
}

bool CSoundRender_Source::prepare(
	LPCSTR path, PreparedSoundSource& prepared, xr_string& error, bool log_warnings)
{
	PROF_EVENT("Sound: Load ogg");
	prepared = PreparedSoundSource{};
	prepared.path = path;

	// Load file into memory and parse WAV-format
	OggVorbis_File ovf;
	ov_callbacks ovc = {ov_read_func, ov_seek_func, ov_close_func, ov_tell_func};
	IReader* wave = FS.r_open(path);
	if (!wave || !wave->length())
	{
		if (wave)
			FS.r_close(wave);
		error = make_string("Can't open wave file: %s", path).c_str();
		return false;
	}

	const u8* wave_data = static_cast<const u8*>(wave->pointer());
	const size_t wave_size = wave->length();
	// Headers and comments do not require libvorbisfile's full seekable-open pass.
	const int open_result = ov_test_callbacks(wave, &ovf, NULL, 0, ovc);
	if (open_result)
	{
		FS.r_close(wave);
		error = make_string("Invalid OGG stream (%d): %s", open_result, path).c_str();
		return false;
	}

	vorbis_info* ovi = ov_info(&ovf, -1);
	if (!ovi)
	{
		ov_clear(&ovf);
		FS.r_close(wave);
		error = make_string("Invalid source info: %s", path).c_str();
		return false;
	}
	s64 pcm_total = 0;
	if (!ReadSingleStreamPcmTotal(wave_data, wave_size, ovf, *ovi, pcm_total))
	{
		// Chained, multiplexed and unusual streams retain the original full parser.
		const int finish_result = ov_test_open(&ovf);
		if (finish_result)
		{
			FS.r_close(wave);
			error = make_string("Invalid OGG stream (%d): %s", finish_result, path).c_str();
			return false;
		}
		ovi = ov_info(&ovf, -1);
		if (!ovi)
		{
			ov_clear(&ovf);
			FS.r_close(wave);
			error = make_string("Invalid source info: %s", path).c_str();
			return false;
		}
		pcm_total = ov_pcm_total(&ovf, -1);
	}
	//R_ASSERT3(ovi->rate == 44100, "Invalid source rate:", pname.c_str());
	if (ovi->rate != 44100)
	{
		if (log_warnings)
			Msg("! Warning: Invalid source rate: %s", path);
		else
			prepared.warning = PreparedSoundSource::Warning::InvalidRate;
		ov_clear(&ovf);
		FS.r_close(wave);
		return true;
	}
	if (pcm_total < 0)
	{
		ov_clear(&ovf);
		FS.r_close(wave);
		error = make_string("Invalid PCM length: %s", path).c_str();
		return false;
	}

#ifdef DEBUG
    if (log_warnings && ovi->channels == 2)
    {
        Msg("stereo sound source [%s]", path);
    }
#endif // #ifdef DEBUG

	ZeroMemory(&prepared.format, sizeof(WAVEFORMATEX));
	prepared.format.nSamplesPerSec = ovi->rate; //44100;
	prepared.format.wFormatTag = WAVE_FORMAT_PCM;
	prepared.format.nChannels = u16(ovi->channels);
	prepared.format.wBitsPerSample = 16;
	prepared.format.nBlockAlign = prepared.format.wBitsPerSample / 8 * prepared.format.nChannels;
	prepared.format.nAvgBytesPerSec = prepared.format.nSamplesPerSec * prepared.format.nBlockAlign;

	prepared.bytes_total = u32(pcm_total * prepared.format.nBlockAlign);
	prepared.time_total = s_f_def_source_footer + prepared.bytes_total / float(prepared.format.nAvgBytesPerSec);

	vorbis_comment* ovm = ov_comment(&ovf, -1);
	if (ovm && ovm->comments && ovm->user_comments[0] && ovm->comment_lengths[0] >= sizeof(u32))
	{
		IReader F(ovm->user_comments[0], ovm->comment_lengths[0]);
		u32 vers = F.r_u32();
		if (vers == 0x0001 && ovm->comment_lengths[0] >= 16)
		{
			prepared.min_distance = F.r_float();
			prepared.max_distance = F.r_float();
			prepared.base_volume = 1.0f;
			prepared.game_type = F.r_u32();
			prepared.max_ai_distance = prepared.max_distance;
		}
		else if (vers == 0x0002 && ovm->comment_lengths[0] >= 20)
		{
			prepared.min_distance = F.r_float();
			prepared.max_distance = F.r_float();
			prepared.base_volume = F.r_float();
			prepared.game_type = F.r_u32();
			prepared.max_ai_distance = prepared.max_distance;
		}
		else if (vers == OGG_COMMENT_VERSION && ovm->comment_lengths[0] >= 24)
		{
			prepared.min_distance = F.r_float();
			prepared.max_distance = F.r_float();
			prepared.base_volume = F.r_float();
			prepared.game_type = F.r_u32();
			prepared.max_ai_distance = F.r_float();
		} 
		else
		{
			if (log_warnings && strstr(Core.Params, "-dbg"))
			{
				Log("! Invalid ogg-comment version, file: ", path);
			}
			else if (!log_warnings && strstr(Core.Params, "-dbg"))
				prepared.warning = PreparedSoundSource::Warning::InvalidComment;
		}
	}
	else
	{
		if (log_warnings && strstr(Core.Params, "-dbg"))
		{
			Log("! Missing ogg-comment, file: ", path);
		}
		else if (!log_warnings && strstr(Core.Params, "-dbg"))
			prepared.warning = PreparedSoundSource::Warning::MissingComment;
	}
	if (prepared.max_ai_distance < 0.1f || prepared.max_distance < 0.1f)
	{
		ov_clear(&ovf);
		FS.r_close(wave);
		error = make_string("Invalid max distance: %s", path).c_str();
		return false;
	}

	ov_clear(&ovf);
	FS.r_close(wave);
	prepared.loaded = true;

	return true;
}

bool CSoundRender_Source::LoadWave(LPCSTR path)
{
	PreparedSoundSource prepared;
	xr_string error;
	R_ASSERT3(prepare(path, prepared, error), "Can't prepare sound source", error.c_str());
	load_prepared(fname.c_str(), prepared);
	return prepared.loaded;
}

void CSoundRender_Source::resolve_path(LPCSTR name, xr_string& path)
{
	string_path fn;
	strconcat(sizeof(fn), fn, name, ".ogg");
	if (!FS.exist("$level$", fn))
		FS.update_path(fn, "$game_sounds$", fn);

	if (!FS.exist(fn))
	{
		Msg("! Can't find sound '%s'", name);
		FS.update_path(fn, "$game_sounds$", "$no_sound.ogg");
	}
	path = fn;
}

void CSoundRender_Source::load_prepared(LPCSTR name, const PreparedSoundSource& prepared)
{
	fname = name;
	pname = prepared.path.c_str();
	m_wformat = prepared.format;
	fTimeTotal = prepared.time_total;
	dwBytesTotal = prepared.bytes_total;
	m_fBaseVolume = prepared.base_volume;
	m_fMinDist = prepared.min_distance;
	m_fMaxDist = prepared.max_distance;
	m_fMaxAIDist = prepared.max_ai_distance;
	m_uGameType = prepared.game_type;
	if (prepared.loaded)
		SoundRender->cache.cat_create(CAT, dwBytesTotal);
}

void CSoundRender_Source::load(LPCSTR name)
{
	string_path fn, N;
	xr_strcpy(N, name);
	strlwr(N);
	if (strext(N)) *strext(N) = 0;

	fname = N;
	xr_string path;
	resolve_path(N, path);
	xr_strcpy(fn, path.c_str());

	if (LoadWave(fn))
		return;
}

void CSoundRender_Source::unload()
{
	SoundRender->cache.cat_destroy(CAT);
	fTimeTotal = 0.0f;
	dwBytesTotal = 0;
}
