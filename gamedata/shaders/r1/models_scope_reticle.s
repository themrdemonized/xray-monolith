local function compile(shader, t_base, pixel_shader)
    shader:begin("models_scope_reticle", pixel_shader)
        :fog(false)
        :zb(true, false)
        :blend(false, blend.one, blend.zero)
        :sorting(2, true)
        :scopelense(3)
    shader:sampler("s_base"):texture(t_base):clamp():f_bilinear()
    shader:sampler("s_scene"):texture("$user$3dss_scene"):clamp():f_bilinear()
end

function normal(shader, t_base, t_second, t_detail)
    compile(shader, t_base, "models_scope_reticle")
end

function scope_dual(shader, t_base, t_second, t_detail)
    compile(shader, t_base, "models_scope_reticle_dual")
end

function scope_giperon(shader, t_base, t_second, t_detail)
    compile(shader, t_base, "models_scope_reticle_giperon")
end
