////////////////////////////////////////////////////////////////////////////
// Module : edit_actions.cpp
// Created : 04.03.2008
// Author : Evgeniy Sokolov
// Description : edit actions chars class implementation
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "edit_actions.h"
#include "line_edit_control.h"
#include "xr_input.h"
#include <locale.h>
#include <locale>

namespace text_editor
{
    base::base() : m_previous_action(NULL)
    {
    }

    base::~base()
    {
        xr_delete(m_previous_action);
    }

    void base::on_assign(base* const prev_action)
    {
        m_previous_action = prev_action;
    }

    void base::on_key_press(line_edit_control* const control)
    {
        if (m_previous_action)
        {
            m_previous_action->on_key_press(control);
        }
    }

    // -------------------------------------------------------------------------------------------------

    callback_base::callback_base(Callback const& callback, u32 const dik, key_state state)
    {
        m_callback = callback;
        m_run_state = state;
        m_dik = dik;
    }

    callback_base::~callback_base()
    {
    }

    static inline bool is_numpad_numkey(u32 dik)
    {
        switch (dik)
        {
        case DIK_NUMPAD0:
        case DIK_NUMPAD1:
        case DIK_NUMPAD2:
        case DIK_NUMPAD3:
        case DIK_NUMPAD4:
        case DIK_NUMPAD5:
        case DIK_NUMPAD6:
        case DIK_NUMPAD7:
        case DIK_NUMPAD8:
        case DIK_NUMPAD9:
            return true;
            break;
        }
        return false;
    }

    void callback_base::on_key_press(line_edit_control* const control)
    {
        if (control->get_key_state(m_run_state))
        {
            if (
                (!is_numpad_numkey(m_dik)) ||
                (control->get_num_lock_state()) && (  
                    (m_run_state == ks_NumLock && (!control->get_key_state(ks_Ctrl) && !control->get_key_state(ks_Alt)))
                    || (m_run_state == ks_NumLk_Ctrl && control->get_key_state(ks_Ctrl))
                    || (m_run_state == ks_NumLk_Alt && control->get_key_state(ks_Alt)))
                )
            {
                m_callback();
                return;
            }
		}
		base::on_key_press(control);
	}
	// -------------------------------------------------------------------------------------------------

	type_pair::type_pair(u32 dik, char c, char c_shift, bool b_translate)
	{
		init(dik, c, c_shift, b_translate);
	}

	type_pair::~type_pair()
	{
	}

	void type_pair::init(u32 dik, char c, char c_shift, bool b_translate)
	{
		m_translate = b_translate;
		m_dik = dik;
		m_char = c;
		m_char_shift = c_shift;
	}

	void type_pair::on_key_press(line_edit_control* const control)
	{
		char out[16] = {};
		const bool shift = control->get_key_state(ks_Shift);
		const bool caps = control->get_key_state(ks_CapsLock);
		const bool ctrl = control->get_key_state(ks_Ctrl);
		const bool alt = control->get_key_state(ks_Alt);
		const bool altgr = control->get_key_state(ks_RAlt);
		if (m_translate) {
			if (pInput->dik_to_text((int)m_dik, shift, caps, ctrl, alt, altgr, out, sizeof(out)))
			{
				control->insert_text(out);
				return;
			}

			// If unrepresentable, do nothing (or insert fallback char?)
			return;
		}

		// Fallback to old static mapping
		char c = m_char;
		if (shift != caps)
			c = m_char_shift;

		control->insert_character(c);
	}

	// -------------------------------------------------------------------------------------------------

	key_state_base::key_state_base(key_state state, base* type_pair)
		: m_type_pair(type_pair), m_state(state)
	{
	}

	key_state_base::~key_state_base()
	{
		xr_delete(m_type_pair);
	}

	void key_state_base::on_key_press(line_edit_control* const control)
	{
		control->set_key_state(m_state, true);
		if (m_type_pair)
			m_type_pair->on_key_press(control);
	}
} // namespace text_editor
