/*
The timeline layout and interaction follow GpuTimeline.cs of s&box (Facepunch/sbox-public)
The Overview and CPU tabs follow its HubOverviewTab.cs, HubDetailTabs.cs and HubPrimitives.cs
Measure, the hitch rule, the target and Judge follow its PerfHub.Analysis.cs

MIT License

Copyright (c) 2025 Facepunch Studios Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "stdafx.h"
#pragma hdrstop
#include "dx10GpuTimeline.h"
#include "../xrRender/HW.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <atomic>

extern int ps_framelimiter;
extern float GetMonitorRefresh();

namespace gpu_timeline
{
namespace
{
	// Frames in flight before a frame's results are read, a frame still unready then is dropped
	const u32 ring = 6;
	const u32 max_rows = 10;
	const u32 log_cap = 36000;
	const u32 hist_cap = 2048;
	const u32 window_frames = 60;
	const double window_sec = 1.0;
	const double update_interval = 0.25;

	// The frame bracket rows come before every scope name, and no scope name has a space
	const u32 frame_row = 0;
	const u32 gap_row = 1;
	const u32 present_row = 2;
	const u32 between_row = 3;
	const u32 bracket_rows = 4;

	const u32 perf_cap = 512;
	const u32 task_count = CRenderDevice::FrameRecord::task_count;
	const u32 hist_buckets = 48;
	const u32 top_rows = 8;

	// Draws with and without the nested scopes, polys, verts, then the pipeline statistics
	enum { count_draws, count_draws_self, count_polys, count_verts, count_vs, count_clip_in, count_clip_out, count_ps, count_cs, count_fields };
	const u32 pipe_first = count_vs;

	struct scope_rec
	{
		LPCWSTR name;
		u32 parent;
		u32 depth;
		u64 cpu_begin;
		u64 cpu_end;
		// RCache counters at the begin, their change by the end
		u32 draws;
		u32 polys;
		u32 verts;
		u32 pipe;
	};

	struct frame_slot
	{
		ID3D11Query* disjoint = nullptr;
		// Frame start, frame end, after Present, and the next frame's first stamp
		ID3D11Query* bracket[4] = {};
		xr_vector<ID3D11Query*> stamps;
		xr_vector<ID3D11Query*> pipes;
		u32 pipes_used = 0;
		xr_vector<scope_rec> scopes;
		u64 cpu_begin = 0;
		u64 cpu_end = 0;
		u32 frame = 0;
		bool started = false;
		bool ended = false;
		bool presented = false;
		bool followed = false;
		bool pending = false;
	};

	struct event
	{
		u32 name;
		u32 parent;
		double start;
		double end;
		double self;
		double cpu;
		double cpu_self;
		double counts[count_fields];
		bool piped;
	};

	struct resolved_frame
	{
		u32 frame = 0;
		// Bracket length and the part of it no scope covers, negative without a bracket
		double gpu = -1.0;
		double gap = 0.0;
		xr_vector<event> events;
	};

	struct frame_acc
	{
		double gpu = 0.0;
		double self = 0.0;
		double cpu = 0.0;
		u32 calls = 0;
		bool seen = false;
		double cpu_self = 0.0;
		double counts[count_fields] = {};
		bool piped = false;
	};

	struct sample
	{
		u32 frame;
		float gpu;
		float self;
		float cpu;
		u32 calls;
		float cpu_self;
		float counts[count_fields];
		bool piped;
	};

	struct history
	{
		xr_vector<sample> samples;
		u32 next = 0;
	};

	struct frame_mark
	{
		u32 frame;
		u64 time;
	};

	struct log_stat
	{
		xr_vector<float> gpu;
		xr_vector<float> self;
		xr_vector<float> cpu;
		u32 calls = 0;
		// Count sums, the pipeline ones over the frames that read them
		double counts[count_fields] = {};
		u32 piped = 0;
	};

	// One frame of the device's frame record in ms, keyed by its frame number
	struct perf_frame
	{
		u32 frame;
		float interval;
		float cap;
		float present;
		float join;
		float hub;
		float gpu;
		float task[task_count];
		// Render thread spins on the occlusion and sync readbacks
		float wait;
		bool presented;
	};

	struct perf_stat
	{
		float avg;
		float p50;
		float p99;
		float max;
	};

	enum severity { sev_good, sev_info, sev_warning, sev_bad };
	enum limiter { limit_unknown, limit_cap, limit_vsync, limit_gpu, limit_cpu };

	struct perf_summary
	{
		u32 frames = 0;
		float window_ms = 0.f;
		perf_stat frame = {}, cpu = {}, gpu = {}, cap = {}, present = {}, join = {}, hub = {};
		perf_stat task[task_count] = {};
		// Readback spins alone, and with Present as the vsync wait
		perf_stat wait = {}, vsync_wait = {};
		float std_dev = 0.f;
		float target_ms = 0.f;
		LPCSTR target_source = "";
		float hitch_ms = 0.f;
		u32 spikes = 0;
		float worst_spike = 0.f;
		float not_presented = 0.f;
		limiter limit = limit_unknown;
		severity rating = sev_info;
		bool meeting_target = false;
		bool vsync = false;
		u32 buckets[hist_buckets] = {};
		u32 hitch_bucket = 0;
		float range = 0.f;
	};

	frame_slot slots[ring];
	frame_slot* cur = nullptr;
	u32 cur_index = 0;
	xr_vector<u32> stack;
	DWORD thread = 0;

	xr_vector<xr_string> names;
	xr_map<LPCWSTR, u32> name_ids;

	resolved_frame latest;
	xr_vector<frame_acc> acc;
	xr_vector<history> hist;
	xr_vector<log_stat> log_acc;
	frame_mark marks[hist_cap];
	u32 marks_next = 0;
	u32 marks_count = 0;

	xr_vector<UINT64> poll_stamps;
	xr_vector<D3D11_QUERY_DATA_PIPELINE_STATISTICS> poll_pipes;
	xr_vector<double> cover;
	xr_vector<double> cover_end;
	xr_vector<u32> top;

	perf_frame perf[perf_cap];
	u32 perf_newest = 0;
	// Bit per task stamped since the window opened, the hub leaves the others out
	u32 tasks_stamped = 0;
	xr_vector<const perf_frame*> perf_window;
	xr_vector<float> perf_sorted;

	u32 peak_scopes = 0;
	u32 frames_dropped = 0;
	u32 frames_disjoint = 0;
	u32 scopes_dropped = 0;
	u32 scopes_foreign = 0;
	u32 log_frames = 0;
	u32 log_dropped = 0;
	bool log_armed = false;

	std::atomic<bool> log_requested = false;
	std::atomic<bool> frozen = false;

	// Local and non-local video memory against the budget the OS gives the game
	IDXGIAdapter3* vram_adapter = nullptr;
	DXGI_QUERY_VIDEO_MEMORY_INFO vram[2] = {};
	bool vram_read = false;

	bool read_vram()
	{
		if (!vram_adapter && FAILED(HW.m_pAdapter->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&vram_adapter)))
			return false;
		return SUCCEEDED(vram_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vram[0])) &&
			SUCCEEDED(vram_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &vram[1]));
	}

	void add_name(const xr_string& name)
	{
		names.push_back(name);
		acc.emplace_back();
		hist.emplace_back();
		log_acc.emplace_back();
	}

	u32 name_id(LPCWSTR name)
	{
		const auto it = name_ids.find(name);
		if (it != name_ids.end())
			return it->second;

		// Scope names are identifiers, so narrowing each character is enough
		xr_string narrow;
		for (LPCWSTR c = name; *c; ++c)
			narrow += char(*c);
		u32 id = 0;
		while (id < names.size() && names[id] != narrow)
			++id;
		if (id == names.size())
			add_name(narrow);
		name_ids[name] = id;
		return id;
	}

	bool render_thread()
	{
		const DWORD id = GetCurrentThreadId();
		if (!thread)
			thread = id;
		return id == thread;
	}

	// Sorts in place for the median and the nearest rank 95th percentile
	void spread(xr_vector<float>& v, float& median, float& p95)
	{
		std::sort(v.begin(), v.end());
		const u32 n = v.size();
		median = n & 1 ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
		p95 = v[(n * 95 + 99) / 100 - 1];
	}

	// The noun a printed count takes
	LPCSTR noun(u32 n, LPCSTR one, LPCSTR many)
	{
		return n == 1 ? one : many;
	}

	// Union of intervals clipped to lo..hi, each entry starting no earlier than the one before
	void add_cover(double& covered, double& reach, double start, double end, double lo, double hi)
	{
		const double b = _max(start, _max(reach, lo));
		const double e = _min(end, hi);
		if (e > b)
			covered += e - b;
		reach = _max(reach, end);
	}

#if XRCORE_PROFILER == PROFILER_OPTICK
	// While a profiler capture runs, each resolved frame's scopes go to an Optick storage called GPU
	// GPU ticks map to the CPU clock by a line fitted to stamps taken on an idle GPU at capture start and stop
	enum prof_phase { prof_off, prof_starting, prof_on };

	struct prof_scope
	{
		const Optick::EventDescription* desc;
		u32 parent;
		UINT64 begin;
		UINT64 end;
	};

	struct prof_frame
	{
		u32 first;
		u32 count;
		bool bracketed;
		bool presented;
		bool followed;
		UINT64 begin;
		UINT64 end;
		UINT64 present;
		UINT64 next;
	};

	// One stamp and the CPU clock before its submit and after its readback
	struct clock_sample
	{
		UINT64 gpu;
		u64 before;
		u64 after;
	};

	const u32 clock_burst = 64;

	std::atomic<int> prof_now = prof_off;
	xrCriticalSection prof_lock;
	bool prof_forced = false;
	u32 prof_first = 0;
	UINT64 prof_gpu_freq = 0;
	xr_vector<prof_scope> prof_scopes;
	xr_vector<prof_frame> prof_frames;
	xr_vector<clock_sample> prof_clock;
	xr_vector<const Optick::EventDescription*> prof_names;
	ID3D11Query* prof_idle = nullptr;
	ID3D11Query* prof_stamp = nullptr;
	ID3D11Query* prof_disjoint = nullptr;

	bool wait_query(ID3D11Query* q, void* data, UINT size)
	{
		HRESULT hr;
		while ((hr = HW.pContext->GetData(q, data, size, 0)) == S_FALSE)
			;
		return hr == S_OK;
	}

	// Each stamp waits for an idle GPU, so it runs as soon as its flush reaches the GPU
	void prof_burst()
	{
		const D3D11_QUERY_DESC desc[] = {{D3D11_QUERY_EVENT, 0}, {D3D11_QUERY_TIMESTAMP, 0}, {D3D11_QUERY_TIMESTAMP_DISJOINT, 0}};
		ID3D11Query** queries[] = {&prof_idle, &prof_stamp, &prof_disjoint};
		for (u32 i = 0; i < 3; ++i)
			if (!*queries[i] && FAILED(HW.pDevice->CreateQuery(&desc[i], queries[i])))
				return;

		const u32 base = prof_clock.size();
		bool ok = true;
		HW.pContext->Begin(prof_disjoint);
		for (u32 i = 0; ok && i < clock_burst; ++i)
		{
			BOOL done = FALSE;
			HW.pContext->End(prof_idle);
			ok = wait_query(prof_idle, &done, sizeof(done));
			clock_sample c;
			c.before = CPU::QPC();
			HW.pContext->End(prof_stamp);
			HW.pContext->Flush();
			ok = ok && wait_query(prof_stamp, &c.gpu, sizeof(c.gpu));
			c.after = CPU::QPC();
			prof_clock.push_back(c);
		}
		HW.pContext->End(prof_disjoint);
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
		if (!wait_query(prof_disjoint, &dj, sizeof(dj)) || dj.Disjoint || !dj.Frequency || !ok)
		{
			prof_clock.resize(base);
			return;
		}
		prof_gpu_freq = dj.Frequency;
	}

	void prof_record(const frame_slot& s, const UINT64* stamps, const UINT64* bracket, const xr_vector<event>& ev)
	{
		xrCriticalSectionGuard guard(prof_lock);
		const u32 first = prof_scopes.size();
		for (u32 i = 0; i < ev.size(); ++i)
		{
			const u32 id = ev[i].name;
			if (prof_names.size() <= id)
				prof_names.resize(id + 1, nullptr);
			if (!prof_names[id])
				prof_names[id] = Optick::EventDescription::CreateShared(names[id].c_str());
			prof_scopes.push_back({prof_names[id], ev[i].parent, stamps[2 * i], stamps[2 * i + 1]});
		}
		const bool presented = s.started && s.ended && s.presented;
		prof_frames.push_back({first, u32(ev.size()), s.started && s.ended, presented, presented && s.followed, bracket[0], bracket[1], bracket[2], bracket[3]});
	}

	// Least squares over the window midpoints, with the nominal rate when only one burst landed
	// Each sample weighs 1 over its half window squared, so a slow readback barely moves the line
	void prof_emit()
	{
		const u32 n = prof_clock.size();
		if (n < 2 || !prof_gpu_freq)
		{
			Msg("! [gpu_timeline] profiler capture has no GPU clock map, its GPU row stays empty");
			return;
		}
		const double g0 = double(prof_clock[0].gpu);
		const double c0 = double(prof_clock[0].before);
		const double nominal = double(CPU::qpc_freq) / double(prof_gpu_freq);
		auto x = [g0](const clock_sample& c) { return double(c.gpu) - g0; };
		auto y = [c0](const clock_sample& c) { return 0.5 * (double(c.before) + double(c.after)) - c0; };
		auto weight = [](const clock_sample& c) {
			const double half = _max(0.5, 0.5 * double(c.after - c.before));
			return 1.0 / (half * half);
		};
		double sw = 0.0, mx = 0.0, my = 0.0;
		for (const clock_sample& c : prof_clock)
		{
			const double w = weight(c);
			sw += w;
			mx += w * x(c);
			my += w * y(c);
		}
		mx /= sw;
		my /= sw;
		double k = nominal;
		if (n > clock_burst)
		{
			double sxx = 0.0, sxy = 0.0;
			for (const clock_sample& c : prof_clock)
			{
				const double w = weight(c);
				sxx += w * (x(c) - mx) * (x(c) - mx);
				sxy += w * (x(c) - mx) * (y(c) - my);
			}
			if (sxx > 0.0)
				k = sxy / sxx;
		}
		const double a = my - k * mx;
		auto to_cpu = [&](UINT64 g) { return int64_t(c0 + a + k * (double(g) - g0) + 0.5); };

		static Optick::EventStorage* storage = Optick::RegisterStorage("GPU", uint64_t(-1), Optick::ThreadMask::GPU);
		static const Optick::EventDescription* present_desc = Optick::EventDescription::CreateShared("frame present");
		static const Optick::EventDescription* between_desc = Optick::EventDescription::CreateShared("between frames");
		xr_vector<s64> begin, end, last;
		for (const prof_frame& f : prof_frames)
		{
			if (f.bracketed)
			{
				Optick::BeginFrame(Optick::FrameType::GPU, to_cpu(f.begin), thread);
				Optick::EndFrame(Optick::FrameType::GPU, to_cpu(f.end), thread);
			}
			// The viewer nests by time alone, so siblings start after the one before and children stay inside their parent
			begin.resize(f.count);
			end.resize(f.count);
			// std::min and std::max stay exact at the s64 limits used as bounds here
			last.assign(f.count + 1, std::numeric_limits<s64>::min());
			for (u32 i = 0; i < f.count; ++i)
			{
				const prof_scope& p = prof_scopes[f.first + i];
				const u32 up = p.parent == no_scope ? f.count : p.parent;
				s64 lo = last[up], hi = std::numeric_limits<s64>::max();
				if (p.parent != no_scope)
				{
					lo = std::max(lo, begin[p.parent]);
					hi = end[p.parent];
				}
				begin[i] = std::min(std::max(to_cpu(p.begin), lo), hi);
				end[i] = std::min(std::max(to_cpu(p.end), begin[i]), hi);
				// Equal intervals sort in either order, so a child matching its parent ends a tick early
				if (p.parent != no_scope && begin[i] == begin[p.parent] && end[i] == hi && end[i] > begin[i])
					--end[i];
				last[up] = end[i];
				Optick::Event::Add(storage, p.desc, begin[i], end[i]);
			}
			// Present and the wait for the next frame follow the frame end as top level rows
			if (f.presented)
			{
				const s64 frame_end = std::max(to_cpu(f.end), last[f.count]);
				const s64 present_end = std::max(to_cpu(f.present), frame_end);
				Optick::Event::Add(storage, present_desc, frame_end, present_end);
				if (f.followed)
					Optick::Event::Add(storage, between_desc, present_end, std::max(to_cpu(f.next), present_end));
			}
		}
	}
#endif

	void publish(const frame_slot& s, const UINT64* stamps, const UINT64* bracket, const D3D11_QUERY_DATA_PIPELINE_STATISTICS* pipes, double freq)
	{
		if (names.empty())
		{
			add_name("GPU frame");
			add_name("no scope");
			add_name("frame present");
			add_name("between frames");
		}

		const u32 n = s.scopes.size();
		UINT64 first = s.started ? bracket[0] : stamps[0];
		UINT64 last = s.ended ? bracket[1] : first;
		for (u32 i = 0; i < n; ++i)
		{
			first = _min(first, stamps[2 * i]);
			last = _max(last, _max(stamps[2 * i], stamps[2 * i + 1]));
		}
		const double span = double(last - first) * freq;
		const double cpu_freq = 1000.0 / double(CPU::qpc_freq);

		latest.frame = s.frame;
		latest.events.clear();
		for (u32 i = 0; i < n; ++i)
		{
			const scope_rec& r = s.scopes[i];
			const UINT64 b = stamps[2 * i];
			const UINT64 e = _max(b, stamps[2 * i + 1]);
			const double cpu = r.cpu_end > r.cpu_begin ? double(r.cpu_end - r.cpu_begin) * cpu_freq : 0.0;
			latest.events.push_back({name_id(r.name), r.parent, double(b - first) * freq, double(e - first) * freq, 0.0, cpu, cpu,
				{double(r.draws), double(r.draws), double(r.polys), double(r.verts)}, r.pipe != no_scope});
			if (r.pipe == no_scope)
				continue;
			const D3D11_QUERY_DATA_PIPELINE_STATISTICS& p = pipes[r.pipe];
			double* c = latest.events.back().counts;
			c[count_vs] = double(p.VSInvocations);
			c[count_clip_in] = double(p.CInvocations);
			c[count_clip_out] = double(p.CPrimitives);
			c[count_ps] = double(p.PSInvocations);
			c[count_cs] = double(p.CSInvocations);
		}

		// Self time is a scope's duration minus the union of its children
		// On the CPU children run one after another on this thread, so their sum is subtracted
		xr_vector<event>& ev = latest.events;
#if XRCORE_PROFILER == PROFILER_OPTICK
		if (prof_now == prof_on && s.frame >= prof_first)
			prof_record(s, stamps, bracket, ev);
#endif
		cover.assign(n, 0.0);
		cover_end.assign(n, 0.0);
		top.clear();
		for (u32 i = 0; i < n; ++i)
		{
			const u32 p = ev[i].parent;
			if (p == no_scope)
				top.push_back(i);
			else
			{
				add_cover(cover[p], cover_end[p], ev[i].start, ev[i].end, ev[p].start, ev[p].end);
				ev[p].cpu_self -= ev[i].cpu;
				ev[p].counts[count_draws_self] -= ev[i].counts[count_draws];
			}
		}
		for (u32 i = 0; i < n; ++i)
		{
			ev[i].self = _max(0.0, ev[i].end - ev[i].start - cover[i]);
			ev[i].cpu_self = _max(0.0, ev[i].cpu_self);
		}

		std::sort(top.begin(), top.end(), [&](u32 a, u32 b) { return ev[a].start < ev[b].start; });
		double covered = 0.0, reach = 0.0;
		for (u32 i : top)
			add_cover(covered, reach, ev[i].start, ev[i].end, 0.0, span);
		const bool bracketed = s.started && s.ended;
		latest.gpu = bracketed ? span : -1.0;
		latest.gap = bracketed ? _max(0.0, span - covered) : 0.0;
		perf_frame& pf = perf[s.frame % perf_cap];
		if (bracketed && pf.frame == s.frame)
			pf.gpu = float(latest.gpu);

		// Scopes nested inside one of the same name add only their self time
		for (const event& e : ev)
		{
			frame_acc& a = acc[e.name];
			a.self += e.self;
			a.cpu_self += e.cpu_self;
			a.counts[count_draws_self] += e.counts[count_draws_self];
			a.seen = true;
			bool nested = false;
			for (u32 p = e.parent; p != no_scope && !nested; p = ev[p].parent)
				nested = ev[p].name == e.name;
			if (nested)
				continue;
			a.gpu += e.end - e.start;
			a.cpu += e.cpu;
			++a.calls;
			for (u32 k = 0; k < count_fields; ++k)
				if (k != count_draws_self)
					a.counts[k] += e.counts[k];
			a.piped = a.piped || e.piped;
		}
		if (bracketed)
		{
			const double cpu = s.cpu_end > s.cpu_begin ? double(s.cpu_end - s.cpu_begin) * cpu_freq : 0.0;
			acc[frame_row] = {latest.gpu, latest.gap, cpu, 1, true};
			acc[gap_row] = {latest.gap, latest.gap, 0.0, 0, true};
			// From the frame end to the stamp after Present, then on to the next frame's first stamp
			if (s.presented)
			{
				const double present = bracket[2] > bracket[1] ? double(bracket[2] - bracket[1]) * freq : 0.0;
				acc[present_row] = {present, present, 0.0, 0, true};
				if (s.followed)
				{
					const double between = bracket[3] > bracket[2] ? double(bracket[3] - bracket[2]) * freq : 0.0;
					acc[between_row] = {between, between, 0.0, 0, true};
				}
			}
		}

		marks[marks_next] = {s.frame, CPU::QPC()};
		marks_next = (marks_next + 1) % hist_cap;
		marks_count = _min(marks_count + 1, hist_cap);

		// The log keeps per frame values from one log request to the next, up to log_cap frames
		const bool logging = log_armed && log_frames < log_cap;
		for (u32 id = 0; id < names.size(); ++id)
		{
			frame_acc& a = acc[id];
			if (!a.seen)
				continue;
			sample v = {s.frame, float(a.gpu), float(a.self), float(a.cpu), a.calls, float(a.cpu_self)};
			for (u32 k = 0; k < count_fields; ++k)
				v.counts[k] = float(a.counts[k]);
			v.piped = a.piped;
			history& h = hist[id];
			if (h.samples.size() < hist_cap)
				h.samples.push_back(v);
			else
				h.samples[h.next] = v;
			h.next = (h.next + 1) % hist_cap;
			if (logging)
			{
				log_stat& l = log_acc[id];
				l.gpu.push_back(v.gpu);
				l.self.push_back(v.self);
				l.cpu.push_back(v.cpu);
				l.calls += v.calls;
				for (u32 k = 0; k < count_fields; ++k)
					l.counts[k] += a.counts[k];
				l.piped += a.piped;
			}
			a = frame_acc();
		}
		log_frames += logging;
	}

	// Reads finished frames oldest first without flushing, never waits
	void poll()
	{
		for (u32 k = 1; k <= ring; ++k)
		{
			frame_slot& s = slots[(cur_index + k) % ring];
			if (!s.pending)
				continue;
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
			if (HW.pContext->GetData(s.disjoint, &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;
			s.pending = false;
			if (dj.Disjoint || !dj.Frequency)
			{
				++frames_disjoint;
				continue;
			}
			poll_stamps.resize(s.scopes.size() * 2);
			UINT64 bracket[4] = {};
			bool ok = true;
			for (u32 i = 0; ok && i < poll_stamps.size(); ++i)
				ok = HW.pContext->GetData(s.stamps[i], &poll_stamps[i], sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
			const bool written[4] = {s.started, s.ended, s.presented, s.followed};
			for (u32 i = 0; ok && i < 4; ++i)
				if (written[i])
					ok = HW.pContext->GetData(s.bracket[i], &bracket[i], sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
			poll_pipes.resize(s.pipes_used);
			for (u32 i = 0; ok && i < s.pipes_used; ++i)
				ok = HW.pContext->GetData(s.pipes[i], &poll_pipes[i], sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
			if (!ok)
			{
				++frames_dropped;
				++log_dropped;
				continue;
			}
			if (!s.scopes.empty() || (s.started && s.ended))
				publish(s, poll_stamps.data(), bracket, poll_pipes.data(), 1000.0 / double(dj.Frequency));
		}
	}

#if XRCORE_PROFILER == PROFILER_OPTICK
	void prof_reset()
	{
		prof_now = prof_off;
		prof_scopes.clear();
		prof_frames.clear();
		prof_clock.clear();
		if (prof_forced)
			ps_r__gpu_timeline = 0;
		prof_forced = false;
	}

	// On the render thread between frames the open frame closes and the burst drains the GPU, so every captured frame resolves
	void prof_stop()
	{
		if (prof_now == prof_on && thread == GetCurrentThreadId() && stack.empty())
		{
			if (cur)
			{
				HW.pContext->End(cur->disjoint);
				cur->pending = cur->ended;
				cur = nullptr;
			}
			prof_burst();
			poll();
		}
		xrCriticalSectionGuard guard(prof_lock);
		if (prof_now == prof_on)
			prof_emit();
		prof_reset();
	}

	// Optick calls this before it changes state, so the GPU row is filled while the capture is still on
	bool prof_state_changed(Optick::State::Type state)
	{
		if (state == Optick::State::START_CAPTURE)
		{
			xrCriticalSectionGuard guard(prof_lock);
			prof_reset();
			prof_forced = !ps_r__gpu_timeline;
			ps_r__gpu_timeline = 1;
			prof_now = prof_starting;
		}
		else if (state == Optick::State::STOP_CAPTURE)
			prof_stop();
		else if (state == Optick::State::CANCEL_CAPTURE)
		{
			xrCriticalSectionGuard guard(prof_lock);
			prof_reset();
		}
		return true;
	}

	const bool prof_hooked = Optick::SetStateChangedCallback(prof_state_changed);
#endif

	float display_hz = 60.f;
	float eco_hz = 60.f;

	// Current refresh of the monitor holding the game window, the rate a windowed vsync paces to
	float window_refresh(float fallback)
	{
		MONITORINFOEXA mi = {};
		mi.cbSize = sizeof(mi);
		DEVMODEA dm = {};
		dm.dmSize = sizeof(dm);
		if (GetMonitorInfoA(MonitorFromWindow(HW.m_hWnd, MONITOR_DEFAULTTONEAREST), &mi) &&
			EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
			return float(dm.dmDisplayFrequency);
		return fallback;
	}

	// The pause and menu cap waits on the device refresh, a windowed vsync on the monitor's current one
	void read_refresh()
	{
		const float period = refresh_rate > 0.f ? refresh_rate : GetMonitorRefresh();
		eco_hz = period > 0.f ? 1.f / period : 60.f;
		display_hz = HW.m_ChainDescFullscreen.Windowed ? window_refresh(eco_hz) : eco_hz;
	}

	// Pause and the main menu cap frames in the same loop as the frame cap
	bool eco_capped()
	{
		return Device.Paused() || (g_pGamePersistent && g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->IsActive());
	}

	// Average, median, 99th percentile and max of one series over the window
	template <typename F>
	perf_stat measure(F value, bool skip_zero = false)
	{
		perf_sorted.clear();
		double sum = 0.0;
		for (const perf_frame* p : perf_window)
		{
			const float v = value(*p);
			if (skip_zero && v <= 0.f)
				continue;
			perf_sorted.push_back(v);
			sum += v;
		}
		const u32 n = perf_sorted.size();
		if (!n)
			return {};
		std::sort(perf_sorted.begin(), perf_sorted.end());
		const u32 p99 = _min(u32(ceilf(0.99f * float(n - 1))), n - 1);
		return {float(sum / n), perf_sorted[n / 2], perf_sorted[p99], perf_sorted[n - 1]};
	}

	// Whether the target is met and what most likely limits the frame, an inference from the timings
	void judge(perf_summary& s)
	{
		// Judged without the hub's own cost
		const float frame = _max(0.f, s.frame.avg - s.hub.avg);
		const float refresh = 1000.f / display_hz;
		// Whole refreshes per frame, paced when vsync holds the average on one of them
		const float steps = floorf(s.frame.avg / refresh + 0.5f);
		const bool paced = s.vsync && steps >= 1.f && _abs(s.frame.avg - steps * refresh) < refresh * 0.05f;
		// With vsync off the Present and readback waits are the GPU holding the CPU back
		const float gpu_wait = s.vsync ? 0.f : s.present.avg + s.wait.avg;
		if (s.cap.avg > frame * 0.1f)
			s.limit = limit_cap;
		// Under vsync the GPU stamps also hold the wait for a free back buffer, so frames paced at one refresh are vsync limited
		else if (paced && steps == 1.f)
			s.limit = limit_vsync;
		// Paced at several refreshes, the CPU is the limit when its own work and the worker join outlast all but the last
		else if (paced)
			s.limit = s.cpu.avg + s.join.avg > (steps - 1.f) * refresh ? limit_cpu : limit_gpu;
		else if ((s.gpu.avg > 0.f && s.gpu.avg >= frame * 0.85f) || gpu_wait > frame * 0.1f)
			s.limit = limit_gpu;
		else
			s.limit = limit_cpu;

		s.meeting_target = frame <= s.target_ms * 1.05f || s.limit == limit_cap || s.limit == limit_vsync;
		s.rating = s.meeting_target ? (s.spikes ? sev_warning : sev_good) : frame <= s.target_ms * 1.5f ? sev_warning : sev_bad;
	}

	// Frame stats over the recorded frames from the stats window's oldest frame on
	void summarise(perf_summary& s, u32 from)
	{
		s = perf_summary();
		perf_window.clear();
		for (u32 f = perf_newest; f && f >= from && f + perf_cap > perf_newest; --f)
			if (perf[f % perf_cap].frame == f)
				perf_window.push_back(&perf[f % perf_cap]);

		// The frame cap if there is one, else the pause or menu cap, else the display refresh
		s.vsync = psDeviceFlags.test(rsVSync);
		s.target_ms = 1000.f / (ps_framelimiter > 0 ? float(ps_framelimiter) : eco_capped() ? eco_hz : display_hz);
		s.target_source = ps_framelimiter > 0 ? "r__framelimit" : eco_capped() ? "pause or menu cap" : "display refresh";

		const u32 n = perf_window.size();
		s.frames = n;
		if (n < 8)
			return;

		s.frame = measure([](const perf_frame& p) { return p.interval; });
		s.cpu = measure([](const perf_frame& p) { return _max(0.f, p.interval - p.cap - p.present - p.wait - p.join - p.hub); });
		s.gpu = measure([](const perf_frame& p) { return p.gpu; }, true);
		s.cap = measure([](const perf_frame& p) { return p.cap; });
		s.present = measure([](const perf_frame& p) { return p.present; });
		s.join = measure([](const perf_frame& p) { return p.join; });
		s.hub = measure([](const perf_frame& p) { return p.hub; });
		s.wait = measure([](const perf_frame& p) { return p.wait; });
		s.vsync_wait = measure([](const perf_frame& p) { return p.present + p.wait; });
		for (u32 t = 0; t < task_count; ++t)
			s.task[t] = measure([t](const perf_frame& p) { return p.task[t]; });

		double variance = 0.0;
		u32 skipped = 0;
		for (const perf_frame* p : perf_window)
		{
			s.window_ms += p->interval;
			variance += _sqr(double(p->interval) - s.frame.avg);
			skipped += !p->presented;
		}
		s.std_dev = float(_sqrt(variance / n));
		s.not_presented = float(skipped) / float(n);

		// Hitches are frames you would feel, twice the usual and well over the target
		s.hitch_ms = _max(s.frame.p50 * 2.f, s.target_ms * 1.5f);
		// The range reaches past the hitch line and puts it on a bin edge, so no bin mixes hitches with other frames
		const float range = _max(s.frame.p99 * 1.3f, s.target_ms * 1.4f);
		s.hitch_bucket = clampr(u32(s.hitch_ms / range * hist_buckets), 1u, hist_buckets - 1);
		s.range = s.hitch_ms * hist_buckets / s.hitch_bucket;
		for (const perf_frame* p : perf_window)
		{
			const int b = int(p->interval / s.range * hist_buckets);
			const bool hitch = p->interval >= s.hitch_ms;
			++s.buckets[hitch ? clampr(b, int(s.hitch_bucket), int(hist_buckets) - 1) : clampr(b, 0, int(s.hitch_bucket) - 1)];
			if (!hitch)
				continue;
			++s.spikes;
			s.worst_spike = _max(s.worst_spike, p->interval);
		}

		judge(s);
	}

	void limit_text(const perf_summary& s, string64& text)
	{
		switch (s.limit)
		{
		case limit_cap: xr_sprintf(text, "cap, %s", s.target_source); break;
		case limit_vsync: xr_sprintf(text, "vsync %.0f Hz", display_hz); break;
		case limit_gpu: xr_strcpy(text, "GPU"); break;
		case limit_cpu: xr_strcpy(text, "CPU"); break;
		default: xr_strcpy(text, "not enough frames");
		}
	}

	u32 window_start();

	void write_log()
	{
		xr_vector<u32> order;
		xr_vector<float> median(names.size(), 0.f), p95(names.size(), 0.f), self(names.size(), 0.f), cpu(names.size(), 0.f);
		float unused;
		for (u32 id = 0; id < names.size(); ++id)
		{
			log_stat& l = log_acc[id];
			if (l.gpu.empty())
				continue;
			spread(l.gpu, median[id], p95[id]);
			spread(l.self, self[id], unused);
			spread(l.cpu, cpu[id], unused);
			order.push_back(id);
		}
		std::sort(order.begin(), order.end(), [&](u32 a, u32 b) { return median[a] > median[b]; });

		Msg("* [gpu_timeline] %u %s read since the last log, %u dropped unready, %u disjoint, %u %s dropped, "
			"%u %s off the render thread, peak %u %s per frame, %u timestamp queries in the largest slot, %ux%u",
			log_frames, noun(log_frames, "frame", "frames"), log_dropped, frames_disjoint, scopes_dropped, noun(scopes_dropped, "scope", "scopes"),
			scopes_foreign, noun(scopes_foreign, "scope", "scopes"), peak_scopes, noun(peak_scopes, "scope", "scopes"), 2 * peak_scopes + 2,
			Device.dwWidth, Device.dwHeight);
		for (u32 id : order)
		{
			const log_stat& l = log_acc[id];
			const u32 n = l.gpu.size();
			// Per frame averages, the pipeline ones over the frames that read them
			string256 counts = "";
			if (id >= bracket_rows)
				xr_sprintf(counts, ", per frame %.1f draws %.1f own draws %.0f polys %.0f verts", l.counts[count_draws] / n, l.counts[count_draws_self] / n,
					l.counts[count_polys] / n, l.counts[count_verts] / n);
			if (l.piped)
			{
				string128 pipeline;
				xr_sprintf(pipeline, ", vs %.0f clip in %.0f clip out %.0f ps %.0f cs %.0f invocations", l.counts[count_vs] / l.piped,
					l.counts[count_clip_in] / l.piped, l.counts[count_clip_out] / l.piped, l.counts[count_ps] / l.piped, l.counts[count_cs] / l.piped);
				xr_strcat(counts, pipeline);
			}
			Msg("* [gpu_timeline] %s median %.3f p95 %.3f max %.3f self %.3f cpu %.3f ms, %.2f calls per frame, in %u %s%s",
				names[id].c_str(), median[id], p95[id], l.gpu.back(), self[id], cpu[id], float(l.calls) / float(n), n, noun(n, "frame", "frames"), counts);
		}
		vram_read = read_vram();
		if (vram_read)
			Msg("* [gpu_timeline] vram local %.0f of %.0f MB budget, non-local %.0f of %.0f MB", vram[0].CurrentUsage / 1048576.0,
				vram[0].Budget / 1048576.0, vram[1].CurrentUsage / 1048576.0, vram[1].Budget / 1048576.0);

		// The perf hub's frame stats over the current stats window, while the window records them
		perf_summary s;
		summarise(s, window_start());
		if (s.frames >= 8)
		{
			string64 limit;
			limit_text(s, limit);
			static const LPCSTR task_fields[task_count] = {"rain and particles", "parallel render", "bones", "game"};
			string256 tasks = "";
			for (u32 t = 0; t < task_count; ++t)
			{
				if (!(tasks_stamped & (1u << t)))
					continue;
				string64 field;
				xr_sprintf(field, " %s %.3f", task_fields[t], s.task[t].avg);
				xr_strcat(tasks, field);
			}
			Msg("* [gpu_timeline] frames %u over %.3fs, frame avg %.3f median %.3f p99 %.3f max %.3f std dev %.3f ms, "
				"avg cpu work %.3f gpu %.3f cap wait %.3f present %.3f readback wait %.3f join %.3f hub %.3f ms",
				s.frames, s.window_ms / 1000.f, s.frame.avg, s.frame.p50, s.frame.p99, s.frame.max, s.std_dev,
				s.cpu.avg, s.gpu.avg, s.cap.avg, s.present.avg, s.wait.avg, s.join.avg, s.hub.avg);
			Msg("* [gpu_timeline] limited by %s, target %.3f ms from %s, %u %s over %.3f ms worst %.3f, %.1f%% not presented%s%s%s",
				limit, s.target_ms, s.target_source, s.spikes, noun(s.spikes, "hitch", "hitches"), s.hitch_ms, s.worst_spike, s.not_presented * 100.f,
				tasks[0] ? ", avg tasks" : "", tasks, tasks[0] ? " ms" : "");
		}

		for (log_stat& l : log_acc)
			l = log_stat();
		log_frames = 0;
		log_dropped = 0;
		log_armed = true;
	}

	bool frame_begin()
	{
		if (cur)
		{
			// The presented frame's last stamp marks where this frame's GPU work begins
			if (cur->presented)
			{
				HW.pContext->End(cur->bracket[3]);
				cur->followed = true;
			}
			HW.pContext->End(cur->disjoint);
			// Frames left open while recording was off are stale, their slots are reused unread
			cur->pending = cur->frame + 1 == Device.dwFrame;
		}
#if XRCORE_PROFILER == PROFILER_OPTICK
		// The start burst runs between frames, with no disjoint query open
		if (prof_now == prof_starting)
		{
			prof_burst();
			prof_first = Device.dwFrame;
			prof_now = prof_on;
		}
#endif
		poll();
		if (log_requested.exchange(false))
			write_log();

		cur_index = (cur_index + 1) % ring;
		frame_slot& s = slots[cur_index];
		if (s.pending)
		{
			++frames_dropped;
			++log_dropped;
			s.pending = false;
		}
		D3D11_QUERY_DESC stamp = {D3D11_QUERY_TIMESTAMP, 0};
		for (ID3D11Query*& q : s.bracket)
		{
			if (!q && FAILED(HW.pDevice->CreateQuery(&stamp, &q)))
			{
				cur = nullptr;
				return false;
			}
		}
		if (!s.disjoint)
		{
			D3D11_QUERY_DESC desc = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
			if (FAILED(HW.pDevice->CreateQuery(&desc, &s.disjoint)))
			{
				cur = nullptr;
				return false;
			}
		}
		s.frame = Device.dwFrame;
		s.scopes.clear();
		s.pipes_used = 0;
		s.started = false;
		s.ended = false;
		s.presented = false;
		s.followed = false;
		HW.pContext->Begin(s.disjoint);
		cur = &s;
		return true;
	}

	struct timeline_bar
	{
		u32 event;
		u32 row;
		ImU32 color;
	};

	struct table_row
	{
		u32 name;
		float median;
		float p95;
		float max;
		float self;
		float cpu;
		u32 calls;
		float cpu_self;
		float counts[count_fields];
		bool piped;
	};

	struct cost
	{
		u32 name;
		float ms;
	};

	resolved_frame shown;
	xr_vector<timeline_bar> bars;
	xr_vector<table_row> table;
	xr_vector<float> win_gpu, win_self, win_cpu, win_cpu_self, win_counts[count_fields];
	xr_vector<cost> top_cpu, top_gpu;
	perf_summary hub;
	u64 record_start = 0;
	u32 record_read = 0;
	bool record_valid = false;
	bool gpu_tab = false;
	int select_tab = -1;
	ImGuiTextFilter filter;
	u32 rows_used = 0;
	u32 hidden_bars = 0;
	u32 window_n = 0;
	double window_s = 0.0;
	double draw_ms = 0.0;
	double span = 0.0;
	double next_update = 0.0;
	ImVec2 chart_min, chart_max;
	int drawn_frame = -1;
	bool window_open = false;
	bool editor_wanted = false;
	bool recording_by_window = false;
	bool editor_by_window = false;
	bool input_by_window = false;

	ImU32 bar_color(const xr_string& name)
	{
		static const ImU32 palette[] = {IM_COL32(78, 121, 167, 255), IM_COL32(242, 142, 43, 255), IM_COL32(225, 87, 89, 255),
			IM_COL32(118, 183, 178, 255), IM_COL32(89, 161, 79, 255), IM_COL32(237, 201, 72, 255), IM_COL32(176, 122, 161, 255),
			IM_COL32(255, 157, 167, 255), IM_COL32(156, 117, 95, 255), IM_COL32(186, 176, 172, 255)};
		u32 hash = 2166136261u;
		for (char c : name)
			hash = (hash ^ u8(c)) * 16777619u;
		return palette[hash % std::size(palette)];
	}

	// Oldest frame of the stats window, at least window_frames frames and window_sec seconds back
	u32 window_start()
	{
		const u64 now = CPU::QPC();
		u32 frame = 0;
		window_n = 0;
		window_s = 0.0;
		for (u32 k = 1; k <= marks_count; ++k)
		{
			const frame_mark& m = marks[(marks_next + hist_cap - k) % hist_cap];
			frame = m.frame;
			window_n = k;
			window_s = double(now - m.time) / double(CPU::qpc_freq);
			if (k >= window_frames && window_s >= window_sec)
				break;
		}
		return frame;
	}

	// Takes the newest frame and the window stats at 4 Hz unless frozen or hovered
	void update(bool hovering)
	{
		const double now = ImGui::GetTime();
		if (now < next_update)
			return;
		next_update = now + update_interval;
		if ((frozen || hovering) && !shown.events.empty())
			return;

		vram_read = read_vram();
		table.clear();
		top_cpu.clear();
		top_gpu.clear();
		const u32 from = window_start();
		summarise(hub, from);
		for (u32 id = 0; id < names.size(); ++id)
		{
			const history& h = hist[id];
			win_gpu.clear();
			win_self.clear();
			win_cpu.clear();
			win_cpu_self.clear();
			for (xr_vector<float>& w : win_counts)
				w.clear();
			for (const sample& v : h.samples)
			{
				if (v.frame < from)
					continue;
				win_gpu.push_back(v.gpu);
				win_self.push_back(v.self);
				win_cpu.push_back(v.cpu);
				win_cpu_self.push_back(v.cpu_self);
				for (u32 k = 0; k < (v.piped ? count_fields : pipe_first); ++k)
					win_counts[k].push_back(v.counts[k]);
			}
			if (win_gpu.empty())
				continue;
			table_row r = {id};
			float unused;
			spread(win_gpu, r.median, r.p95);
			spread(win_self, r.self, unused);
			spread(win_cpu, r.cpu, unused);
			spread(win_cpu_self, r.cpu_self, unused);
			for (u32 k = 0; k < count_fields; ++k)
				if (!win_counts[k].empty())
					spread(win_counts[k], r.counts[k], unused);
			r.piped = !win_counts[pipe_first].empty();
			r.max = win_gpu.back();
			r.calls = h.samples[(h.next + h.samples.size() - 1) % h.samples.size()].calls;
			table.push_back(r);
			if (id >= bracket_rows)
			{
				top_cpu.push_back({id, r.cpu_self});
				top_gpu.push_back({id, r.self});
			}
		}
		// Own time keeps nested scopes from counting twice, so the top lists do not overlap
		for (xr_vector<cost>* list : {&top_cpu, &top_gpu})
		{
			std::sort(list->begin(), list->end(), [](const cost& a, const cost& b) { return a.ms > b.ms; });
			if (list->size() > top_rows)
				list->resize(top_rows);
		}
		if (latest.frame == shown.frame && !shown.events.empty())
			return;
		shown = latest;

		bars.clear();
		rows_used = 0;
		hidden_bars = 0;
		span = _max(shown.gpu, 0.0);
		const xr_vector<event>& ev = shown.events;
		if (ev.empty())
			return;

		xr_vector<u32> order(ev.size());
		for (u32 i = 0; i < order.size(); ++i)
			order[i] = i;
		// Parents start no later and end no earlier than their children, so they sort first
		std::stable_sort(order.begin(), order.end(), [&](u32 a, u32 b) {
			return ev[a].start != ev[b].start ? ev[a].start < ev[b].start : ev[a].end > ev[b].end;
		});

		for (const event& e : ev)
			span = _max(span, e.end);
		span = _max(span, 0.001);

		xr_vector<u32> rows(ev.size(), u32(-1));
		double row_ends[max_rows] = {};
		for (u32 i : order)
		{
			const event& e = ev[i];
			if (e.parent != no_scope && rows[e.parent] == u32(-1))
			{
				++hidden_bars;
				continue;
			}
			u32 row = e.parent == no_scope ? 0 : rows[e.parent] + 1;
			while (row < max_rows && row_ends[row] > e.start)
				++row;
			if (row == max_rows)
			{
				++hidden_bars;
				continue;
			}
			rows[i] = row;
			row_ends[row] = e.end;
			rows_used = _max(rows_used, row + 1);
			bars.push_back({i, row, bar_color(names[e.name])});
		}
	}

	void draw_tooltip(const timeline_bar& bar)
	{
		const event& e = shown.events[bar.event];
		xr_string path = names[e.name];
		for (u32 p = e.parent; p != no_scope; p = shown.events[p].parent)
			path = names[shown.events[p].name] + " > " + path;

		ImGui::BeginTooltip();
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(bar.color), "%s", names[e.name].c_str());
		ImGui::Text("Duration %.4fms  |  Self %.4fms  |  CPU %.4fms", e.end - e.start, e.self, e.cpu);
		ImGui::Text("Start %.4fms  |  End %.4fms (from frame start)", e.start, e.end);
		const double* c = e.counts;
		ImGui::Text("Draws %.0f  |  Own draws %.0f  |  Polys %.0f  |  Verts %.0f", c[count_draws], c[count_draws_self], c[count_polys], c[count_verts]);
		if (e.piped)
			ImGui::Text("Invocations VS %.0f  |  Clip in %.0f  |  Clip out %.0f  |  PS %.0f  |  CS %.0f", c[count_vs], c[count_clip_in],
				c[count_clip_out], c[count_ps], c[count_cs]);
		ImGui::TextDisabled("Frame #%u", shown.frame);
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.f);
		ImGui::TextUnformatted(path.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	void draw_timeline(bool hovering)
	{
		// The filter and Freeze keep a fixed place at the right, the header text clips before them
		const float filter_w = 140.f;
		const float button_w = 72.f;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const ImVec2 line = ImGui::GetCursorScreenPos();
		const float controls_x = ImGui::GetCursorPosX() + _max(0.f, ImGui::GetContentRegionAvail().x - filter_w - spacing - button_w);
		ImGui::PushClipRect(line, ImVec2(line.x + controls_x - ImGui::GetCursorPosX() - spacing, line.y + ImGui::GetFrameHeight()), true);
		string64 bracket;
		if (shown.gpu >= 0.0)
			xr_sprintf(bracket, "frame %.3fms  |  no scope %.3fms", shown.gpu, shown.gap);
		else
			xr_strcpy(bracket, "frame unbracketed");
		ImGui::Text("GPU frame #%u  |  %s  |  %s", shown.frame, bracket,
			!ps_r__gpu_timeline ? "RECORDING OFF" : frozen ? "FROZEN" : hovering ? "HELD FOR INSPECTION" : "LIVE (4 Hz)");
		const bool filtering = filter.IsActive();
		if (filtering)
		{
			double self = 0.0;
			for (const event& e : shown.events)
				if (filter.PassFilter(names[e.name].c_str()))
					self += e.self;
			ImGui::SameLine();
			ImGui::Text(" |  matches %.3fms self", self);
		}
		ImGui::PopClipRect();
		ImGui::SameLine(controls_x);
		ImGui::SetNextItemWidth(filter_w);
		if (ImGui::InputTextWithHint("##filter", "filter, -exclude", filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf)))
			filter.Build();
		ImGui::SameLine();
		if (ImGui::Button(frozen ? "Resume" : "Freeze", ImVec2(button_w, 0.f)))
		{
			frozen = !frozen;
			next_update = 0.0;
		}
		// Pause and the main menu cap frames in the same loop as the frame cap
		const bool menu_cap = Device.Paused() || (g_pGamePersistent && g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->IsActive());
		if (psDeviceFlags.test(rsVSync) || ps_framelimiter || menu_cap)
			ImGui::TextColored(ImVec4(1.f, 0.67f, 0.24f, 1.f), "Vsync or a frame cap is on, waits can land inside scopes");
		if (psDeviceFlags.test(rsVSync))
			ImGui::TextDisabled("Under vsync the GPU times include the display hold");

		const float label_w = 96.f;
		const float row_h = ImGui::GetFrameHeight() + 6.f;
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		const float chart_x = origin.x + label_w;
		const float chart_w = _max(width - label_w, 1.f);
		const float chart_y = origin.y + ImGui::GetTextLineHeightWithSpacing();
		chart_min = ImVec2(chart_x, chart_y);
		chart_max = ImVec2(chart_x + chart_w, chart_y + _max(rows_used, 2u) * row_h);

		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(ImVec2(origin.x, chart_y), chart_max, IM_COL32(24, 26, 30, 255));

		const int ticks = clampr(int(chart_w / 140.f), 2, 10);
		for (int i = 0; i <= ticks; ++i)
		{
			const float x = chart_x + chart_w * i / ticks;
			dl->AddLine(ImVec2(x, chart_y), ImVec2(x, chart_max.y), IM_COL32(255, 255, 255, 26));
			string32 tick;
			xr_sprintf(tick, "%.3fms", span * i / ticks);
			const float tw = ImGui::CalcTextSize(tick).x;
			dl->AddText(ImVec2(i == ticks ? x - tw : x + 2.f, origin.y), IM_COL32(255, 255, 255, 166), tick);
		}
		dl->AddText(ImVec2(origin.x + ImGui::GetStyle().FramePadding.x, chart_y + (row_h - ImGui::GetFontSize()) * 0.5f),
			IM_COL32(255, 255, 255, 255), "Graphics");

		auto bar_rect = [&](const timeline_bar& bar, ImVec2& a, ImVec2& b) {
			const event& e = shown.events[bar.event];
			const float left = chart_x + float(e.start / span) * chart_w;
			const float right = chart_x + float(e.end / span) * chart_w;
			a = ImVec2(left, chart_y + bar.row * row_h);
			b = ImVec2(_max(right, left + 1.f), a.y + row_h - 3.f);
		};

		// Subpixel scopes get a small hit target without changing their drawn duration
		int hovered = -1;
		if (ImGui::IsWindowHovered())
		{
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			float nearest = 3.f;
			for (u32 i = 0; i < bars.size(); ++i)
			{
				ImVec2 a, b;
				bar_rect(bars[i], a, b);
				if (mouse.y < a.y || mouse.y >= b.y)
					continue;
				const float d = _max(0.f, _max(a.x - mouse.x, mouse.x - b.x));
				if (d < nearest)
				{
					nearest = d;
					hovered = int(i);
				}
			}
		}

		dl->PushClipRect(chart_min, chart_max, true);
		for (u32 i = 0; i < bars.size(); ++i)
		{
			ImVec2 a, b;
			bar_rect(bars[i], a, b);
			const event& e = shown.events[bars[i].event];
			const bool selected = int(i) == hovered;
			const bool match = !filtering || filter.PassFilter(names[e.name].c_str());
			ImVec4 c = ImGui::ColorConvertU32ToFloat4(bars[i].color);
			if (selected)
				c = ImVec4(c.x + (1.f - c.x) * 0.25f, c.y + (1.f - c.y) * 0.25f, c.z + (1.f - c.z) * 0.25f, 1.f);
			else
				c.w = match ? 0.85f : 0.2f;
			dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(c));
			if (selected)
				dl->AddRect(a, b, IM_COL32(255, 255, 255, 255));
			else if (filtering && match && b.x - a.x < 4.f)
				dl->AddRect(ImVec2(a.x - 2.f, a.y), ImVec2(b.x + 2.f, b.y), IM_COL32(255, 255, 255, 230));
			if (b.x - a.x >= 48.f)
			{
				string128 label;
				xr_sprintf(label, "%s  %.3fms", names[e.name].c_str(), e.end - e.start);
				dl->PushClipRect(ImVec2(a.x + 5.f, a.y), ImVec2(b.x - 5.f, b.y), true);
				dl->AddText(ImVec2(a.x + 5.f, a.y + (b.y - a.y - ImGui::GetFontSize()) * 0.5f),
					match ? IM_COL32(0, 0, 0, 255) : IM_COL32(0, 0, 0, 90), label);
				dl->PopClipRect();
			}
		}
		dl->PopClipRect();

		ImGui::Dummy(ImVec2(width, chart_max.y - origin.y));
		if (hovered >= 0)
			draw_tooltip(bars[hovered]);

		ImGui::TextDisabled("Elapsed GPU scopes, not additive  |  Offsets from frame start  |  Freeze to hold a frame");
		if (const u32 omitted = hidden_bars + scopes_dropped)
		{
			ImGui::SameLine();
			ImGui::TextDisabled(" |  %u %s omitted", omitted, noun(omitted, "event", "events"));
		}
		ImGui::TextDisabled("Stats over %u %s, %.2fs  |  Peak %u %s per frame  |  %u %s dropped unready  |  Window CPU %.3fms",
			window_n, noun(window_n, "frame", "frames"), window_s, peak_scopes, noun(peak_scopes, "scope", "scopes"), frames_dropped,
			noun(frames_dropped, "frame", "frames"), draw_ms);
	}

	void draw_table()
	{
		const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
			ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Hideable;
		static const LPCSTR count_columns[count_fields] = {"Draws", "Own draws", "Polys", "Verts", "VS inv", "Clip in", "Clip out", "PS inv", "CS inv"};
		if (!ImGui::BeginTable("scopes", 7 + count_fields, flags, ImVec2(0.f, _max(ImGui::GetContentRegionAvail().y, 120.f))))
			return;
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 3.f, 0);
		ImGui::TableSetupColumn("Median ms", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 1.f, 1);
		ImGui::TableSetupColumn("P95 ms", ImGuiTableColumnFlags_PreferSortDescending, 1.f, 2);
		ImGui::TableSetupColumn("Max ms", ImGuiTableColumnFlags_PreferSortDescending, 1.f, 3);
		ImGui::TableSetupColumn("Self ms", ImGuiTableColumnFlags_PreferSortDescending, 1.f, 4);
		ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_PreferSortDescending, 1.f, 5);
		ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_PreferSortDescending, 1.f, 6);
		for (u32 k = 0; k < count_fields; ++k)
			ImGui::TableSetupColumn(count_columns[k], ImGuiTableColumnFlags_PreferSortDescending, 1.f, 7 + k);
		ImGui::TableHeadersRow();

		const ImGuiTableSortSpecs* sort = ImGui::TableGetSortSpecs();
		if (sort && sort->SpecsCount)
		{
			const ImGuiTableColumnSortSpecs& s = sort->Specs[0];
			const bool up = s.SortDirection == ImGuiSortDirection_Ascending;
			auto key = [&](const table_row& r) -> double {
				switch (s.ColumnUserID)
				{
				case 1: return r.median;
				case 2: return r.p95;
				case 3: return r.max;
				case 4: return r.self;
				case 5: return r.cpu;
				case 6: return r.calls;
				}
				return s.ColumnUserID >= 7 ? r.counts[s.ColumnUserID - 7] : 0.0;
			};
			std::stable_sort(table.begin(), table.end(), [&](const table_row& a, const table_row& b) {
				if (s.ColumnUserID == 0)
					return up ? names[a.name] < names[b.name] : names[b.name] < names[a.name];
				return up ? key(a) < key(b) : key(b) < key(a);
			});
		}

		const bool filtering = filter.IsActive();
		for (const table_row& r : table)
		{
			if (filtering && !filter.PassFilter(names[r.name].c_str()))
				continue;
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(bar_color(names[r.name])), "%s", names[r.name].c_str());
			ImGui::TableNextColumn();
			ImGui::Text("%.3f", r.median);
			ImGui::TableNextColumn();
			ImGui::Text("%.3f", r.p95);
			ImGui::TableNextColumn();
			ImGui::Text("%.3f", r.max);
			ImGui::TableNextColumn();
			ImGui::Text("%.3f", r.self);
			ImGui::TableNextColumn();
			const bool gpu_only = r.name != frame_row && r.name < bracket_rows;
			if (gpu_only)
				ImGui::TextDisabled("-");
			else if (r.cpu > 0.f && r.cpu >= r.median)
			{
				ImGui::TextColored(ImVec4(1.f, 0.67f, 0.24f, 1.f), "%.3f", r.cpu);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("CPU submit time reaches the GPU time\nGPU time may include idle waiting on submission");
			}
			else
				ImGui::Text("%.3f", r.cpu);
			ImGui::TableNextColumn();
			if (gpu_only)
				ImGui::TextDisabled("-");
			else
				ImGui::Text("%u", r.calls);
			for (u32 k = 0; k < count_fields; ++k)
			{
				ImGui::TableNextColumn();
				if (r.name < bracket_rows || (k >= pipe_first && !r.piped))
					ImGui::TextDisabled("-");
				// Thousands and millions keep large counts inside a narrow column
				else if (r.counts[k] >= 1e7f)
					ImGui::Text("%.1fM", r.counts[k] / 1e6f);
				else if (r.counts[k] >= 1e5f)
					ImGui::Text("%.0fk", r.counts[k] / 1e3f);
				else
					ImGui::Text("%.0f", r.counts[k]);
			}
		}
		ImGui::EndTable();
	}

	ImVec4 severity_color(severity s)
	{
		switch (s)
		{
		case sev_good: return ImVec4(0.45f, 0.82f, 0.45f, 1.f);
		case sev_warning: return ImVec4(1.f, 0.67f, 0.24f, 1.f);
		case sev_bad: return ImVec4(0.94f, 0.36f, 0.36f, 1.f);
		default: return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
		}
	}

	// Status and Freeze for the Overview and CPU tabs, the GPU tab keeps its own header
	bool hub_header()
	{
		const float button_w = 72.f;
		const float controls_x = ImGui::GetCursorPosX() + _max(0.f, ImGui::GetContentRegionAvail().x - button_w);
		ImGui::Text("%s  |  Stats over %u %s, %.2fs", !ps_r__gpu_timeline ? "RECORDING OFF" : frozen ? "FROZEN" : "LIVE (4 Hz)",
			hub.frames, noun(hub.frames, "frame", "frames"), hub.window_ms / 1000.f);
		ImGui::SameLine(controls_x);
		if (ImGui::Button(frozen ? "Resume" : "Freeze", ImVec2(button_w, 0.f)))
		{
			frozen = !frozen;
			next_update = 0.0;
		}
		if (hub.frames >= 8)
			return true;
		ImGui::TextUnformatted(ps_r__gpu_timeline ? "Waiting for frames..." : "Recording is off, r__gpu_timeline 1 starts it");
		return false;
	}

	void stat_cells(const perf_stat& v, severity avg, severity p99, severity max)
	{
		const float values[] = {v.avg, v.p50, v.p99, v.max};
		const severity marks[] = {avg, sev_info, p99, max};
		for (u32 i = 0; i < 4; ++i)
		{
			ImGui::TableNextColumn();
			if (marks[i] == sev_info)
				ImGui::Text("%.2f", values[i]);
			else
				ImGui::TextColored(severity_color(marks[i]), "%.2f", values[i]);
		}
	}

	void cost_table(LPCSTR id, LPCSTR title, const xr_vector<cost>& list)
	{
		if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingStretchProp))
			return;
		ImGui::TableSetupColumn(title, ImGuiTableColumnFlags_WidthStretch, 3.f);
		ImGui::TableSetupColumn("Median ms", ImGuiTableColumnFlags_WidthStretch, 1.f);
		ImGui::TableHeadersRow();
		for (const cost& c : list)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(bar_color(names[c.name])), "%s", names[c.name].c_str());
			ImGui::TableNextColumn();
			ImGui::Text("%.3f", c.ms);
		}
		ImGui::EndTable();
	}

	void draw_overview()
	{
		if (!hub_header())
			return;

		string64 limit;
		limit_text(hub, limit);
		ImGui::TextUnformatted("Limited by");
		ImGui::SameLine();
		ImGui::TextColored(severity_color(hub.rating), "%s", limit);
		ImGui::SameLine();
		ImGui::Text(" |  Target %.2fms, %.0f fps from %s  | ", hub.target_ms, 1000.f / hub.target_ms, hub.target_source);
		ImGui::SameLine();
		if (hub.spikes)
			ImGui::TextColored(severity_color(hub.spikes > 4 || hub.worst_spike > 100.f ? sev_bad : sev_warning), "%u %s, worst %.1fms",
				hub.spikes, noun(hub.spikes, "hitch", "hitches"), hub.worst_spike);
		else
			ImGui::TextUnformatted("No hitches");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Frames over %.1fms, twice the median and half again over the target", hub.hitch_ms);

		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_SizingStretchProp;
		if (ImGui::BeginTable("breakdown", 5, flags))
		{
			ImGui::TableSetupColumn("Frame breakdown", ImGuiTableColumnFlags_WidthStretch, 3.f);
			ImGui::TableSetupColumn("Avg ms", ImGuiTableColumnFlags_WidthStretch, 1.f);
			ImGui::TableSetupColumn("P50 ms", ImGuiTableColumnFlags_WidthStretch, 1.f);
			ImGui::TableSetupColumn("P99 ms", ImGuiTableColumnFlags_WidthStretch, 1.f);
			ImGui::TableSetupColumn("Max ms", ImGuiTableColumnFlags_WidthStretch, 1.f);
			ImGui::TableHeadersRow();

			// Waits and the hub are dimmed, the work rows warn past the target and the hitch line
			const float work = _max(0.f, hub.frame.avg - hub.hub.avg);
			const severity missed = hub.meeting_target ? sev_info : hub.rating;
			const struct
			{
				LPCSTR name;
				const perf_stat& v;
				severity avg;
				bool dim;
			} rows[] = {
				{"Frame", hub.frame, work > hub.target_ms * 1.5f ? sev_bad : work > hub.target_ms * 1.05f ? sev_warning : sev_info, false},
				{"CPU work", hub.cpu, hub.limit == limit_cpu ? missed : sev_info, false},
				{"GPU", hub.gpu, hub.limit == limit_gpu ? missed : sev_info, false},
				{"Cap wait", hub.cap, sev_info, true},
				{"Present wait", hub.present, sev_info, true},
				{"Readback wait", hub.wait, sev_info, true},
				{"Vsync wait", hub.vsync_wait, sev_info, true},
				{"Worker join wait", hub.join, sev_info, true},
				{"Hub (this window)", hub.hub, sev_info, true},
			};
			for (const auto& r : rows)
			{
				if (&r.v == &hub.gpu && hub.gpu.avg <= 0.f)
					continue;
				// Under vsync Present and the readbacks both wait for the display
				if (hub.vsync ? &r.v == &hub.present || &r.v == &hub.wait : &r.v == &hub.vsync_wait)
					continue;
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (r.dim)
					ImGui::TextDisabled("%s", r.name);
				else
					ImGui::TextUnformatted(r.name);
				const severity p99 = r.v.p99 > hub.hitch_ms ? sev_bad : r.v.p99 > hub.target_ms * 1.25f && !r.dim ? sev_warning : sev_info;
				const severity max = r.v.max > hub.hitch_ms && !r.dim ? sev_warning : sev_info;
				stat_cells(r.v, r.avg, p99, max);
			}
			ImGui::EndTable();
		}

		if (ImGui::BeginTable("top", 2, ImGuiTableFlags_SizingStretchSame))
		{
			ImGui::TableNextColumn();
			cost_table("top_cpu", "Render thread, own CPU", top_cpu);
			ImGui::TableNextColumn();
			cost_table("top_gpu", "GPU passes, own time", top_gpu);
			ImGui::EndTable();
		}
		ImGui::TextDisabled("Own time leaves out nested scopes, so the rows do not overlap  |  The render thread also runs work outside any scope");
	}

	void draw_histogram()
	{
		const float h = 120.f;
		const ImVec2 o = ImGui::GetCursorScreenPos();
		const float w = _max(ImGui::GetContentRegionAvail().x, 1.f);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(o, ImVec2(o.x + w, o.y + h), IM_COL32(24, 26, 30, 255));

		u32 most = 1;
		for (u32 c : hub.buckets)
			most = _max(most, c);
		const float bar_w = w / hist_buckets;
		for (u32 b = 0; b < hist_buckets; ++b)
		{
			if (!hub.buckets[b])
				continue;
			// Square root, so a few slow frames still show beside hundreds of good ones
			const float bh = _max(1.f, _sqrt(float(hub.buckets[b]) / float(most)) * (h - 4.f));
			const ImU32 c = b >= hub.hitch_bucket ? IM_COL32(240, 92, 92, 255) : IM_COL32(78, 121, 167, 220);
			dl->AddRectFilled(ImVec2(o.x + b * bar_w, o.y + h - bh), ImVec2(o.x + (b + 1) * bar_w - 1.f, o.y + h), c);
		}
		const float tx = o.x + hub.target_ms / hub.range * w;
		for (float y = o.y; y < o.y + h; y += 6.f)
			dl->AddLine(ImVec2(tx, y), ImVec2(tx, y + 3.f), IM_COL32(255, 255, 255, 160));
		ImGui::Dummy(ImVec2(w, h));
		ImGui::TextDisabled("0 to %.1fms  |  Dashed line at the %.1fms target  |  Red past the %.1fms hitch line", hub.range, hub.target_ms,
			hub.hitch_ms);
	}

	void draw_cpu()
	{
		if (!hub_header())
			return;

		ImGui::TextUnformatted("CPU work");
		ImGui::SameLine();
		ImGui::TextColored(severity_color(hub.limit == limit_cpu && !hub.meeting_target ? hub.rating : sev_info), "%.2fms", hub.cpu.avg);
		ImGui::SameLine();
		if (hub.vsync)
			ImGui::Text(" |  Cap wait %.2fms  |  Vsync wait %.2fms  |  Worker join wait %.2fms  |  Hub %.2fms", hub.cap.avg, hub.vsync_wait.avg,
				hub.join.avg, hub.hub.avg);
		else
			ImGui::Text(" |  Cap wait %.2fms  |  Present wait %.2fms  |  Readback wait %.2fms  |  Worker join wait %.2fms  |  Hub %.2fms", hub.cap.avg,
				hub.present.avg, hub.wait.avg, hub.join.avg, hub.hub.avg);
		ImGui::Text("Frame p50 %.2fms  |  Max %.1fms  |  Std dev %.2fms  |  Not presented %.0f%%", hub.frame.p50, hub.frame.max, hub.std_dev,
			hub.not_presented * 100.f);

		// What decides the cap, so a frame rate stuck at a round number can be explained
		string64 capped, limit;
		if (ps_framelimiter > 0)
			xr_sprintf(capped, "r__framelimit %d fps", ps_framelimiter);
		else if (eco_capped())
			xr_sprintf(capped, "pause or menu, %.0f fps", eco_hz);
		else
			xr_strcpy(capped, "nothing");
		limit_text(hub, limit);
		ImGui::Text("Capped by %s  |  Vsync %s  |  %s  |  Limited by", capped, psDeviceFlags.test(rsVSync) ? "on" : "off",
			HW.m_ChainDescFullscreen.Windowed ? "Windowed" : "Fullscreen");
		ImGui::SameLine();
		ImGui::TextColored(severity_color(hub.rating), "%s", limit);

		ImGui::SeparatorText("Frame time distribution");
		draw_histogram();

		if (!tasks_stamped)
			return;
		ImGui::SeparatorText("Worker tasks");
		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_SizingStretchProp;
		if (ImGui::BeginTable("tasks", 5, flags))
		{
			static const LPCSTR task_names[task_count] = {"Rain and particles", "Parallel render", "Bones", "Game thread"};
			ImGui::TableSetupColumn("Task body", ImGuiTableColumnFlags_WidthStretch, 3.f);
			ImGui::TableSetupColumn("Avg ms", ImGuiTableColumnFlags_WidthStretch, 1.f);
			ImGui::TableSetupColumn("P50 ms", ImGuiTableColumnFlags_WidthStretch, 1.f);
			ImGui::TableSetupColumn("P99 ms", ImGuiTableColumnFlags_WidthStretch, 1.f);
			ImGui::TableSetupColumn("Max ms", ImGuiTableColumnFlags_WidthStretch, 1.f);
			ImGui::TableHeadersRow();
			for (u32 t = 0; t < task_count; ++t)
			{
				if (!(tasks_stamped & (1u << t)))
					continue;
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(task_names[t]);
				stat_cells(hub.task[t], sev_info, sev_info, sev_info);
			}
			ImGui::EndTable();
		}
		ImGui::TextDisabled("Tasks run beside the render thread, a long one shows as worker join wait");
	}

	void draw();

	struct window_frame : public pureFrame
	{
		void _BCL OnFrame() override { draw(); }
	} drawer;

	// Takes the frame the device finished last loop, the start stamp of this loop closes it
	// Runs in seqFrame, after the join and before the tasks start, so no writer is running
	void read_record()
	{
		const bool on = Device.frame_record_on;
		Device.frame_record_on = ps_r__gpu_timeline != 0;
		const CRenderDevice::FrameRecord& r = Device.frame_record;
		const u32 frame = Device.dwFrame - 1;
		if (on && record_valid && record_read == frame && r.frame == frame)
		{
			const double ms = 1000.0 / double(CPU::qpc_freq);
			auto span = [ms](const u64* s) { return s[1] > s[0] ? float(double(s[1] - s[0]) * ms) : 0.f; };
			perf_frame& p = perf[frame % perf_cap];
			p.frame = frame;
			p.interval = float(double(r.start - record_start) * ms);
			// Frames the device did not render leave the last Present stamps in place
			const bool rendered = r.present[0] > record_start;
			p.cap = span(r.cap);
			p.present = rendered ? span(r.present) : 0.f;
			p.join = span(r.join);
			p.hub = float(draw_ms);
			p.gpu = 0.f;
			p.wait = float(double(r.wait) * ms);
			for (u32 t = 0; t < task_count; ++t)
			{
				p.task[t] = span(r.task[t]);
				if (r.task[t][1] > r.task[t][0])
					tasks_stamped |= 1u << t;
			}
			p.presented = rendered && r.presented;
			perf_newest = frame;
		}
		record_valid = on && Device.frame_record_on;
		record_start = r.start;
		record_read = Device.dwFrame;
	}

	// Undoes what open did, on the X and whenever the editor hides
	void close_window()
	{
		window_open = false;
		editor_wanted = false;
		Device.frame_record_on = false;
		record_valid = false;
		Device.seqFrame.Remove(&drawer);
#if XRCORE_PROFILER == PROFILER_OPTICK
		// Running captures keep recording and turn it off when they end
		if (recording_by_window && prof_now != prof_off)
		{
			xrCriticalSectionGuard guard(prof_lock);
			prof_forced = true;
			recording_by_window = false;
		}
#endif
		if (recording_by_window)
			ps_r__gpu_timeline = 0;
		if (Device.imgui_shown())
		{
			if (editor_by_window)
				Device.imgui().Show(false);
			else if (input_by_window)
				Device.imgui().EnableInput(false);
		}
		editor_by_window = false;
		input_by_window = false;
		frozen = false;
	}

	// Showing the editor takes input the way its own key does, so Esc hands it back
	void take_editor()
	{
		editor_wanted = false;
		if (!Device.imgui_shown())
		{
			editor_by_window = true;
			Device.imgui().Show(true);
		}
		else if (!Device.imgui().is_input())
		{
			input_by_window = true;
			Device.imgui().EnableInput(true);
		}
	}

	void draw()
	{
		if (editor_wanted)
		{
			read_refresh();
			take_editor();
		}
		// The window lives only while the editor is shown, like every other editor window
		if (!Device.imgui_shown())
		{
			close_window();
			return;
		}
		// Once the user releases input, taking it back is theirs, so close leaves it
		if (input_by_window && !Device.imgui().is_input())
			input_by_window = false;
		if (record_read != Device.dwFrame)
			read_record();

		// Draws once per ImGui frame and only inside one
		const ImGuiContext* g = ImGui::GetCurrentContext();
		if (!g || !g->WithinFrameScope || ImGui::GetFrameCount() == drawn_frame)
			return;
		drawn_frame = ImGui::GetFrameCount();
		const u64 begin = CPU::QPC();

		ImGui::SetNextWindowPos(ImVec2(40.f, 60.f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(1240.f, 620.f), ImGuiCond_FirstUseEver);
		bool open = true;
		if (ImGui::Begin("Performance", &open))
		{
			// The chart rect is from the last frame the GPU tab drew
			const bool hovering = gpu_tab && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(chart_min, chart_max);
			update(hovering);
			gpu_tab = false;
			if (ImGui::BeginTabBar("tabs"))
			{
				auto pick = [](int t) { return select_tab == t ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
				if (ImGui::BeginTabItem("Overview", nullptr, pick(0)))
				{
					draw_overview();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("CPU", nullptr, pick(1)))
				{
					draw_cpu();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("GPU", nullptr, pick(2)))
				{
					gpu_tab = true;
					if (shown.events.empty())
						ImGui::TextUnformatted(ps_r__gpu_timeline ? "GPU timeline: waiting for timestamp data..." :
							"GPU timeline: recording is off, r__gpu_timeline 1 starts it");
					else
						draw_timeline(hovering);
					// Past the local budget the OS starts moving resources out of video memory
					if (vram_read)
					{
						const float mb = 1.f / 1048576.f;
						ImGui::TextColored(severity_color(vram[0].CurrentUsage > vram[0].Budget ? sev_warning : sev_info),
							"VRAM %.0f of %.0f MB budget  |  Shared %.0f of %.0f MB", vram[0].CurrentUsage * mb, vram[0].Budget * mb,
							vram[1].CurrentUsage * mb, vram[1].Budget * mb);
					}
					draw_table();
					ImGui::EndTabItem();
				}
				select_tab = -1;
				ImGui::EndTabBar();
			}
		}
		ImGui::End();
		draw_ms = double(CPU::QPC() - begin) * 1000.0 / double(CPU::qpc_freq);

		if (!open)
			close_window();
	}
}

void frame_start()
{
	if (!render_thread() || !stack.empty())
		return;
	if ((!cur || cur->frame != Device.dwFrame) && !frame_begin())
		return;
	if (!cur->started)
	{
		HW.pContext->End(cur->bracket[0]);
		cur->cpu_begin = CPU::QPC();
		cur->started = true;
	}
}

void frame_end()
{
	if (!cur || !cur->started || cur->ended || cur->frame != Device.dwFrame || !stack.empty() || GetCurrentThreadId() != thread)
		return;
	cur->cpu_end = CPU::QPC();
	HW.pContext->End(cur->bracket[1]);
	cur->ended = true;
}

void frame_presented()
{
	if (!cur || !cur->ended || cur->presented || cur->frame != Device.dwFrame || !stack.empty() || GetCurrentThreadId() != thread)
		return;
	HW.pContext->End(cur->bracket[2]);
	cur->presented = true;
	// Submit the stamp after Present now so the wait for the next frame follows it
	PROF_EVENT("gpu_timeline present flush", PROF_CAT(Debug));
	HW.pContext->Flush();
}

u32 scope_begin(LPCWSTR name)
{
	if (!render_thread())
	{
		++scopes_foreign;
		return no_scope;
	}

	if ((!cur || cur->frame != Device.dwFrame) && stack.empty() && !frame_begin())
		return no_scope;
	if (!cur)
		return no_scope;

	frame_slot& s = *cur;
	const u32 index = s.scopes.size();
	while (s.stamps.size() < 2 * index + 2)
	{
		ID3D11Query* q = nullptr;
		D3D11_QUERY_DESC desc = {D3D11_QUERY_TIMESTAMP, 0};
		if (FAILED(HW.pDevice->CreateQuery(&desc, &q)))
		{
			++scopes_dropped;
			return no_scope;
		}
		s.stamps.push_back(q);
	}

	s.scopes.push_back({name, stack.empty() ? no_scope : stack.back(), u32(stack.size()), 0, 0, RCache.stat.calls, RCache.stat.polys,
		RCache.stat.verts, no_scope});
	HW.pContext->End(s.stamps[2 * index]);
	// Scopes one level below the top never overlap, so one statistics query runs at a time
	if (stack.size() == 1)
	{
		D3D11_QUERY_DESC desc = {D3D11_QUERY_PIPELINE_STATISTICS, 0};
		ID3D11Query* q = nullptr;
		if (s.pipes_used < s.pipes.size() || SUCCEEDED(HW.pDevice->CreateQuery(&desc, &q)))
		{
			if (q)
				s.pipes.push_back(q);
			HW.pContext->Begin(s.pipes[s.pipes_used]);
			s.scopes.back().pipe = s.pipes_used++;
		}
	}
	s.scopes.back().cpu_begin = CPU::QPC();
	stack.push_back(index);
	peak_scopes = _max(peak_scopes, index + 1);
	return index;
}

void scope_end(u32 scope)
{
	if (!cur || scope >= cur->scopes.size())
		return;
	scope_rec& r = cur->scopes[scope];
	r.cpu_end = CPU::QPC();
	if (r.pipe != no_scope)
		HW.pContext->End(cur->pipes[r.pipe]);
	HW.pContext->End(cur->stamps[2 * scope + 1]);
	r.draws = RCache.stat.calls - r.draws;
	r.polys = RCache.stat.polys - r.polys;
	r.verts = RCache.stat.verts - r.verts;
	while (!stack.empty())
	{
		const u32 top = stack.back();
		stack.pop_back();
		if (top == scope)
			break;
	}
}

void open(LPCSTR tab)
{
	static const LPCSTR tabs[] = {"overview", "cpu", "gpu"};
	for (int t = 0; t < int(std::size(tabs)); ++t)
		if (!xr_strcmp(tab, tabs[t]))
			select_tab = t;
	if (!window_open)
	{
		// Closing the window turns recording off again when opening it turned it on
		recording_by_window = !ps_r__gpu_timeline;
#if XRCORE_PROFILER == PROFILER_OPTICK
		// The window takes over recording a running capture turned on
		{
			xrCriticalSectionGuard guard(prof_lock);
			recording_by_window = recording_by_window || prof_forced;
			prof_forced = false;
		}
#endif
		ps_r__gpu_timeline = 1;
		window_open = true;
		tasks_stamped = 0;
		Device.seqFrame.Add(&drawer, REG_PRIORITY_LOW);
	}
	// Config lines run before the device and the UI exist, so the first frame takes the editor
	if (Device.b_is_Ready)
	{
		read_refresh();
		take_editor();
	}
	else
		editor_wanted = true;
}

void toggle_freeze()
{
	frozen = !frozen;
}

void request_log()
{
	if (ps_r__gpu_timeline)
		log_requested = true;
	else
		Msg("* [gpu_timeline] recording is off, r__gpu_timeline 1 or gpu_timeline starts it");
}

void destroy()
{
	for (frame_slot& s : slots)
	{
		_RELEASE(s.disjoint);
		for (ID3D11Query*& q : s.bracket)
			_RELEASE(q);
		for (ID3D11Query*& q : s.stamps)
			_RELEASE(q);
		for (ID3D11Query*& q : s.pipes)
			_RELEASE(q);
		s.stamps.clear();
		s.pipes.clear();
		s.pipes_used = 0;
		s.scopes.clear();
		s.started = false;
		s.ended = false;
		s.presented = false;
		s.followed = false;
		s.pending = false;
	}
#if XRCORE_PROFILER == PROFILER_OPTICK
	_RELEASE(prof_idle);
	_RELEASE(prof_stamp);
	_RELEASE(prof_disjoint);
#endif
	_RELEASE(vram_adapter);
	vram_read = false;
	cur = nullptr;
	stack.clear();
	thread = 0;
	latest = resolved_frame();
	shown = resolved_frame();
	bars.clear();
}
}
