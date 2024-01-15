#include "stdafx.h"
#include "atlas_submit_queue.h"
#include "stats_submitter.h"
#include "login_manager.h"
#include "profile_store.h"
#include "MainMenu.h"

atlas_submit_queue::atlas_submit_queue(gamespy_profile::stats_submitter* stats_submitter) :
	m_stats_submitter(stats_submitter),
	m_atlas_in_process(false)
{
	VERIFY(m_stats_submitter);
	m_atlas_submitted.bind(this, &atlas_submit_queue::atlas_submitted);
}

atlas_submit_queue::~atlas_submit_queue()
{
}

void atlas_submit_queue::submit_all()
{
}

void atlas_submit_queue::submit_reward(gamespy_profile::enum_awards_t const award_id)
{
}

void atlas_submit_queue::submit_best_results()
{
}

void atlas_submit_queue::update()
{
	if (m_reward_tasks.empty() || is_active())
		return;
}

void atlas_submit_queue::do_atlas_reward(gamespy_gp::profile const* profile,
                                         gamespy_profile::enum_awards_t const award_id,
                                         u32 const count)
{
	VERIFY(m_stats_submitter);
	VERIFY(!m_atlas_in_process);

	m_atlas_in_process = true;

	m_stats_submitter->reward_with_award(award_id, count, profile, m_atlas_submitted);
}

void atlas_submit_queue::do_atlas_best_results(gamespy_gp::profile const* profile, gamespy_profile::all_best_scores_t* br_ptr)
{
	VERIFY(m_stats_submitter);
	VERIFY(!m_atlas_in_process);

	m_atlas_in_process = true;

	m_stats_submitter->set_best_scores(br_ptr, profile, m_atlas_submitted);
}

void atlas_submit_queue::do_atlas_submit_all(gamespy_gp::profile const* profile)
{
	VERIFY(m_stats_submitter);
	VERIFY(!m_atlas_in_process);
}

void __stdcall atlas_submit_queue::atlas_submitted(bool result, char const* err_string)
{
	if (result)
	{
		Msg("* submit complete successfully !");
	}
	else
	{
		Msg("! failed to submit atlas report: %s", err_string);
	}
	m_atlas_in_process = false;
}
