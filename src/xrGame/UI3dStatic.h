#pragma once
#include "ui/UIWindow.h"
#include "ui/UIStatic.h"
#include "../../Include/xrRender/RenderVisual.h"
#include "../../Include/xrRender/Kinematics.h"

class CGameObject;
class CScriptGameObject;
class script_attachment;

class CUI3dStatic : public CUIStatic
{
    typedef CUIStatic inherited;
public:
    CUI3dStatic();
    virtual ~CUI3dStatic();

    // Called once immediately before the normal HUD UI traversal.
    static void PrepareAtlas();

    void SetRotation(float x, float y, float z) { m_angle.set(x, y, z); }
    void SetScale(float scale) { m_scale = scale; }
    void SetObject(CScriptGameObject* obj);
    void SetVisual(LPCSTR name);

    virtual void Draw();

protected:
    Fvector m_angle;
    float dist, m_scale;
    bool m_bFirstUpdate;
    void FromScreenToItem(int x_screen, int y_screen, float& x_item, float& y_item);
    void FromTargetToItem(float x, float y, float target_width, float target_height, float target_aspect,
        float& x_item, float& y_item);
    void QueueVisuals(const Frect& target_rect, float target_width, float target_height, float target_aspect);
    bool ValidateCurrentItem();
    Fmatrix DrawVisualIcon(IRenderVisual* visual, vis_data& vis_data, const Frect& target_rect,
        float target_width, float target_height, float target_aspect);
    void DrawAttachmentIcon(script_attachment* att, const Fmatrix& matrix, IKinematics* parent);

    CGameObject* m_pCurrentItem;
    u16 m_currentItemId;
    IRenderVisual* m_pFakeVisual;
    CUIStaticItem m_atlasItem;
    Frect m_atlasRect;
    Frect m_atlasContentRect;
    Fvector2 m_atlasDrawMargin;
    u32 m_atlasFrame;
    bool m_atlasShaderCreated;
};
