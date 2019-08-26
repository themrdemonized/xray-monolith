#pragma once

#include "../xrCore/intrusive_ptr.h"

class CPhraseDialog;

typedef intrusive_ptr<CPhraseDialog> DIALOG_SHARED_PTR;

#include "PhraseDialog.h"
#include "ui\UI3tButton.h"

DEFINE_VECTOR(shared_str, DIALOG_ID_VECTOR, DIALOG_ID_IT);
DEFINE_VECTOR(CUI3tButton*, ButtonVector, ButtonVec_IT);