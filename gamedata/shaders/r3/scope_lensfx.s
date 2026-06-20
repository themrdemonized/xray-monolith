function normal(shader, t_base, t_second, t_detail)
	shader:begin("scope_vertex", "scope_lensfx")
	: zb(false, false)
	shader:dx10texture("s_lensfx_src", "$user$pip_lensfx_src")
	shader:dx10sampler("smp_rtlinear")
end
