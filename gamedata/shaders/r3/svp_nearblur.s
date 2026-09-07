function normal(shader, t_base, t_second, t_detail)
	shader:begin("stub_notransform_t", "svp_nearblur")
	: zb(false, false)
	shader:dx10texture("s_nb_image", "$user$svp_nearblur_src")
	shader:dx10texture("s_nb_pos", "$user$svp_nearblur_pos")
	shader:dx10sampler("smp_base")
	shader:dx10sampler("smp_nofilter")
end
