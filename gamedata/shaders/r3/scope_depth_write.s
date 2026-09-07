function normal(shader, t_base, t_second, t_detail)
    shader:begin("scope_vertex","scope_depth_write")
	: zb(false, true)
	: scopelense(2)
end
