function normal(shader, t_base, t_second, t_detail)
    shader:begin("null", "scope_hud_sun")
        :fog(false):zb(false, false):blend(false, blend.one, blend.zero)
    shader:sampler("s_position"):texture("$user$position"):clamp():f_none()
    shader:sampler("s_normal"):texture("$user$normal"):clamp():f_none()
    shader:sampler("s_diffuse"):texture("$user$albedo"):clamp():f_none()
    shader:sampler("s_material"):texture("$user$material"):clamp():f_linear()
end
