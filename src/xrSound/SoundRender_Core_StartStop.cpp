#include "stdafx.h"
#pragma hdrstop

#include "SoundRender_Core.h"
#include "SoundRender_Emitter.h"
#include "SoundRender_Target.h"
#include "SoundRender_Source.h"

void CSoundRender_Core::i_start(CSoundRender_Emitter* E)
{
	R_ASSERT(E);
	if (!E->owner_data || !E->owner_data->handle)
	{
		Msg("! sound: i_start skipped emitter without valid source handle");
		E->stop(FALSE);
		return;
	}

	// Search lowest-priority target (never steal targets from other persistent streams)
	float Ptest = E->priority();
	float Ptarget = flt_max;
	CSoundRender_Target* T = 0;
	for (u32 it = 0; it < s_targets.size(); it++)
	{
		CSoundRender_Target* Ttest = s_targets[it];
		CSoundRender_Emitter* cur = Ttest->get_emitter();
		if (cur && cur != E && cur->is_persistent())
			continue;
		if (Ttest->priority < Ptarget)
		{
			T = Ttest;
			Ptarget = Ttest->priority;
		}
	}
	if (!T)
		return;

	// Stop currently playing (never cancel persistent emitters)
	if (T->get_emitter())
	{
		CSoundRender_Emitter* victim = T->get_emitter();
		if (victim != E && !victim->is_persistent())
			victim->cancel();
	}

	// Associate
	E->target = T;
	E->target->start(E);
	T->priority = Ptest;
	
	//v2v3v4 in
	if (E->target->get_emitter())
	{
		float dist = SoundRender->listener_position().distance_to(E->target->get_emitter()->p_source.position);
		if (dist > E->target->get_emitter()->p_source.max_distance && !E->target->get_emitter()->b2D)
			E->target->get_emitter()->cancel();
	}
	//v2v3v4 out
}

void CSoundRender_Core::i_stop(CSoundRender_Emitter* E)
{
	// Msg					("- %10s : %3d[%1.4f] : %s","i_stop",E->dbg_ID,E->priority(),E->source->fname);
	R_ASSERT(E);
	R_ASSERT(E == E->target->get_emitter());
	E->target->stop();
	E->target = NULL;
}

void CSoundRender_Core::i_rewind(CSoundRender_Emitter* E)
{
	// Msg					("- %10s : %3d[%1.4f] : %s","i_rewind",E->dbg_ID,E->priority(),E->source->fname);
	R_ASSERT(E);
	R_ASSERT(E == E->target->get_emitter());
	E->target->rewind();
}

BOOL CSoundRender_Core::i_allow_play(CSoundRender_Emitter* E)
{
	// Search available target
	float Ptest = E->priority();
	for (u32 it = 0; it < s_targets.size(); it++)
	{
		CSoundRender_Target* T = s_targets[it];
		CSoundRender_Emitter* cur = T->get_emitter();
		if (cur && cur != E && cur->is_persistent())
			continue;
		if (T->priority < Ptest) return TRUE;
	}
	return FALSE;
}
