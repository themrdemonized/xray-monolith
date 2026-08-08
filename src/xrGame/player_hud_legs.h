#pragma once

class CActor;
class IKinematicsAnimated;
class IKinematics;
struct IDSGraphManager;

class player_legs_controller
{
public:
    void    update(CActor* actor, bool isShadowPass = false);
    void    render();
    void    destroy();

    bool    is_active() const { return m_model != nullptr; }
    IKinematics* model() const { return m_model; }

private:
    IKinematics* m_model = nullptr;
    shared_str              m_visual_name;
    shared_str              m_last_outfit_sect;
    shared_str              m_last_model;
    std::optional<float> m_fwd_offset = std::nullopt;
    float                   m_y_offset = 0.f;
    Fmatrix                 m_legs_transform;
    bool                    m_first_frame = true;
    xr_vector<Fmatrix>      m_prev_bones;
    bool                    m_has_prev = false;
    float                   m_blend_speed = 8.f;
    bool                    m_logged = false;
    bool is_costume_affected_bone(LPCSTR bone_name) const;
    bool should_hide_bone(LPCSTR bone_name) const;
    bool is_spine_bone(LPCSTR bone_name) const;
    void remove_camera_pitch(Fmatrix& bone_transform);

    bool    resolve_config(CActor* actor, shared_str& sect, shared_str& model);
    bool    ensure_model(const shared_str& sect, const shared_str& model);
    void    copy_bones_from_actor(CActor* actor, bool isShadowPass = false);
    void    warn_once(const char* fmt, ...);
    bool is_keep_bind_bone(LPCSTR bone_name) const;
};
