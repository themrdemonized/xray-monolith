#include "stdafx.h"
#include "UI3dStatic.h"
#include "gameobject.h"
#include "HUDManager.h"
#include "../../Include/xrRender/RenderVisual.h"
#include "..\xrEngine\device.h"
#include "Actor.h"
#include "Level.h"
#include "script_attachment_manager.h"
#include "ui_base.h"
#include "ui/UIScrollView.h"

#include <algorithm>

#define DIST UI_3D_ICON_DISTANCE

namespace
{
constexpr u32 UI_3D_ICON_ATLAS_PADDING = 4;
constexpr u32 UI_3D_ICON_FILTER_MARGIN = 6;
LPCSTR UI_3D_ICON_ATLAS_TEXTURE = "$user$ui_3d_icons";

xr_vector<CUI3dStatic*> ui_3d_statics;
xr_vector<IRenderVisual*> temp_visuals;

bool IsEffectivelyVisible(CUIWindow* window)
{
	Frect window_rect;
	window->GetAbsoluteRect(window_rect);
	const Frect screen_rect = {0.f, 0.f, UI_BASE_WIDTH, UI_BASE_HEIGHT};
	if (!screen_rect.intersected(window_rect))
		return false;

    for (CUIWindow* current = window; current; current = current->GetParent())
    {
        if (!current->IsShown())
            return false;

		CUIScrollView* scroll_view = smart_cast<CUIScrollView*>(current);
		if (scroll_view)
		{
			Frect visible_rect;
			scroll_view->GetVisibleRect(visible_rect);
			if (!visible_rect.intersected(window_rect))
				return false;
		}
	}
    return true;
}

void DeleteTemporaryVisuals()
{
    for (IRenderVisual* visual : temp_visuals)
        ::Render->model_Delete(visual);
    temp_visuals.clear();
}
}

CUI3dStatic::CUI3dStatic()
{
    m_pCurrentItem = nullptr;
    m_currentItemId = u16(-1);
    m_pFakeVisual = nullptr;
    m_angle.set(0.f, 0.f, 0.f);
    m_scale = 1.f;
    m_bFirstUpdate = true;
    dist = DIST;
    m_atlasFrame = u32(-1);
    m_atlasShaderCreated = false;
    m_atlasRect.set(0.f, 0.f, 0.f, 0.f);
    m_atlasContentRect.set(0.f, 0.f, 0.f, 0.f);
    m_atlasDrawMargin.set(0.f, 0.f);
    ui_3d_statics.push_back(this);
}

CUI3dStatic::~CUI3dStatic()
{
    ui_3d_statics.erase(std::remove(ui_3d_statics.begin(), ui_3d_statics.end(), this), ui_3d_statics.end());

    if (m_pFakeVisual)
    {
        ::Render->model_Delete(m_pFakeVisual);
        m_pFakeVisual = nullptr;
    }

}

void CUI3dStatic::PrepareAtlas()
{
    u32 atlas_target_width, atlas_target_height;
    float resolution;
    if (!::Render->GetUI3DIconAtlasInfo(atlas_target_width, atlas_target_height, resolution))
        return;

    struct AtlasRequest
    {
        CUI3dStatic* widget;
        u32 width;
        u32 height;
        u32 content_width;
        u32 content_height;
        u32 margin_x;
        u32 margin_y;
        float draw_width;
        float draw_height;
    };

    xr_vector<AtlasRequest> requests;
    requests.reserve(ui_3d_statics.size());

    for (CUI3dStatic* widget : ui_3d_statics)
    {
        widget->m_atlasFrame = u32(-1);
        const bool has_current_item = widget->ValidateCurrentItem();
        if ((!has_current_item && !widget->m_pFakeVisual) || !IsEffectivelyVisible(widget))
            continue;

        Fvector2 physical_size;
        physical_size.set(widget->GetWidth(), widget->GetHeight());
        UI().ClientToScreenScaled(physical_size);
        const u32 width = _max(1u, u32(ceilf(physical_size.x * resolution)));
        const u32 height = _max(1u, u32(ceilf(physical_size.y * resolution)));

        // Reserve space for model scale overflow and the filter/shadow taps.
        const float scale_overflow = _max(0.f, _abs(widget->m_scale) - 1.f);
        const u32 margin_x = UI_3D_ICON_FILTER_MARGIN +
            u32(ceilf(float(width) * scale_overflow * 0.5f));
        const u32 margin_y = UI_3D_ICON_FILTER_MARGIN +
            u32(ceilf(float(height) * scale_overflow * 0.5f));
        const u32 atlas_width = width + margin_x * 2;
        const u32 atlas_height = height + margin_y * 2;
        if (atlas_width + UI_3D_ICON_ATLAS_PADDING * 2 > atlas_target_width ||
            atlas_height + UI_3D_ICON_ATLAS_PADDING * 2 > atlas_target_height)
            continue;

        requests.push_back({widget, atlas_width, atlas_height, width, height, margin_x, margin_y,
            physical_size.x, physical_size.y});
    }

    if (requests.empty())
        return;

    std::sort(requests.begin(), requests.end(), [](const AtlasRequest& left, const AtlasRequest& right)
    {
        return left.height > right.height;
    });

    // Height-sorted shelf packing leaves padding around each filtered icon rectangle.
    u32 x = UI_3D_ICON_ATLAS_PADDING;
    u32 y = UI_3D_ICON_ATLAS_PADDING;
    u32 shelf_height = 0;
    xr_vector<CUI3dStatic*> packed;
    packed.reserve(requests.size());

    for (const AtlasRequest& request : requests)
    {
        if (x + request.width + UI_3D_ICON_ATLAS_PADDING > atlas_target_width)
        {
            x = UI_3D_ICON_ATLAS_PADDING;
            y += shelf_height + UI_3D_ICON_ATLAS_PADDING * 2;
            shelf_height = 0;
        }
        if (y + request.height + UI_3D_ICON_ATLAS_PADDING > atlas_target_height)
            continue;

        request.widget->m_atlasRect.set(float(x), float(y), float(x + request.width), float(y + request.height));
        request.widget->m_atlasContentRect.set(float(x + request.margin_x), float(y + request.margin_y),
            float(x + request.margin_x + request.content_width),
            float(y + request.margin_y + request.content_height));
        request.widget->m_atlasDrawMargin.set(
            float(request.margin_x) * request.draw_width / float(request.content_width),
            float(request.margin_y) * request.draw_height / float(request.content_height));
        packed.push_back(request.widget);
        x += request.width + UI_3D_ICON_ATLAS_PADDING * 2;
        shelf_height = _max(shelf_height, request.height);
    }

    if (packed.empty() || !::Render->BeginUI3DIconAtlas())
        return;

    ::Render->set_UI(true);
    for (CUI3dStatic* widget : packed)
    {
        ::Render->BeginUI3DIconAtlasItem();
        widget->QueueVisuals(widget->m_atlasContentRect, float(atlas_target_width),
            float(atlas_target_height), float(atlas_target_height) / float(atlas_target_width));
    }
    ::Render->set_UI(false);
    ::Render->EndUI3DIconAtlas();
    DeleteTemporaryVisuals();

    for (CUI3dStatic* widget : packed)
        widget->m_atlasFrame = Device.dwFrame;
}

void CUI3dStatic::FromScreenToItem(int x_screen, int y_screen, float& x_item, float& y_item)
{
    FromTargetToItem(float(x_screen), float(y_screen), 1024.f, 768.f, Device.fASPECT, x_item, y_item);
}

void CUI3dStatic::FromTargetToItem(float x, float y, float target_width, float target_height,
    float target_aspect, float& x_item, float& y_item)
{
    const float halfwidth = target_width * 0.5f;
    const float halfheight = target_height * 0.5f;
    const float half_view_height = UI_3D_ICON_ORTHO_HEIGHT * 0.5f;
    const float half_view_width = half_view_height / target_aspect;

    x_item = (x - halfwidth) * half_view_width / halfwidth;
    y_item = (halfheight - y) * half_view_height / halfheight;
}

void CUI3dStatic::Draw()
{
    if (!ValidateCurrentItem() && !m_pFakeVisual)
    {
        inherited::Draw();
        return;
    }

    if (m_atlasFrame == Device.dwFrame)
    {
        if (!m_atlasShaderCreated)
        {
            m_atlasItem.CreateShader(UI_3D_ICON_ATLAS_TEXTURE);
            m_atlasShaderCreated = true;
        }
        Frect rect;
        GetAbsoluteRect(rect);
        Fvector2 margin = m_atlasDrawMargin;
        UI().ClientToScreenScaledWidth(margin.x);
        UI().ClientToScreenScaledHeight(margin.y);
        m_atlasItem.SetPos(rect.left - margin.x, rect.top - margin.y);
        m_atlasItem.SetSize(Fvector2().set(rect.width() + margin.x * 2.f, rect.height() + margin.y * 2.f));
        m_atlasItem.SetTextureRect(m_atlasRect);
        m_atlasItem.SetTextureColor(GetTextureColor());
        // Rotation is already part of the 3D model transform. Inventory cells
        // may also carry a 2D heading for their ordinary texture icon; applying
        // it here would rotate the completed 3D icon a second time.
        m_atlasItem.Render();
        CUIWindow::Draw();
        return;
    }

    // Use the direct-render path when this widget was not packed this frame.
    Frect rect;
    GetAbsoluteRect(rect);
    ::Render->set_UI(true);
    QueueVisuals(rect, 1024.f, 768.f, Device.fASPECT);
    ::Render->set_UI(false);
    ::Render->RenderUI();
    DeleteTemporaryVisuals();

    // Draw children without rendering the inherited 2D texture over the model.
    CUIWindow::Draw();
}

void CUI3dStatic::QueueVisuals(const Frect& target_rect, float target_width, float target_height, float target_aspect)
{
    // Icons have no scene renderable owner; queued meshes must not retain the previous one.
    ::Render->set_Object(nullptr);

    if (ValidateCurrentItem())
    {
        IRenderVisual* visual = m_pCurrentItem->Visual();
        vis_data& visual_data = m_pCurrentItem->Visual()->getVisData();

        Fmatrix matrix = DrawVisualIcon(visual, visual_data, target_rect,
            target_width, target_height, target_aspect);
        for (auto pair : *m_pCurrentItem->GetAttachments())
            if (pair.second->GetType() == eSA_World)
                DrawAttachmentIcon(pair.second, matrix, visual->dcast_PKinematics());
    }
    else if (m_pFakeVisual)
        DrawVisualIcon(m_pFakeVisual, m_pFakeVisual->getVisData(), target_rect,
            target_width, target_height, target_aspect);
}

bool CUI3dStatic::ValidateCurrentItem()
{
    if (!m_pCurrentItem)
        return false;

    if (!g_pGameLevel)
    {
        m_pCurrentItem = nullptr;
        m_currentItemId = u16(-1);
        m_atlasFrame = u32(-1);
        return false;
    }

    CObject* registered_object = Level().Objects.net_Find(m_currentItemId);
    if (registered_object == m_pCurrentItem && !m_pCurrentItem->getDestroy())
    {
        // During load an object may already be registered while its render
        // visual is not ready yet. Keep the reference and retry next frame.
        return m_pCurrentItem->Visual() != nullptr;
    }

    // UI scripts can retain an item across level/quick-load teardown. Never
    // dereference that raw pointer after the object has left Level().Objects.
    m_pCurrentItem = nullptr;
    m_currentItemId = u16(-1);
    m_atlasFrame = u32(-1);
    return false;
}

Fmatrix CUI3dStatic::DrawVisualIcon(IRenderVisual* visual, vis_data& vis_data, const Frect& target_rect,
    float target_width, float target_height, float target_aspect)
{
    float x1, y1, x2, y2;

    FromTargetToItem(target_rect.left, target_rect.top, target_width, target_height, target_aspect, x1, y1);
    FromTargetToItem(target_rect.right, target_rect.bottom, target_width, target_height, target_aspect, x2, y2);

    const float available_width = _abs(x2 - x1);
    const float available_height = _abs(y2 - y1);

    Fvector box_center;
    vis_data.box.getcenter(box_center);

    Fmatrix fit_transform;
    fit_transform.translate(-box_center.x, -box_center.y, -box_center.z);
    fit_transform.mulA_43(Fmatrix().setHPB(m_angle));

    // Measure the model after applying the requested icon rotation. Fitting
    // these projected X/Y extents makes m_scale == 1 fill the available icon
    // rectangle while preserving the model's aspect ratio.
    Fbox rotated_box;
    rotated_box.xform(vis_data.box, fit_transform);
    Fvector rotated_size;
    Fvector rotated_center;
    rotated_box.getsize(rotated_size);
    rotated_box.getcenter(rotated_center);

    float scale;
    if (rotated_size.x > EPS_S && rotated_size.y > EPS_S)
        scale = _min(available_width / rotated_size.x, available_height / rotated_size.y) * m_scale;
    else
        scale = _min(available_width, available_height) / _max(vis_data.sphere.R * 2.f, EPS_S) * m_scale;

    // Numerical and asymmetric-box offsets are removed after rotation so the
    // fitted projected bounds are centered in the UI rectangle.
    fit_transform.mulA_43(Fmatrix().translate(-rotated_center.x, -rotated_center.y, -rotated_center.z));
    float right_item_offset, up_item_offset;

    FromTargetToItem((target_rect.left + target_rect.right) * 0.5f,
        (target_rect.top + target_rect.bottom) * 0.5f, target_width, target_height, target_aspect,
        right_item_offset, up_item_offset);

    Fmatrix camera_matrix;
    camera_matrix.identity();
    camera_matrix = Device.mView;
    camera_matrix.invert();

    Fmatrix matrix = fit_transform;
    matrix.mulA_43(Fmatrix().scale(scale, scale, scale));
    matrix.mulA_43(Fmatrix().translate(right_item_offset, up_item_offset, dist));
    matrix.mulA_43(camera_matrix);

    ::Render->set_Transform(&matrix);
    ::Render->add_Visual(visual);

    return matrix;
}

void CUI3dStatic::DrawAttachmentIcon(script_attachment* att, const Fmatrix& matrix, IKinematics* parent)
{
    Fmatrix trans;
    trans = matrix;
    if (parent)
        trans.mulB_43(att->BoneTransform(parent));
    trans.mulB_43(att->GetOffset());

    ::Render->set_Transform(&trans);
    ::Render->add_Visual(att->renderable.visual);

    for (auto& pair : *att->GetAttachments())
    {
        script_attachment* cat = pair.second;
        if (cat->GetType() != eSA_World) continue;

        DrawAttachmentIcon(cat, trans, att->renderable.visual->dcast_PKinematics());
    }
}

void CUI3dStatic::SetObject(CScriptGameObject* obj)
{
    if (m_pFakeVisual)
    {
        ::Render->model_Delete(m_pFakeVisual);
        m_pFakeVisual = nullptr;
    }

    if (!obj)
    {
        m_pCurrentItem = nullptr;
        m_currentItemId = u16(-1);
        return;
    }

    m_pCurrentItem = &obj->object();
    m_currentItemId = m_pCurrentItem->ID();
    m_bFirstUpdate = true;
}

void CUI3dStatic::SetVisual(LPCSTR name)
{
    if (m_pCurrentItem)
    {
        m_pCurrentItem = nullptr;
        m_currentItemId = u16(-1);
    }

    if (m_pFakeVisual)
    {
        ::Render->model_Delete(m_pFakeVisual);
        m_pFakeVisual = nullptr;
    }

    if (!name || !xr_strlen(name))
        return;

    m_pFakeVisual = ::Render->model_Create(name);
    R_ASSERT(m_pFakeVisual);

    if (IKinematics* kinematics = m_pFakeVisual->dcast_PKinematics())
        kinematics->CalculateBones(TRUE);

    m_bFirstUpdate = true;
}
