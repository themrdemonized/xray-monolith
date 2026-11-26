#include "stdafx.h"
#include "xrSheduler.h"
#include "xr_object.h"
#include "IGame_Level.h"

#include "../xrCore/profiler.h"

float psShedulerCurrent = 10.f;
float psShedulerTarget = 10.f;
const float psShedulerReaction = 0.1f;

//-------------------------------------------------------------------------------------
void CSheduler::Initialize()
{
	m_current_step_obj = NULL;
	m_processing_now = false;
	m_processing_nowRT = false;
	m_bTerminating = false;
}

void CSheduler::Destroy()
{
	m_bTerminating = true;
	internal_Registration();

	{
		xrSRWLockGuard g(ItemsLock);
		for (u32 it = 0; it < Items.size(); it++)
		{
			if (0 == Items[it].Object)
			{
				Items.erase(Items.begin() + it);
				it--;
			}
		}
	}
	
#ifdef DEBUG
    if (!Items.empty())
    {
        string1024 _objects;
        _objects[0] = 0;

        Msg("! Sheduler work-list is not empty");
        for (u32 it = 0; it < Items.size(); it++)
            Msg("%s", Items[it].Object->shedule_Name().c_str());
    }
#endif // DEBUG

	ItemsRT.clear();

	{
		xrSRWLockGuard g(ItemsLock);
		Items.clear();
	}
	
	Registration.clear();
}

void CSheduler::internal_Registration()
{
	xr_unordered_flat_map<ISheduled*, u32> final_states;
	final_states.reserve(Registration.size());

	// Get latest state of sheduled object
	for (u32 it = 0; it < Registration.size(); it++)
	{
		ItemReg& R = Registration[it];
		final_states[R.Object] = it;
	}

	for (const auto& [object, it] : final_states)
	{
		ItemReg& R = Registration[it];
		if (R.OP)
			internal_Register(R.Object, R.RT);
		else
			internal_Unregister(R.Object, R.RT);
	}

	Registration.clear();
}

void CSheduler::internal_Register(ISheduled* O, BOOL RT)
{
	VERIFY(!O->shedule.b_locked);
	if (RT)
	{
		// Fill item structure
		Item TNext;
		TNext.dwTimeForExecute = Device.dwTimeGlobal;
		TNext.dwTimeOfLastExecute = Device.dwTimeGlobal;
		TNext.Object = O;
		TNext.scheduled_name = O->shedule_Name();
		O->shedule.b_RT = TRUE;

		ItemsRT.push_back(std::move(TNext));
	}
	else
	{
		// Fill item structure
		Item TNext;
		TNext.dwTimeForExecute = Device.dwTimeGlobal;
		TNext.dwTimeOfLastExecute = Device.dwTimeGlobal;
		TNext.Object = O;
		TNext.scheduled_name = O->shedule_Name();
		O->shedule.b_RT = FALSE;

		// Insert into priority Queue
		Push(std::move(TNext));
	}
}

bool CSheduler::internal_Unregister(ISheduled* O, BOOL RT, bool warn_on_not_found)
{
	//the object may be already dead
	//VERIFY (!O->shedule.b_locked) ;
	if (RT)
	{
		for (u32 i = 0; i < ItemsRT.size(); i++)
		{
			if (ItemsRT[i].Object == O)
			{
				ItemsRT.erase(ItemsRT.begin() + i);
				return (true);
			}
		}
	}
	else
	{
		if (m_current_step_obj == O)
		{
			m_current_step_obj = NULL;
			return true;
		}

		xrSRWLockGuard g(ItemsLock);
		for (u32 i = 0; i < Items.size(); i++)
		{
			if (Items[i].Object == O)
			{
				Items[i].Object = NULL;
				return (true);
			}
		}
	}

#ifdef DEBUG
    if (warn_on_not_found)
        Msg("! scheduled object %s tries to unregister but is not registered", *O->shedule_Name());
#endif // DEBUG

	return (false);
}

#ifdef DEBUG
bool CSheduler::Registered(ISheduled* object) const
{
    u32 count = 0;
    typedef xr_vector<Item> ITEMS;

    {
        ITEMS::const_iterator I = ItemsRT.begin();
        ITEMS::const_iterator E = ItemsRT.end();
        for (; I != E; ++I)
            if ((*I).Object == object)
            {
                // Msg ("0x%8x found in RT",object);
                count = 1;
                break;
            }
    }
    {
        ITEMS::const_iterator I = Items.begin();
        ITEMS::const_iterator E = Items.end();
        for (; I != E; ++I)
            if ((*I).Object == object)
            {
                // Msg ("0x%8x found in non-RT",object);
                VERIFY(!count);
                count = 1;
                break;
            }
    }

    {
        ITEMS::const_iterator I = ItemsProcessed.begin();
        ITEMS::const_iterator E = ItemsProcessed.end();
        for (; I != E; ++I)
            if ((*I).Object == object)
            {
                // Msg ("0x%8x found in process items",object);
                VERIFY(!count);
                count = 1;
                break;
            }
    }

    typedef xr_vector<ItemReg> ITEMS_REG;
    ITEMS_REG::const_iterator I = Registration.begin();
    ITEMS_REG::const_iterator E = Registration.end();
    for (; I != E; ++I)
    {
        if ((*I).Object == object)
        {
            if ((*I).OP)
            {
                // Msg ("0x%8x found in registration on register",object);
                VERIFY(!count);
                ++count;
            }
            else
            {
                // Msg ("0x%8x found in registration on UNregister",object);
                VERIFY(count == 1);
                --count;
            }
        }
    }

    if (!count && (m_current_step_obj == object))
    {
        VERIFY2(m_processing_now, "trying to unregister self unregistering object while not processing now");
        count = 1;
    }
    VERIFY(!count || (count == 1));
    return (count == 1);
}
#endif // DEBUG

void CSheduler::Register(ISheduled* A, BOOL RT)
{
#ifdef DEBUG
	VERIFY(!Registered(A));
#endif
	ItemReg R;
	R.OP = TRUE;
	R.RT = RT;
	R.Object = A;
	R.Object->shedule.b_RT = RT;

	Registration.push_back(std::move(R));
}

void CSheduler::Unregister(ISheduled* A)
{
#ifdef DEBUG
	VERIFY(Registered(A));
#endif

	// wait until done with m_current_step_obj
	if (m_current_step_obj == A)
	{
		// Loop until the worker releases the object
        xrSpinWait w;
		while (m_current_step_obj == A)
		{
            w();
		}
	}

	if (!( (!A->shedule.b_RT && m_processing_now) || (A->shedule.b_RT && m_processing_nowRT) ))
	{
		if (internal_Unregister(A, A->shedule.b_RT, false))
			return;
	}

	ItemReg R;
	R.OP = FALSE;
	R.RT = A->shedule.b_RT;
	R.Object = A;

	Registration.push_back(std::move(R));
}

void CSheduler::EnsureOrder(ISheduled* Before, ISheduled* After)
{
	VERIFY(Before->shedule.b_RT && After->shedule.b_RT);

	for (u32 i = 0; i < ItemsRT.size(); i++)
	{
		if (ItemsRT[i].Object == After)
		{
			Item A = ItemsRT[i];
			ItemsRT.erase(ItemsRT.begin() + i);
			ItemsRT.push_back(std::move(A));
			return;
		}
	}
}

void CSheduler::PushImpl(Item& I)
{
	Items.push_back(I);
	std::push_heap(Items.begin(), Items.end());
}

void CSheduler::PushImpl(Item&& I)
{
	Items.push_back(std::move(I));
	std::push_heap(Items.begin(), Items.end());
}

void CSheduler::Push(Item& I)
{
	xrSRWLockGuard g(ItemsLock);
	PushImpl(I);
}

void CSheduler::Push(Item&& I)
{
	xrSRWLockGuard g(ItemsLock);
	PushImpl(std::move(I));
}

void CSheduler::PopImpl()
{
	std::pop_heap(Items.begin(), Items.end());
	Items.pop_back();
}

void CSheduler::Pop()
{
	xrSRWLockGuard g(ItemsLock);
	PopImpl();
}

int SchedulerBatchSize = 256;
BOOL SchedulerLog = FALSE;
extern ENGINE_API IGame_Level* g_pGameLevel;
void CSheduler::ProcessStep()
{
	// Normal priority
	u32 dwTime = Device.dwTimeGlobal;
	static xr_vector<Item> ItemsBatch;
	ItemsBatch.clear();
	u32 ItemsCount = Items.size();
	float target = psShedulerTarget;

    u32 batchSize = g_pGameLevel && g_pGameLevel->schedulerFlush ? 65536 : SchedulerBatchSize;

	{
		xrSRWLockGuard g(ItemsLock);
		while (!Items.empty() && Top().dwTimeForExecute < dwTime && ItemsBatch.size() < batchSize)
		{
			// Optional: Also stop collecting if we are already out of time
			// (Prevents grabbing items we won't even touch)
			if (Device.dwPrecacheFrame == 0 && CPU::QPC() > cycles_limit)
				break;

			// Add item to batch
			Item& T = Top();
			if (T.Object && T.Object->shedule_Needed())
			{
				ItemsBatch.push_back(std::move(T));
			}	
			PopImpl();
		}
	}

	if (ItemsBatch.empty())
	{
		// always try to decrease target
		psShedulerTarget -= psShedulerReaction;
		return;
	}

	static xr_vector<Item> ItemsProcessed;
	ItemsProcessed.clear();
	int i = 0;
	{
		for (i = 0; i < ItemsBatch.size(); ++i)
		{
			// Stop if about to exit
			if (m_bTerminating)
				break;

			// Stop if really underestimated the cost of batch, check every 8th item
			if ((i % 8) == 0 && Device.dwPrecacheFrame == 0 && CPU::QPC() > cycles_limit)
			{
				psShedulerTarget += (psShedulerReaction * 3);
				break;
			}

			Item& T = ItemsBatch[i];
			if (!T.Object)
				continue;
			m_current_step_obj = T.Object;

			// Update
			u32 Elapsed = dwTime - T.dwTimeOfLastExecute;

			// Calc next update interval
			u32 dwMin = _max(u32(30), T.Object->shedule.t_min);
			u32 dwMax = (1000 + T.Object->shedule.t_max) / 2;
			float scale = T.Object->shedule_Scale();
			u32 dwUpdate = dwMin + iFloor(float(dwMax - dwMin) * scale);
			clamp(dwUpdate, u32(_max(dwMin, u32(20))), dwMax);

			T.Object->shedule_Update(clampr(Elapsed, u32(1), u32(_max(u32(T.Object->shedule.t_max), u32(1000)))));

			// Fill item structure
			Item TNext;
			TNext.dwTimeForExecute = dwTime + dwUpdate;
			TNext.dwTimeOfLastExecute = dwTime;
			TNext.Object = T.Object;
			TNext.scheduled_name = T.Object->shedule_Name();
			ItemsProcessed.push_back(std::move(TNext));

			m_current_step_obj = NULL;
		}
	}

	if (SchedulerLog)
		Msg("Sheduler: cycles_start[%llu] cycles_limit[%llu] qpc_freq[%llu] Target[%f] psShedulerReaction[%f] NewTarget[%f] ItemsCount[%d] ItemsBatch[%d], ItemsActuallyProcessed [%d]",
			cycles_start,
			cycles_limit,
			CPU::qpc_freq,
			target,
			psShedulerReaction,
			psShedulerTarget,
			ItemsCount,
			ItemsBatch.size(),
			i);
	
	// Reinsertion
	{
		// Strategy
		// Rule of thumb: if k < n / log2(n), push_heap is faster. With default batch size of 128 its always faster, start choosing only with 256
		// Otherwise, dump and heapify
		// For n=3000, log2(n) is ~11.5. n/11.5 is ~260.
		// We use a simple approximation: n >> 4 (which is n / 16) for a conservative safety margin.
		u32 k = ItemsProcessed.size() + (ItemsBatch.size() - i);
		u32 n = Items.size();

		xrSRWLockGuard g(ItemsLock);

		if (k < 256 || k < (n >> 4))
		{
			// Push finished
			for (auto& T : ItemsProcessed)
			{
				if (T.Object)
					PushImpl(std::move(T));
			}

			// Push unprocessed
			for (int j = i; j < ItemsBatch.size(); ++j)
			{
				if (ItemsBatch[j].Object)
					PushImpl(std::move(ItemsBatch[j]));
			}
		}
		else
		{
			// Push finished
			for (auto& T : ItemsProcessed)
			{
				if (T.Object)
					Items.push_back(std::move(T));
			}

			// Push unprocessed
			for (int j = i; j < ItemsBatch.size(); ++j)
			{
				if (ItemsBatch[j].Object)
					Items.push_back(std::move(ItemsBatch[j]));
			}

			// Heapify
			std::make_heap(Items.begin(), Items.end());
		}
		ItemsProcessed.clear();
	}
	
	// always try to decrease target
	psShedulerTarget -= psShedulerReaction;
}

void CSheduler::UpdateInit()
{
	PROF_EVENT();
	R_ASSERT(Device.Statistic);
	// Initialize
	Device.Statistic->Sheduler.Begin();
	internal_Registration();
}

void CSheduler::UpdateRT()
{
	PROF_EVENT();

	// Realtime priority
	m_processing_nowRT = true;
	u32 dwTime = Device.dwTimeGlobal;
	for (u32 it = 0; it < ItemsRT.size(); it++)
	{
		Item& T = ItemsRT[it];
		R_ASSERT(T.Object);
		if (!T.Object->shedule_Needed())
		{
			T.dwTimeOfLastExecute = dwTime;
			continue;
		}

		u32 Elapsed = dwTime - T.dwTimeOfLastExecute;
#ifdef DEBUG
		VERIFY(T.Object->dbg_startframe != Device.dwFrame);
		T.Object->dbg_startframe = Device.dwFrame;
#endif
		T.Object->shedule_Update(Elapsed);
		T.dwTimeOfLastExecute = dwTime;
	}
	m_processing_nowRT = false;
}

void CSheduler::UpdateDeferred()
{
	PROF_EVENT();

	cycles_start = CPU::QPC();
	cycles_limit = CPU::qpc_freq * u64(iCeil(psShedulerCurrent)) / 1000ull + cycles_start;

	// Normal (sheduled)
	m_processing_now = true;
	ProcessStep();
	m_processing_now = false;
}

void CSheduler::UpdateFinalize()
{
	PROF_EVENT();

	clamp(psShedulerTarget, 3.f, 66.f);
	psShedulerCurrent = 0.9f * psShedulerCurrent + 0.1f * psShedulerTarget;
	Device.Statistic->fShedulerLoad = psShedulerCurrent;

	// Finalize
	internal_Registration();
	Device.Statistic->Sheduler.End();
}

void CSheduler::Update()
{
	PROF_EVENT();

	UpdateInit();
	UpdateRT();
	UpdateDeferred();
	UpdateFinalize();
}
