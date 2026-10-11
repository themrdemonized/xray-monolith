function normal(shader, t_base, t_second, t_detail)
    local name = "models_laserbeam"
    local texture = string.lower(t_base)
    if string.find(texture, "laserdot") or string.find(texture, "laser_dot") or string.find(texture, "dot_reticle") then
        name = "models_laserbeam_dot"
    end
    shader:begin(name, name)
        :fog(false):zb(true, false):blend(true, blend.srcalpha, blend.one)
        :aref(true, 0):sorting(2, true)
    shader:sampler("s_base"):texture(t_base)
end
