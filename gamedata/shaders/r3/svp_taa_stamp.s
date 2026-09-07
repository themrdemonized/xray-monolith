-- stamps taa mask alpha 1 over the composited lens so the main resolve skips it
function normal(shader, t_base, t_second, t_detail)
	shader:begin("scope_vertex", "svp_taa_stamp")
	: zb(false, false)
	: scopelense(2)
end
