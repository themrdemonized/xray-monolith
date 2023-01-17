IC void CPHElement::InverceLocalForm(Fmatrix& m)
{
	m.identity();
	m.c.set(m_mass_center);
	m.invert();
}

IC void CPHElement::MulB43InverceLocalForm(Fmatrix& m) const
{
	Fvector ic;
	ic.set(m_mass_center);
	ic.invert();
	m.transform_dir(ic);
	m.c.add(ic);
}


IC void CPHElement::CalculateBoneTransform(Fmatrix& bone_transform) const
{
	Fmatrix parent;
	parent.invert(m_shell->mXFORM);
	bone_transform.mul_43(parent, mXFORM);
}
