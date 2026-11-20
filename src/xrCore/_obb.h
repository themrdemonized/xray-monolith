#ifndef FOBB_H
#define FOBB_H

template <class T>
struct _obb
{
public:
	typedef _obb<T> Self;
	typedef Self& SelfRef;
	typedef const Self& SelfCRef;
	typedef _vector3<T> Tvector;
	typedef _matrix<T> Tmatrix;
protected:
	static bool clip(T fDenom, T fNumer, T& rfT0, T& rfT1)
	{
		// Return value is 'true' if line segment intersects the current test
		// plane. Otherwise 'false' is returned in which case the line segment
		// is entirely clipped.

		if (fDenom > 0.0f)
		{
			if (fNumer > fDenom * rfT1) return false;
			if (fNumer > fDenom * rfT0) rfT0 = fNumer / fDenom;
			return true;
		}
		else if (fDenom < 0.0f)
		{
			if (fNumer > fDenom * rfT0) return false;
			if (fNumer > fDenom * rfT1) rfT1 = fNumer / fDenom;
			return true;
		}
		else
		{
			return fNumer <= 0.0f;
		}
	}

	static bool intersect(const Tvector& start, const Tvector& dir, const Tvector& extent, T& rfT0, T& rfT1)
	{
		T fSaveT0 = rfT0, fSaveT1 = rfT1;

		bool bNotEntirelyClipped =
			clip(+dir.x, -start.x - extent[0], rfT0, rfT1) &&
			clip(-dir.x, +start.x - extent[0], rfT0, rfT1) &&
			clip(+dir.y, -start.y - extent[1], rfT0, rfT1) &&
			clip(-dir.y, +start.y - extent[1], rfT0, rfT1) &&
			clip(+dir.z, -start.z - extent[2], rfT0, rfT1) &&
			clip(-dir.z, +start.z - extent[2], rfT0, rfT1);

		return bNotEntirelyClipped && (rfT0 != fSaveT0 || rfT1 != fSaveT1);
	}

public:
	_matrix33<T> m_rotate;
	Tvector m_translate;
	Tvector m_halfsize;

	IC SelfRef invalidate()
	{
		m_rotate.identity();
		m_translate.set(0, 0, 0);
		m_halfsize.set(0, 0, 0);
		return *this;
	}

	IC SelfRef identity()
	{
		invalidate();
		m_halfsize.set(T(0.5), T(0.5), T(0.5));
		return *this;
	}

	IC void xform_get(Tmatrix& D) const
	{
		D.i.set(m_rotate.i);
		D._14_ = 0;
		D.j.set(m_rotate.j);
		D._24_ = 0;
		D.k.set(m_rotate.k);
		D._34_ = 0;
		D.c.set(m_translate);
		D._44_ = 1;
	}

	IC SelfRef xform_set(const Tmatrix& S)
	{
		m_rotate.i.set(S.i);
		m_rotate.j.set(S.j);
		m_rotate.k.set(S.k);
		m_translate.set(S.c);
		return *this;
	}

	IC void xform_full(Tmatrix& D) const
	{
		Tmatrix R, S;
		xform_get(R);
		S.scale(m_halfsize);
		D.mul_43(R, S);
	}

	// NOTE: Unoptimized
	IC SelfRef transform(SelfCRef src, const Tmatrix& M)
	{
		Tmatrix srcR, destR;

		src.xform_get(srcR);
		destR.mul_43(M, srcR);
		xform_set(destR);
		m_halfsize.set(src.m_halfsize);
		return *this;
	}

	IC bool intersect(const Tvector& start, const Tvector& dir, T& dist) const
	{
		// convert ray to box coordinates
		Tvector kDiff;
		kDiff.sub(start, m_translate);
		Tvector kOrigin;
		kOrigin.set(kDiff.dotproduct(m_rotate.i), kDiff.dotproduct(m_rotate.j), kDiff.dotproduct(m_rotate.k));
		Tvector kDirection;
		kDirection.set(dir.dotproduct(m_rotate.i), dir.dotproduct(m_rotate.j), dir.dotproduct(m_rotate.k));

		T fT0 = 0.0f, fT1 = type_max(T);
		if (intersect(kOrigin, kDirection, m_halfsize, fT0, fT1))
		{
			bool bPick = false;
			if (fT0 > 0.0f)
			{
				if (fT0 < dist)
				{
					dist = fT0;
					bPick = true;
				}
				if (fT1 < dist)
				{
					dist = fT1;
					bPick = true;
				}
			}
			else
			{
				if (fT1 < dist)
				{
					dist = fT1;
					bPick = true;
				}
			}
			return bPick;
		}
		return false;
	}

    IC bool intersect(SelfCRef A, SelfCRef B) const
    {
        float RA = _sqrt(A.m_halfsize.x * A.m_halfsize.x + A.m_halfsize.y * A.m_halfsize.y + A.m_halfsize.z * A.m_halfsize.z);
        float RB = _sqrt(B.m_halfsize.x * B.m_halfsize.x + B.m_halfsize.y * B.m_halfsize.y + B.m_halfsize.z * B.m_halfsize.z);

        // Early out if too far apart to collide
        Fvector d; d.sub(B.m_translate, A.m_translate);
        float r = RA + RB;
        if (d.square_magnitude() > r * r)
            return false;

        const Tvector Aaxis[3] = {A.m_rotate.i, A.m_rotate.j, A.m_rotate.k};
        const Tvector Baxis[3] = { B.m_rotate.i, B.m_rotate.j, B.m_rotate.k };

        // Half-sizes
        const T a[3] = { A.m_halfsize[0], A.m_halfsize[1], A.m_halfsize[2] };
        const T b[3] = { B.m_halfsize[0], B.m_halfsize[1], B.m_halfsize[2] };

        // Rotation matrix R = A^T * B  (R[i][j] = dot(Ai, Bj))
        T R[3][3], AbsR[3][3];

        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                R[i][j] = Aaxis[i].dotproduct(Baxis[j]);
            }
        }

        Tvector tVec;
        tVec.sub(B.m_translate, A.m_translate);
        T t[3] = {
            tVec.dotproduct(Aaxis[0]),
            tVec.dotproduct(Aaxis[1]),
            tVec.dotproduct(Aaxis[2])
        };

        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                AbsR[i][j] = std::fabs(R[i][j]) + EPS_S;

        T ra, rb;

        for (int i = 0; i < 3; ++i)
        {
            ra = a[i];
            rb = b[0] * AbsR[i][0] + b[1] * AbsR[i][1] + b[2] * AbsR[i][2];
            if (std::fabs(t[i]) > ra + rb) return false;
        }

        for (int j = 0; j < 3; ++j)
        {
            ra = a[0] * AbsR[0][j] + a[1] * AbsR[1][j] + a[2] * AbsR[2][j];
            rb = b[j];
            const T tj = std::fabs(t[0] * R[0][j] + t[1] * R[1][j] + t[2] * R[2][j]);
            if (tj > ra + rb) return false;
        }

        // A0 x B0
        ra = a[1] * AbsR[2][0] + a[2] * AbsR[1][0];
        rb = b[1] * AbsR[0][2] + b[2] * AbsR[0][1];
        if (std::fabs(t[2] * R[1][0] - t[1] * R[2][0]) > ra + rb) return false;

        // A0 x B1
        ra = a[1] * AbsR[2][1] + a[2] * AbsR[1][1];
        rb = b[0] * AbsR[0][2] + b[2] * AbsR[0][0];
        if (std::fabs(t[2] * R[1][1] - t[1] * R[2][1]) > ra + rb) return false;

        // A0 x B2
        ra = a[1] * AbsR[2][2] + a[2] * AbsR[1][2];
        rb = b[0] * AbsR[0][1] + b[1] * AbsR[0][0];
        if (std::fabs(t[2] * R[1][2] - t[1] * R[2][2]) > ra + rb) return false;

        // A1 x B0
        ra = a[0] * AbsR[2][0] + a[2] * AbsR[0][0];
        rb = b[1] * AbsR[1][2] + b[2] * AbsR[1][1];
        if (std::fabs(t[0] * R[2][0] - t[2] * R[0][0]) > ra + rb) return false;

        // A1 x B1
        ra = a[0] * AbsR[2][1] + a[2] * AbsR[0][1];
        rb = b[0] * AbsR[1][2] + b[2] * AbsR[1][0];
        if (std::fabs(t[0] * R[2][1] - t[2] * R[0][1]) > ra + rb) return false;

        // A1 x B2
        ra = a[0] * AbsR[2][2] + a[2] * AbsR[0][2];
        rb = b[0] * AbsR[1][1] + b[1] * AbsR[1][0];
        if (std::fabs(t[0] * R[2][2] - t[2] * R[0][2]) > ra + rb) return false;

        // A2 x B0
        ra = a[0] * AbsR[1][0] + a[1] * AbsR[0][0];
        rb = b[1] * AbsR[2][2] + b[2] * AbsR[2][1];
        if (std::fabs(t[1] * R[0][0] - t[0] * R[1][0]) > ra + rb) return false;

        // A2 x B1
        ra = a[0] * AbsR[1][1] + a[1] * AbsR[0][1];
        rb = b[0] * AbsR[2][2] + b[2] * AbsR[2][0];
        if (std::fabs(t[1] * R[0][1] - t[0] * R[1][1]) > ra + rb) return false;

        // A2 x B2
        ra = a[0] * AbsR[1][2] + a[1] * AbsR[0][2];
        rb = b[0] * AbsR[2][1] + b[1] * AbsR[2][0];
        if (std::fabs(t[1] * R[0][2] - t[0] * R[1][2]) > ra + rb) return false;

        return true;
    }

    IC bool intersect(SelfCRef B) const
    {
        return intersect(*this, B);
    }
};

typedef _obb<float> Fobb;
typedef _obb<double> Dobb;

template <class T>
BOOL _valid(const _obb<T>& m)
{
	return _valid(m_rotate) && _valid(m_translate) && _valid(m_halfsize);
}

#endif
