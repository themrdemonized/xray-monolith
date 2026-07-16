#ifndef SoundRender_SourceH
#define SoundRender_SourceH
#pragma once

#include "SoundRender_Cache.h"

// refs
struct OggVorbis_File;

struct PreparedSoundSource
{
	enum class Warning : u8
	{
		None,
		InvalidRate,
		InvalidComment,
		MissingComment
	};

	xr_string path;
	WAVEFORMATEX format{};
	float time_total = 0.f;
	u32 bytes_total = 0;
	float base_volume = 1.f;
	float min_distance = 1.f;
	float max_distance = 300.f;
	float max_ai_distance = 300.f;
	u32 game_type = 0;
	bool loaded = false;
	Warning warning = Warning::None;
};

class XRSOUND_EDITOR_API CSoundRender_Source : public CSound_source
{
public:
	shared_str pname;
	shared_str fname;
	cache_cat CAT;

	float fTimeTotal;
	u32 dwBytesTotal;

	WAVEFORMATEX m_wformat;

	float m_fBaseVolume;
	float m_fMinDist;
	float m_fMaxDist;
	float m_fMaxAIDist;
	u32 m_uGameType;
private:
	void i_decompress_fr(OggVorbis_File* ovf, char* dest, u32 size);
	bool LoadWave(LPCSTR name);
public:
	CSoundRender_Source();
	~CSoundRender_Source();

	void load(LPCSTR name);
	void load_prepared(LPCSTR name, const PreparedSoundSource& prepared);
	void unload();
	void decompress(u32 line, OggVorbis_File* ovf);
	static bool prepare(LPCSTR path, PreparedSoundSource& prepared, xr_string& error, bool log_warnings = true);
	static void resolve_path(LPCSTR name, xr_string& path);

	virtual float length_sec() const { return fTimeTotal; }
	virtual u32 game_type() const { return m_uGameType; }
	virtual LPCSTR file_name() const { return *fname; }
	virtual float base_volume() const { return m_fBaseVolume; }
	virtual u16 channels_num() const { return m_wformat.nChannels; }
	virtual u32 bytes_total() const { return dwBytesTotal; }
};
#endif
