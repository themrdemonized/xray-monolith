#include "stdafx.h"
#include "stats_submitter.h"
#include "login_manager.h"
#include "MainMenu.h"
#include "profile_store.h"
#include "awards_store.h"
#include "best_scores_store.h"

namespace gamespy_profile
{
	stats_submitter::stats_submitter(CGameSpy_Full* fullgs) :
		m_ltx_file(
			p_number,
			q_number,
			g_number,
			&stats_submitter::fill_private_key)
	{
		shedule.t_min = 500;
		shedule.t_max = 1000;
		//VERIFY(fullgs);
		//m_fullgs_obj				= fullgs;
		//m_atlas_obj					= fullgs->GetGameSpyATLAS();
		//VERIFY(m_atlas_obj);
		m_last_operation_profile = NULL;
		//m_atlas_report				= NULL;
		m_last_best_scores = NULL;
		//ZeroMemory					(m_atlas_connection_id, sizeof(m_atlas_connection_id));
	}

	stats_submitter::~stats_submitter()
	{
	}

	void stats_submitter::reward_with_award(enum_awards_t award_id, u32 const count, gamespy_gp::profile const* profile,
	                                        store_operation_cb opcb)
	{
		if (!opcb)
		{
			m_last_operation_cb.bind(this, &stats_submitter::onlylog_operation);
		}
		else
		{
			m_last_operation_cb = opcb;
		}
		R_ASSERT(!m_last_operation_profile);

		m_last_operation_profile = profile;
		m_report_type = ert_set_award;
		m_last_award_id = award_id;
		m_last_award_count = count;

		begin_session();
	}


	void stats_submitter::set_best_scores(all_best_scores_t const* scores, gamespy_gp::profile const* profile,
	                                      store_operation_cb opcb)
	{
		if (!opcb)
		{
			m_last_operation_cb.bind(this, &stats_submitter::onlylog_operation);
		}
		else
		{
			m_last_operation_cb = opcb;
		}
		R_ASSERT(!m_last_operation_profile);

		m_last_operation_profile = profile;
		m_report_type = ert_set_best_scores;
		m_last_best_scores = scores;

		begin_session();
	}

	void stats_submitter::submit_all(all_awards_t const* awards,
	                                 all_best_scores_t const* scores,
	                                 gamespy_gp::profile const* profile,
	                                 store_operation_cb opcb)
	{
		if (!opcb)
		{
			m_last_operation_cb.bind(this, &stats_submitter::onlylog_operation);
		}
		else
		{
			m_last_operation_cb = opcb;
		}
		R_ASSERT(!m_last_operation_profile);

		m_last_operation_profile = profile;
		m_report_type = ert_synchronize_profile;
		m_last_all_awards = awards;
		m_last_best_scores = scores;

		begin_session();
	}

	void stats_submitter::shedule_Update(u32 dt)
	{
	}


	void __stdcall stats_submitter::onlylog_operation(bool const result, char const* err_descr)
	{
		if (!result)
		{
			Msg("! Store operation ERROR: %s", err_descr ? err_descr : "unknown");
			return;
		}
		Msg("* Store operation successfullly complete.");
	}

	u32 const stats_submitter::operation_timeout_value = 60000; //60 seconds
	void stats_submitter::begin_session()
	{
	}

	bool stats_submitter::prepare_report()
	{
		return false;
	}

	bool stats_submitter::add_player_name_to_report()
	{
		return true;
	}

	bool stats_submitter::create_award_inc_report()
	{
		__time32_t tmp_time = 0;
		_time32(&tmp_time);

		return add_player_name_to_report();
	}

	bool stats_submitter::create_best_scores_report()
	{
		return add_player_name_to_report();
	}

	bool stats_submitter::create_all_awards_report()
	{
		return add_player_name_to_report();
	}

	void stats_submitter::terminate_session()
	{
		m_last_operation_cb.clear();
		m_last_operation_profile = NULL;
		//m_atlas_report				= NULL;
		m_last_best_scores = NULL;
		//ZeroMemory					(m_atlas_connection_id, sizeof(m_atlas_connection_id));
	}

	void stats_submitter::quick_reward_with_award(enum_awards_t award_id, gamespy_gp::profile const* profile)
	{
	}

	void stats_submitter::quick_set_best_scores(all_best_scores_t const* scores, gamespy_gp::profile const* profile)
	{
	}

	void stats_submitter::save_file(gamespy_gp::profile const* profile)
	{
	}


	void stats_submitter::fill_private_key(crypto::xr_dsa::private_key_t& priv_key_dest)
	{
		priv_key_dest.m_value[0] = 0x82;
		priv_key_dest.m_value[1] = 0xf0;
		priv_key_dest.m_value[2] = 0x41;
		priv_key_dest.m_value[3] = 0xe9;
		priv_key_dest.m_value[4] = 0x9a;
		priv_key_dest.m_value[5] = 0x98;
		priv_key_dest.m_value[6] = 0xa0;
		priv_key_dest.m_value[7] = 0xf9;
		priv_key_dest.m_value[8] = 0xff;
		priv_key_dest.m_value[9] = 0xe1;
		priv_key_dest.m_value[10] = 0x14;
		priv_key_dest.m_value[11] = 0x48;
		priv_key_dest.m_value[12] = 0xb3;
		priv_key_dest.m_value[13] = 0xde;
		priv_key_dest.m_value[14] = 0xe3;
		priv_key_dest.m_value[15] = 0x69;
		priv_key_dest.m_value[16] = 0xea;
		priv_key_dest.m_value[17] = 0x16;
		priv_key_dest.m_value[18] = 0x64;
		priv_key_dest.m_value[19] = 0xf2;
	}
} //namespace gamespy_profile
