-- stamps the distort mask neutral over the composited lens
function normal(shader, t_base, t_second, t_detail)
	shader:begin("scope_vertex", "svp_distort_stamp")
	: zb(true, false)
	: scopelense(2)
end
