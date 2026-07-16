#include "stdafx.h"
#pragma hdrstop

#include "SoundRender_Core.h"
#include "SoundRender_Source.h"

namespace
{
void NormalizeSourceName(LPCSTR name, string256& id)
{
	xr_strcpy(id, name);
	xr_strlwr(id);
	if (strext(id))
		*strext(id) = 0;
}
}

CSoundRender_Source* CSoundRender_Core::i_create_source(LPCSTR name)
{
	string256 id;
	NormalizeSourceName(name, id);

	{
		std::lock_guard<std::mutex> lock(m_source_prefetch_mutex);
		const auto source = s_sources.find(id);
		if (source != s_sources.end())
			return source->second;
	}

	xr_string path;
	CSoundRender_Source::resolve_path(id, path);
	std::unique_lock<std::mutex> lock(m_source_prefetch_mutex);
	const auto existing = s_sources.find(id);
	if (existing != s_sources.end())
		return existing->second;

	const auto found = m_source_prefetch_by_id.find(id);
	SoundPrefetchJob* job = found == m_source_prefetch_by_id.end() ? nullptr : found->second;
	if (!job || _stricmp(job->path.c_str(), path.c_str()))
	{
		// Level-local and dynamically discovered sounds retain the original synchronous path.
		CSoundRender_Source* source = xr_new<CSoundRender_Source>();
		source->load(id);
		s_sources.insert({id, source});
		return source;
	}

	if (job->state == ESourcePrefetchState::Queued)
	{
		job->state = ESourcePrefetchState::Preparing;
		++m_source_prefetch_promoted;
		lock.unlock();

		PreparedSoundSource prepared;
		xr_string error;
		try
		{
			CSoundRender_Source::prepare(job->path.c_str(), prepared, error);
		}
		catch (...)
		{
			error = make_string("Unhandled exception while preparing sound: %s", job->path.c_str()).c_str();
		}
		finish_source_prepare(*job, std::move(prepared), std::move(error));
		lock.lock();
	}
	else if (job->state == ESourcePrefetchState::Preparing)
	{
		CTimer wait_timer;
		wait_timer.Start();
		m_source_prefetch_changed.wait(lock, [job]()
		{
			return job->state != ESourcePrefetchState::Preparing;
		});
		m_source_prefetch_waited_ms += wait_timer.GetElapsed_ms();
	}

	if (job->state == ESourcePrefetchState::Ready)
		return commit_source_locked(*job);
	if (job->state == ESourcePrefetchState::Committed)
	{
		const auto source = s_sources.find(id);
		R_ASSERT(source != s_sources.end());
		return source->second;
	}

	xr_string error = job->error;
	lock.unlock();
	R_ASSERT3(false, "Can't prepare sound source", error.c_str());
	return nullptr;
}

void CSoundRender_Core::i_destroy_source(CSoundRender_Source* S)
{
	// No actual destroy at all
}

void CSoundRender_Core::build_source_prefetch_manifest()
{
	CTimer timer;
	timer.Start();

	FS_FileSet files;
	FS.file_list(files, "$game_sounds$", FS_ListFiles, "*.ogg");
	for (const FS_File& file : files)
	{
		string256 id;
		NormalizeSourceName(file.name.c_str(), id);
		if (m_source_prefetch_by_id.find(id) != m_source_prefetch_by_id.end())
			continue;

		SoundPrefetchJob* job = xr_new<SoundPrefetchJob>();
		job->id = id;
		string_path path;
		FS.update_path(path, "$game_sounds$", file.name.c_str());
		job->path = path;
		if (const CLocatorAPI::file* descriptor = FS.exist(path))
		{
			job->vfs = descriptor->vfs;
			job->offset = descriptor->ptr;
		}
		m_source_prefetch_jobs.push_back(job);
		m_source_prefetch_order.push_back(job);
		m_source_prefetch_by_id.insert({job->id, job});
	}

	std::sort(m_source_prefetch_order.begin(), m_source_prefetch_order.end(),
		[](const SoundPrefetchJob* left, const SoundPrefetchJob* right)
		{
			if (left->vfs != right->vfs)
				return left->vfs < right->vfs;
			if (left->offset != right->offset)
				return left->offset < right->offset;
			return left->id < right->id;
		});
	m_source_prefetch_enabled = !m_source_prefetch_jobs.empty();
	Msg("* [SOUND PREFETCH] manifest: %u sources in %u ms",
		u32(m_source_prefetch_jobs.size()), timer.GetElapsed_ms());
}

void CSoundRender_Core::source_prefetch_worker()
{
	_initialize_cpu_thread();
	thread_name("Sound prefetch");
	if (!SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN))
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

	for (;;)
	{
		SoundPrefetchJob* job = nullptr;
		{
			std::lock_guard<std::mutex> lock(m_source_prefetch_mutex);
			if (m_source_prefetch_pause || m_source_prefetch_shutdown || m_source_prefetch_failure)
				break;
			while (m_source_prefetch_cursor < m_source_prefetch_order.size())
			{
				SoundPrefetchJob* candidate = m_source_prefetch_order[m_source_prefetch_cursor++];
				if (candidate->state == ESourcePrefetchState::Queued)
				{
					candidate->state = ESourcePrefetchState::Preparing;
					job = candidate;
					break;
				}
			}
			if (!job)
				break;
		}

		PreparedSoundSource prepared;
		xr_string error;
		try
		{
			CSoundRender_Source::prepare(job->path.c_str(), prepared, error, false);
		}
		catch (...)
		{
			error = make_string("Unhandled exception while preparing sound: %s", job->path.c_str()).c_str();
		}
		const bool succeeded = error.empty();
		finish_source_prepare(*job, std::move(prepared), std::move(error));
		if (succeeded)
			Sleep(1);
	}

	{
		std::lock_guard<std::mutex> lock(m_source_prefetch_mutex);
		m_source_prefetch_running = false;
		if (!m_source_prefetch_failure && source_prefetch_remaining_locked() == 0)
			m_source_prefetch_completion_pending = true;
	}
	m_source_prefetch_changed.notify_all();
}

void CSoundRender_Core::finish_source_prepare(
	SoundPrefetchJob& job, PreparedSoundSource&& prepared, xr_string&& error)
{
	{
		std::lock_guard<std::mutex> lock(m_source_prefetch_mutex);
		if (error.empty())
		{
			job.prepared = std::move(prepared);
			job.state = ESourcePrefetchState::Ready;
			++m_source_prefetch_prepared;
		}
		else
		{
			job.error = std::move(error);
			job.state = ESourcePrefetchState::Failed;
			m_source_prefetch_failure = &job;
			m_source_prefetch_pause = true;
			++m_source_prefetch_failed;
		}
		if (!m_source_prefetch_failure && source_prefetch_remaining_locked() == 0)
			m_source_prefetch_completion_pending = true;
	}
	m_source_prefetch_changed.notify_all();
}

CSoundRender_Source* CSoundRender_Core::commit_source_locked(SoundPrefetchJob& job)
{
	const auto existing = s_sources.find(job.id);
	if (existing != s_sources.end())
	{
		job.state = ESourcePrefetchState::Committed;
		return existing->second;
	}

	CSoundRender_Source* source = xr_new<CSoundRender_Source>();
	switch (job.prepared.warning)
	{
	case PreparedSoundSource::Warning::InvalidRate:
		Msg("! Warning: Invalid source rate: %s", job.path.c_str());
		break;
	case PreparedSoundSource::Warning::InvalidComment:
		Log("! Invalid ogg-comment version, file: ", job.path.c_str());
		break;
	case PreparedSoundSource::Warning::MissingComment:
		Log("! Missing ogg-comment, file: ", job.path.c_str());
		break;
	default:
		break;
	}
	source->load_prepared(job.id.c_str(), job.prepared);
	s_sources.insert({job.id, source});
	job.state = ESourcePrefetchState::Committed;
	return source;
}

u32 CSoundRender_Core::source_prefetch_remaining_locked() const
{
	u32 remaining = 0;
	for (const SoundPrefetchJob* job : m_source_prefetch_jobs)
	{
		if (job->state == ESourcePrefetchState::Queued || job->state == ESourcePrefetchState::Preparing)
			++remaining;
	}
	return remaining;
}

u64 CSoundRender_Core::source_prefetch_hash_locked() const
{
	u64 hash = 14695981039346656037ULL;
	const auto append = [&hash](const void* data, size_t size)
	{
		const u8* bytes = static_cast<const u8*>(data);
		for (size_t i = 0; i < size; ++i)
		{
			hash ^= bytes[i];
			hash *= 1099511628211ULL;
		}
	};
	for (const SoundPrefetchJob* job : m_source_prefetch_jobs)
	{
		append(job->id.data(), job->id.size());
		append(&job->prepared.format, sizeof(job->prepared.format));
		append(&job->prepared.time_total, sizeof(job->prepared.time_total));
		append(&job->prepared.bytes_total, sizeof(job->prepared.bytes_total));
		append(&job->prepared.base_volume, sizeof(job->prepared.base_volume));
		append(&job->prepared.min_distance, sizeof(job->prepared.min_distance));
		append(&job->prepared.max_distance, sizeof(job->prepared.max_distance));
		append(&job->prepared.max_ai_distance, sizeof(job->prepared.max_ai_distance));
		append(&job->prepared.game_type, sizeof(job->prepared.game_type));
	}
	return hash;
}

void CSoundRender_Core::source_prefetch_start()
{
	std::unique_lock<std::mutex> lock(m_source_prefetch_mutex);
	if (!m_source_prefetch_enabled || m_source_prefetch_shutdown || m_source_prefetch_running ||
		m_source_prefetch_failure || source_prefetch_remaining_locked() == 0)
		return;

	if (m_source_prefetch_thread.joinable())
	{
		std::thread completed = std::move(m_source_prefetch_thread);
		m_source_prefetch_running = true; // Reserve restart while the completed worker is reaped.
		lock.unlock();
		completed.join();
		lock.lock();
		m_source_prefetch_running = false;
		if (m_source_prefetch_shutdown || m_source_prefetch_failure || source_prefetch_remaining_locked() == 0)
			return;
	}

	m_source_prefetch_pause = false;
	m_source_prefetch_running = true;
	if (!m_source_prefetch_started_at)
		m_source_prefetch_started_at = GetTickCount();
	try
	{
		m_source_prefetch_thread = std::thread(&CSoundRender_Core::source_prefetch_worker, this);
	}
	catch (...)
	{
		m_source_prefetch_pause = true;
		m_source_prefetch_running = false;
		throw;
	}
	Msg("* [SOUND PREFETCH] started: remaining=%u", source_prefetch_remaining_locked());
}

void CSoundRender_Core::source_prefetch_pause()
{
	{
		std::lock_guard<std::mutex> lock(m_source_prefetch_mutex);
		m_source_prefetch_pause = true;
	}
	m_source_prefetch_changed.notify_all();
	if (m_source_prefetch_thread.joinable())
		m_source_prefetch_thread.join();

	std::lock_guard<std::mutex> lock(m_source_prefetch_mutex);
	m_source_prefetch_running = false;
	if (m_source_prefetch_enabled)
		Msg("* [SOUND PREFETCH] paused: prepared=%u, promoted=%u, remaining=%u",
			m_source_prefetch_prepared, m_source_prefetch_promoted, source_prefetch_remaining_locked());
}

void CSoundRender_Core::source_prefetch_stop()
{
	{
		std::lock_guard<std::mutex> lock(m_source_prefetch_mutex);
		m_source_prefetch_shutdown = true;
		m_source_prefetch_pause = true;
	}
	m_source_prefetch_changed.notify_all();
	if (m_source_prefetch_thread.joinable())
		m_source_prefetch_thread.join();
	std::lock_guard<std::mutex> lock(m_source_prefetch_mutex);
	m_source_prefetch_running = false;
}

void CSoundRender_Core::source_prefetch_poll()
{
	xr_string failed_path;
	xr_string failure;
	{
		std::lock_guard<std::mutex> lock(m_source_prefetch_mutex);
		if (m_source_prefetch_shutdown)
			return;
		if (m_source_prefetch_failure)
		{
			failed_path = m_source_prefetch_failure->path;
			failure = m_source_prefetch_failure->error;
		}
		else if (m_source_prefetch_completion_pending && !m_source_prefetch_completion_logged)
		{
			m_source_prefetch_completion_pending = false;
			m_source_prefetch_completion_logged = true;
			Msg("* [SOUND PREFETCH] complete: prepared=%u, promoted=%u, demand_wait=%u ms, "
				"duration=%u ms, metadata_hash=%016llx",
				m_source_prefetch_prepared, m_source_prefetch_promoted, m_source_prefetch_waited_ms,
				GetTickCount() - m_source_prefetch_started_at, source_prefetch_hash_locked());
		}
	}
	if (!failure.empty())
	{
		Msg("! [SOUND PREFETCH] failed: %s (%s)", failed_path.c_str(), failure.c_str());
		R_ASSERT3(false, failure.c_str(), failed_path.c_str());
	}
}

void CSoundRender_Core::clear_source_prefetch()
{
	for (SoundPrefetchJob* job : m_source_prefetch_jobs)
		xr_delete(job);
	m_source_prefetch_jobs.clear();
	m_source_prefetch_order.clear();
	m_source_prefetch_by_id.clear();
}
