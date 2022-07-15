#pragma once

#include "uiwindow.h"
#include "../UIDialogHolder.h"
class CDialogHolder;

class CUIDialogWnd : public CUIWindow
{
private:
	typedef CUIWindow inherited;
	CDialogHolder* m_pParentHolder;
protected:
public:
	bool m_bWorkInPause;
	bool m_bAllowMovement;
	bool m_bNeedCursor;
	bool m_bNeedCenterCursor;
	CUIDialogWnd();
	virtual ~CUIDialogWnd();

	virtual void Show(bool status);

	virtual bool OnKeyboardAction(int dik, EUIMessages keyboard_action);
	virtual bool OnKeyboardHold(int dik);

	CDialogHolder* GetHolder() { return m_pParentHolder; };
	void SetHolder(CDialogHolder* h) { m_pParentHolder = h; };
	void AllowMovement(bool b) { m_bAllowMovement = b; }
	void AllowCursor(bool b) { m_bNeedCursor = b; }
	void AllowCenterCursor(bool b) { m_bNeedCenterCursor = b; }
	virtual bool StopAnyMove() { return !m_bAllowMovement; }
	virtual bool NeedCursor() const { return m_bNeedCursor; }
	virtual bool NeedCenterCursor() const { return m_bNeedCenterCursor; }
	virtual bool WorkInPause() const { return m_bWorkInPause; }
	
	virtual bool Dispatch(int cmd, int param) { return true; }
	void ShowDialog(bool bDoHideIndicators);
	void HideDialog();

	virtual bool IR_process();
};
