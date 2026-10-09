function normal(shader, t_base, t_second, t_detail)
    shader:begin("models_reflex_reticle", "models_reflex_lens")
        :fog(false)
        :zb(true, false)
        :blend(true, blend.srcalpha, blend.invsrcalpha)
        :aref(true, 0)
        :sorting(2, true)
    shader:sampler("s_base"):texture(t_base):clamp():f_bilinear()
end
