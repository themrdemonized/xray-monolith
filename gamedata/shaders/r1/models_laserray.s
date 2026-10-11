function normal(shader, t_base, t_second, t_detail)
    -- BaS also uses this material for permanent luminous iron-sight marks.
    shader:begin("model_def_lplanes", "base_laser")
        :fog(false):zb(true, false):blend(true, blend.srcalpha, blend.one)
        :aref(true, 0):sorting(2, true)
    shader:sampler("s_base"):texture(t_base)
end
