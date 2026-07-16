#pragma once

#include <ppl.h>
#include <concurrent_unordered_map.h>
#include <concurrent_vector.h>
#include <atomic>
#include <array>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <algorithm>
#include <iterator>
#include <type_traits>
#include <utility>

// Atomic types
using xr_atomic_u32 = std::atomic_uint32_t;
using xr_atomic_s32 = std::atomic_int;
using xr_atomic_bool = std::atomic_bool;

// Tasks Redefinition
using xr_task_group = concurrency::task_group;

template <typename T, typename U>
using xr_concurrent_unordered_map = concurrency::concurrent_unordered_map<T, U>;

template <typename T, typename allocator = xalloc<T>>
using xr_concurrent_vector = concurrency::concurrent_vector<T, allocator>;

template<typename BlockRangeType, typename Body>
inline void xr_parallel_for(BlockRangeType Begin, BlockRangeType End, Body Functor)
{
	concurrency::parallel_for(Begin, End, Functor);
}

template<typename Index, typename Body>
inline void xr_parallel_foreach(Index Begin, Index End, Body Functor)
{
	concurrency::parallel_for_each(Begin, End, Functor);
}
// Helper to deduce the value type for the default predicate
template <typename RandomIt>
using IterValueT = typename std::iterator_traits<RandomIt>::value_type;

// Helper to check if an iterator is Random Access
template <typename It>
using IsRandomAccess = std::is_base_of<
    std::random_access_iterator_tag,
    typename std::iterator_traits<It>::iterator_category
>;

// PPL behaviour - fallback to std::sort if chunk size < 2048 and cores < 2
template<typename RandomIt, typename P = std::less<IterValueT<RandomIt>>>
IC void xr_parallel_sort(RandomIt first, RandomIt last, P pred = {})
{
    concurrency::parallel_sort(first, last, pred);
}

enum class NativeLoadPriority : u8
{
    Spawn,
    ShaderTexture,
    Geometry,
    Environment,
    Speculative,
    Count
};

// One process-wide load queue over the existing PPL scheduler. Pump tasks take
// the highest-priority available work, so idle scheduler workers automatically
// help whichever native subsystem still has work left.
class NativeLoadExecutor : xray::noncopyable
{
    struct GenerationState;
    struct BatchState;

public:
    using GenerationId = u64;

    class Batch
    {
        friend class NativeLoadExecutor;

        std::shared_ptr<BatchState> state;

        explicit Batch(std::shared_ptr<BatchState> value) : state(std::move(value)) {}

    public:
        Batch() = default;
        bool Valid() const { return !!state; }
    };

    static NativeLoadExecutor& Instance()
    {
        static NativeLoadExecutor executor;
        return executor;
    }

    GenerationId BeginGeneration()
    {
        std::lock_guard<std::mutex> lifecycle_guard(lifecycle_mutex);

        std::shared_ptr<GenerationState> previous;
        {
            std::lock_guard<std::mutex> guard(mutex);
            previous = current_generation;
        }
        if (previous)
            FinishGeneration(previous, true);

        const auto state = std::make_shared<GenerationState>();
        {
            std::lock_guard<std::mutex> guard(mutex);
            state->id = ++next_generation;
            current_generation = state;
        }
        return state->id;
    }

    void CancelGeneration(GenerationId id)
    {
        std::lock_guard<std::mutex> lifecycle_guard(lifecycle_mutex);
        const auto state = FindGeneration(id);
        if (state)
            FinishGeneration(state, true);
    }

    void FinalizeGeneration(GenerationId id)
    {
        std::lock_guard<std::mutex> lifecycle_guard(lifecycle_mutex);
        const auto state = FindGeneration(id);
        if (state)
            FinishGeneration(state, false);
    }

    Batch BeginBatch(GenerationId id)
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (!current_generation || current_generation->id != id || !current_generation->accepting ||
            current_generation->cancelled)
            return {};

        const auto batch = std::make_shared<BatchState>();
        batch->generation = current_generation;
        return Batch(batch);
    }

    template <typename Function>
    bool Submit(const Batch& batch, NativeLoadPriority priority, Function&& function)
    {
        return SubmitImpl(batch, priority, std::function<void()>(std::forward<Function>(function)), {});
    }

    template <typename Function, typename CancelFunction>
    bool Submit(const Batch& batch, NativeLoadPriority priority, Function&& function, CancelFunction&& cancel)
    {
        return SubmitImpl(batch, priority, std::function<void()>(std::forward<Function>(function)),
            std::function<void()>(std::forward<CancelFunction>(cancel)));
    }

    void Wait(const Batch& batch, bool help = true)
    {
        const auto batch_state = batch.state;
        if (!batch_state)
            return;

        {
            std::lock_guard<std::mutex> guard(mutex);
            batch_state->closed = true;
            batch_state->completed.notify_all();
        }

        for (;;)
        {
            {
                std::lock_guard<std::mutex> guard(mutex);
                if (!batch_state->pending)
                    break;
            }

            if (help && TryExecuteOne(batch_state))
                continue;

            std::unique_lock<std::mutex> guard(mutex);
            batch_state->completed.wait(guard, [&batch_state] { return !batch_state->pending; });
        }

        std::exception_ptr failure;
        {
            std::lock_guard<std::mutex> guard(mutex);
            // A batch owns the lifetime of the data captured by its tasks. Do
            // not unwind it because an unrelated batch in the same generation
            // failed; cancelled work below inherits the generation failure into
            // its own batch before completion.
            failure = batch_state->failure;
        }
        if (failure)
            std::rethrow_exception(failure);
    }

    bool IsCurrent(GenerationId id) const
    {
        std::lock_guard<std::mutex> guard(mutex);
        return current_generation && current_generation->id == id && !current_generation->cancelled;
    }

    GenerationId CurrentGeneration() const
    {
        std::lock_guard<std::mutex> guard(mutex);
        return current_generation ? current_generation->id : 0;
    }

    // Device reset must not invalidate resources while load jobs still own or
    // create them. Drain the current generation without closing it: later
    // stages of the same load may still enqueue more work after reset.
    void WaitCurrentGenerationIdle()
    {
        std::lock_guard<std::mutex> lifecycle_guard(lifecycle_mutex);

        std::shared_ptr<GenerationState> generation;
        {
            std::lock_guard<std::mutex> guard(mutex);
            generation = current_generation;
        }
        if (!generation)
            return;

        WaitForGeneration(generation);

        std::exception_ptr failure;
        {
            std::lock_guard<std::mutex> guard(mutex);
            failure = generation->failure;
        }
        if (failure)
            std::rethrow_exception(failure);
    }

    bool HelpGeneration(GenerationId id)
    {
        const auto state = FindGeneration(id);
        return state && TryExecuteOne(state);
    }

    u32 WorkerLimit() const { return worker_limit; }

private:
    struct GenerationState
    {
        GenerationId id = 0;
        bool accepting = true;
        bool cancelled = false;
        u32 pending = 0;
        u32 queued = 0;
        u32 active_pumps = 0;
        std::exception_ptr failure;
        std::condition_variable completed;
        std::condition_variable pumps_idle;
        std::shared_ptr<xr_task_group> pumps = std::make_shared<xr_task_group>();
    };

    struct BatchState
    {
        std::shared_ptr<GenerationState> generation;
        u32 pending = 0;
        bool closed = false;
        std::exception_ptr failure;
        std::condition_variable completed;
    };

    struct WorkItem
    {
        std::shared_ptr<GenerationState> generation;
        std::shared_ptr<BatchState> batch;
        std::function<void()> function;
        std::function<void()> cancel;
    };

    static constexpr size_t PriorityCount = static_cast<size_t>(NativeLoadPriority::Count);

    mutable std::mutex mutex;
    std::mutex lifecycle_mutex;
    std::array<std::deque<WorkItem>, PriorityCount> queues;
    std::shared_ptr<GenerationState> current_generation;
    GenerationId next_generation = 0;
    const u32 worker_limit;

    NativeLoadExecutor()
        : worker_limit(std::max(1u, std::thread::hardware_concurrency() > 1 ?
            std::thread::hardware_concurrency() - 1 : 1u))
    {
    }

    ~NativeLoadExecutor()
    {
        try
        {
            const GenerationId id = CurrentGeneration();
            if (id)
                CancelGeneration(id);
        }
        catch (...)
        {
        }
    }

    std::shared_ptr<GenerationState> FindGeneration(GenerationId id) const
    {
        std::lock_guard<std::mutex> guard(mutex);
        return current_generation && current_generation->id == id ? current_generation : nullptr;
    }

    bool PopWorkLocked(const std::shared_ptr<GenerationState>& generation, WorkItem& result)
    {
        for (auto& queue : queues)
        {
            if (queue.empty() || queue.front().generation != generation)
                continue;

            result = std::move(queue.front());
            queue.pop_front();
            --generation->queued;
            return true;
        }
        return false;
    }

    bool PopWorkLocked(const std::shared_ptr<BatchState>& batch, WorkItem& result)
    {
        for (auto& queue : queues)
        {
            // Nested work can sit behind its parent in the same priority lane.
            const auto item = std::find_if(queue.begin(), queue.end(), [&batch](const WorkItem& work)
            {
                return work.batch == batch;
            });
            if (item == queue.end())
                continue;

            result = std::move(*item);
            queue.erase(item);
            --batch->generation->queued;
            return true;
        }
        return false;
    }

    void RecordFailureLocked(const std::shared_ptr<GenerationState>& generation,
        const std::shared_ptr<BatchState>& batch, std::exception_ptr failure)
    {
        if (!failure)
            return;
        if (batch && !batch->failure)
            batch->failure = failure;
        if (!generation->failure)
            generation->failure = failure;
        generation->accepting = false;
        generation->cancelled = true;
    }

    void SchedulePumpsLocked(const std::shared_ptr<GenerationState>& generation)
    {
        u32 pumps_to_start = std::min(worker_limit - generation->active_pumps, generation->queued);
        while (pumps_to_start--)
        {
            ++generation->active_pumps;
            try
            {
                generation->pumps->run([this, generation] { Pump(generation); });
            }
            catch (...)
            {
                --generation->active_pumps;
                RecordFailureLocked(generation, nullptr, std::current_exception());
                generation->completed.notify_all();
                generation->pumps_idle.notify_all();
                break;
            }
        }
    }

    bool SubmitImpl(const Batch& batch, NativeLoadPriority priority, std::function<void()> function,
        std::function<void()> cancel)
    {
        const auto batch_state = batch.state;
        if (!batch_state || !function || priority >= NativeLoadPriority::Count)
            return false;

        std::lock_guard<std::mutex> guard(mutex);
        const auto& generation = batch_state->generation;
        if (current_generation != generation || !generation->accepting || generation->cancelled || batch_state->closed)
            return false;

        WorkItem work{generation, batch_state, std::move(function), std::move(cancel)};
        queues[static_cast<size_t>(priority)].push_back(std::move(work));
        ++batch_state->pending;
        ++generation->pending;
        ++generation->queued;
        SchedulePumpsLocked(generation);
        return true;
    }

    void CompleteWork(const WorkItem& work, std::exception_ptr failure)
    {
        std::lock_guard<std::mutex> guard(mutex);
        RecordFailureLocked(work.generation, work.batch, failure);
        if (work.batch->pending)
            --work.batch->pending;
        if (work.generation->pending)
            --work.generation->pending;
        work.batch->completed.notify_all();
        work.generation->completed.notify_all();
    }

    void Execute(WorkItem& work)
    {
        bool execute = false;
        {
            std::lock_guard<std::mutex> guard(mutex);
            execute = current_generation == work.generation && !work.generation->cancelled;
        }

        std::exception_ptr failure;
        if (execute)
        {
            try
            {
                work.function();
            }
            catch (...)
            {
                failure = std::current_exception();
            }
        }
        else if (work.cancel)
        {
            try
            {
                work.cancel();
            }
            catch (...)
            {
                failure = std::current_exception();
            }
        }
		if (!execute && !failure)
		{
			std::lock_guard<std::mutex> guard(mutex);
			failure = work.generation->failure;
		}
        CompleteWork(work, failure);
    }

    void Pump(const std::shared_ptr<GenerationState>& generation)
    {
        for (;;)
        {
            WorkItem work;
            {
                std::lock_guard<std::mutex> guard(mutex);
                if (!PopWorkLocked(generation, work))
                {
                    --generation->active_pumps;
                    generation->pumps_idle.notify_all();
                    return;
                }
            }
            Execute(work);
        }
    }

    bool TryExecuteOne(const std::shared_ptr<GenerationState>& generation)
    {
        WorkItem work;
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (!PopWorkLocked(generation, work))
                return false;
        }
        Execute(work);
        return true;
    }

    bool TryExecuteOne(const std::shared_ptr<BatchState>& batch)
    {
        WorkItem work;
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (!PopWorkLocked(batch, work))
                return false;
        }
        Execute(work);
        return true;
    }

    void WaitForGeneration(const std::shared_ptr<GenerationState>& generation)
    {
        for (;;)
        {
            {
                std::lock_guard<std::mutex> guard(mutex);
                if (!generation->pending)
                    break;
            }

            if (TryExecuteOne(generation))
                continue;

            std::unique_lock<std::mutex> guard(mutex);
            generation->completed.wait(guard, [&generation] { return !generation->pending; });
        }

        std::unique_lock<std::mutex> guard(mutex);
        generation->pumps_idle.wait(guard, [&generation] { return !generation->active_pumps; });
    }

    void FinishGeneration(const std::shared_ptr<GenerationState>& generation, bool cancel)
    {
		if (!cancel)
		{
			// Let every active producer finish while nested submissions are
			// still legal. Closing first makes a late texture child look like a
			// cancelled load even though normal finalization was requested.
			WaitForGeneration(generation);
		}

		{
			std::lock_guard<std::mutex> guard(mutex);
			generation->accepting = false;
			generation->cancelled = generation->cancelled || cancel;
			SchedulePumpsLocked(generation);
		}

		// A non-executor producer may have raced the quiescent check but
		// submitted before accepting was cleared. Drain that final work too.
		WaitForGeneration(generation);

        std::exception_ptr failure;
        try
        {
            generation->pumps->wait();
        }
        catch (...)
        {
            failure = std::current_exception();
        }

        {
            std::lock_guard<std::mutex> guard(mutex);
            if (!failure)
                failure = generation->failure;
            if (current_generation == generation)
                current_generation.reset();
        }

        if (!cancel && failure)
            std::rethrow_exception(failure);
    }
};
