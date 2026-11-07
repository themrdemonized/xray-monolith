#pragma once

class DBG_ScriptCylinder;
class DBG_ScriptSphere;
class DBG_ScriptBox;
class DBG_ScriptLine;

enum DebugRenderType
{
	eDBGLine = 0,
	eDBGSphere,
	eDBGBox,
    eDBGCylinder,
	eDBGUndef
};

class DBG_ScriptObject
{
public:
	Fcolor m_color;
	bool m_visible;
	bool m_hud;

	DBG_ScriptObject()
	{
		m_color.set(1, 0, 0, 1);
		m_visible = true;
		m_hud = false;
	}

	virtual ~DBG_ScriptObject() {}

    virtual DBG_ScriptObject* cast_dbg_obj() { return this; }
	virtual DBG_ScriptSphere* cast_dbg_sphere() { return nullptr; }
	virtual DBG_ScriptBox* cast_dbg_box() { return nullptr; }
	virtual DBG_ScriptLine* cast_dbg_line() { return nullptr; }
	virtual DBG_ScriptCylinder* cast_dbg_cyl() { return nullptr; }

	virtual void Render() {}
};

class DBG_ScriptSphere : public DBG_ScriptObject
{
public:
	Fmatrix m_mat;

	DBG_ScriptSphere()
	{
		m_mat = Fidentity;
	}

	virtual ~DBG_ScriptSphere() {}

	virtual DBG_ScriptSphere* cast_dbg_sphere() { return this; }

	virtual void Render();
};

class DBG_ScriptBox : public DBG_ScriptObject
{
public:
	Fmatrix m_mat;
	Fvector m_size;

	DBG_ScriptBox()
	{
		m_mat = Fidentity;
		m_size.set(1, 1, 1);
	}

	virtual ~DBG_ScriptBox() {}

	virtual DBG_ScriptBox* cast_dbg_box() { return this; }

	virtual void Render();
};

class DBG_ScriptLine : public DBG_ScriptObject
{
public:
	Fvector m_point_a, m_point_b;

	DBG_ScriptLine()
	{
		m_point_a.set(0, 0, 0);
		m_point_b.set(0, 0, 0);
	}

	virtual ~DBG_ScriptLine() {}

	virtual DBG_ScriptLine* cast_dbg_line() { return this; }

	virtual void Render();
};
class DBG_ScriptCylinder : public DBG_ScriptObject
{
public:
    Fmatrix m_mat;
    float m_length, m_radius;

    DBG_ScriptCylinder()
    {
        m_mat = Fidentity;
        m_length = 1.f;
        m_radius = 1.f;
    }

    virtual ~DBG_ScriptCylinder() {}

    virtual DBG_ScriptCylinder* cast_dbg_cyl() { return this; }

    virtual void Render();
};
