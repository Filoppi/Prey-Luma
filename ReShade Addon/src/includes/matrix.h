#pragma once

#include "math.h"

#define CRY_FORCE_INLINE __forceinline
#if !defined(DEBUG)
#define ILINE CRY_FORCE_INLINE
#else
#define ILINE inline
#endif
#define CRY_ALIGN(bytes) __declspec(align(bytes))

template<typename F> struct Matrix44_tpl
{
	F m00, m01, m02, m03;
	F m10, m11, m12, m13;
	F m20, m21, m22, m23;
	F m30, m31, m32, m33;

	ILINE Matrix44_tpl() {};

#if 0
	//! Initialize with zeros.
	ILINE Matrix44_tpl(type_zero)
	{
		m00 = 0;
		m01 = 0;
		m02 = 0;
		m03 = 0;
		m10 = 0;
		m11 = 0;
		m12 = 0;
		m13 = 0;
		m20 = 0;
		m21 = 0;
		m22 = 0;
		m23 = 0;
		m30 = 0;
		m31 = 0;
		m32 = 0;
		m33 = 0;
	}

	ILINE Matrix44_tpl(type_identity) { SetIdentity(); }
#endif

	//! ASSIGNMENT OPERATOR of identical Matrix44 types.
	//! The assignment operator has precedence over assignment constructor.
	//! Matrix44 m; m=m44;
	ILINE Matrix44_tpl<F>& operator=(const Matrix44_tpl<F>& m)
	{
		m00 = m.m00;
		m01 = m.m01;
		m02 = m.m02;
		m03 = m.m03;
		m10 = m.m10;
		m11 = m.m11;
		m12 = m.m12;
		m13 = m.m13;
		m20 = m.m20;
		m21 = m.m21;
		m22 = m.m22;
		m23 = m.m23;
		m30 = m.m30;
		m31 = m.m31;
		m32 = m.m32;
		m33 = m.m33;
		return *this;
	}

	// implementation of the constructors

	ILINE Matrix44_tpl<F>(F v00, F v01, F v02, F v03,
		F v10, F v11, F v12, F v13,
		F v20, F v21, F v22, F v23,
		F v30, F v31, F v32, F v33)
	{
		m00 = v00;
		m01 = v01;
		m02 = v02;
		m03 = v03;
		m10 = v10;
		m11 = v11;
		m12 = v12;
		m13 = v13;
		m20 = v20;
		m21 = v21;
		m22 = v22;
		m23 = v23;
		m30 = v30;
		m31 = v31;
		m32 = v32;
		m33 = v33;
	}

	//! CONSTRUCTOR for identical types.
	//! Matrix44 m=m44;
	ILINE Matrix44_tpl<F>(const Matrix44_tpl<F>& m)
	{
		m00 = m.m00;
		m01 = m.m01;
		m02 = m.m02;
		m03 = m.m03;
		m10 = m.m10;
		m11 = m.m11;
		m12 = m.m12;
		m13 = m.m13;
		m20 = m.m20;
		m21 = m.m21;
		m22 = m.m22;
		m23 = m.m23;
		m30 = m.m30;
		m31 = m.m31;
		m32 = m.m32;
		m33 = m.m33;
	}
	//! CONSTRUCTOR for identical types which converts between double/float.
	//! Matrix44 m=m44r;
	//! Matrix44r m=m44;
	template<class F1> ILINE Matrix44_tpl<F>(const Matrix44_tpl<F1>& m)
	{
		m00 = F(m.m00);
		m01 = F(m.m01);
		m02 = F(m.m02);
		m03 = F(m.m03);
		m10 = F(m.m10);
		m11 = F(m.m11);
		m12 = F(m.m12);
		m13 = F(m.m13);
		m20 = F(m.m20);
		m21 = F(m.m21);
		m22 = F(m.m22);
		m23 = F(m.m23);
		m30 = F(m.m30);
		m31 = F(m.m31);
		m32 = F(m.m32);
		m33 = F(m.m33);
	}

	//---------------------------------------------------------------------

	//! multiply all m1 matrix's values by f and return the matrix
	friend  ILINE Matrix44_tpl<F> operator*(const Matrix44_tpl<F>& m, const float f)
	{
		Matrix44_tpl<F> r;
		r.m00 = m.m00 * f;
		r.m01 = m.m01 * f;
		r.m02 = m.m02 * f;
		r.m03 = m.m03 * f;
		r.m10 = m.m10 * f;
		r.m11 = m.m11 * f;
		r.m12 = m.m12 * f;
		r.m13 = m.m13 * f;
		r.m20 = m.m20 * f;
		r.m21 = m.m21 * f;
		r.m22 = m.m22 * f;
		r.m23 = m.m23 * f;
		r.m30 = m.m30 * f;
		r.m31 = m.m31 * f;
		r.m32 = m.m32 * f;
		r.m33 = m.m33 * f;
		return r;
	}

	//! add all m matrix's values and return the matrix
	friend  ILINE Matrix44_tpl<F> operator+(const Matrix44_tpl<F>& mm0, const Matrix44_tpl<F>& mm1)
	{
		Matrix44_tpl<F> r;
		r.m00 = mm0.m00 + mm1.m00;
		r.m01 = mm0.m01 + mm1.m01;
		r.m02 = mm0.m02 + mm1.m02;
		r.m03 = mm0.m03 + mm1.m03;
		r.m10 = mm0.m10 + mm1.m10;
		r.m11 = mm0.m11 + mm1.m11;
		r.m12 = mm0.m12 + mm1.m12;
		r.m13 = mm0.m13 + mm1.m13;
		r.m20 = mm0.m20 + mm1.m20;
		r.m21 = mm0.m21 + mm1.m21;
		r.m22 = mm0.m22 + mm1.m22;
		r.m23 = mm0.m23 + mm1.m23;
		r.m30 = mm0.m30 + mm1.m30;
		r.m31 = mm0.m31 + mm1.m31;
		r.m32 = mm0.m32 + mm1.m32;
		r.m33 = mm0.m33 + mm1.m33;
		return r;
	}

	//! Implements the multiplication operator: Matrix44=Matrix44*Matrix44.
	//! Matrix44 and Matrix34 are specified in collumn order.
	//! AxB = rotation B followed by rotation A.
	//! This operation takes 48 mults and 36 adds.
	//! Example:
	//!   Matrix44 m44=CreateRotationX33(1.94192f);;
	//!   Matrix44 m44=CreateRotationZ33(3.14192f);
	//!   Matrix44 result=m44*m44;
	friend  ILINE Matrix44_tpl<F> operator*(const Matrix44_tpl<F>& l, const Matrix44_tpl<F>& r)
	{
		Matrix44_tpl<F> res;
		res.m00 = l.m00 * r.m00 + l.m01 * r.m10 + l.m02 * r.m20 + l.m03 * r.m30;
		res.m10 = l.m10 * r.m00 + l.m11 * r.m10 + l.m12 * r.m20 + l.m13 * r.m30;
		res.m20 = l.m20 * r.m00 + l.m21 * r.m10 + l.m22 * r.m20 + l.m23 * r.m30;
		res.m30 = l.m30 * r.m00 + l.m31 * r.m10 + l.m32 * r.m20 + l.m33 * r.m30;
		res.m01 = l.m00 * r.m01 + l.m01 * r.m11 + l.m02 * r.m21 + l.m03 * r.m31;
		res.m11 = l.m10 * r.m01 + l.m11 * r.m11 + l.m12 * r.m21 + l.m13 * r.m31;
		res.m21 = l.m20 * r.m01 + l.m21 * r.m11 + l.m22 * r.m21 + l.m23 * r.m31;
		res.m31 = l.m30 * r.m01 + l.m31 * r.m11 + l.m32 * r.m21 + l.m33 * r.m31;
		res.m02 = l.m00 * r.m02 + l.m01 * r.m12 + l.m02 * r.m22 + l.m03 * r.m32;
		res.m12 = l.m10 * r.m02 + l.m11 * r.m12 + l.m12 * r.m22 + l.m13 * r.m32;
		res.m22 = l.m20 * r.m02 + l.m21 * r.m12 + l.m22 * r.m22 + l.m23 * r.m32;
		res.m32 = l.m30 * r.m02 + l.m31 * r.m12 + l.m32 * r.m22 + l.m33 * r.m32;
		res.m03 = l.m00 * r.m03 + l.m01 * r.m13 + l.m02 * r.m23 + l.m03 * r.m33;
		res.m13 = l.m10 * r.m03 + l.m11 * r.m13 + l.m12 * r.m23 + l.m13 * r.m33;
		res.m23 = l.m20 * r.m03 + l.m21 * r.m13 + l.m22 * r.m23 + l.m23 * r.m33;
		res.m33 = l.m30 * r.m03 + l.m31 * r.m13 + l.m32 * r.m23 + l.m33 * r.m33;
		return res;
	}

	ILINE void SetIdentity()
	{
		m00 = 1;
		m01 = 0;
		m02 = 0;
		m03 = 0;
		m10 = 0;
		m11 = 1;
		m12 = 0;
		m13 = 0;
		m20 = 0;
		m21 = 0;
		m22 = 1;
		m23 = 0;
		m30 = 0;
		m31 = 0;
		m32 = 0;
		m33 = 1;
	}

	ILINE void Transpose()
	{
		Matrix44_tpl<F> tmp = *this;
		m00 = tmp.m00;
		m01 = tmp.m10;
		m02 = tmp.m20;
		m03 = tmp.m30;
		m10 = tmp.m01;
		m11 = tmp.m11;
		m12 = tmp.m21;
		m13 = tmp.m31;
		m20 = tmp.m02;
		m21 = tmp.m12;
		m22 = tmp.m22;
		m23 = tmp.m32;
		m30 = tmp.m03;
		m31 = tmp.m13;
		m32 = tmp.m23;
		m33 = tmp.m33;
	}
	ILINE Matrix44_tpl<F> GetTransposed() const
	{
		Matrix44_tpl<F> tmp;
		tmp.m00 = m00;
		tmp.m01 = m10;
		tmp.m02 = m20;
		tmp.m03 = m30;
		tmp.m10 = m01;
		tmp.m11 = m11;
		tmp.m12 = m21;
		tmp.m13 = m31;
		tmp.m20 = m02;
		tmp.m21 = m12;
		tmp.m22 = m22;
		tmp.m23 = m32;
		tmp.m30 = m03;
		tmp.m31 = m13;
		tmp.m32 = m23;
		tmp.m33 = m33;
		return tmp;
	}

	//! Calculate a real inversion of a Matrix44.
	//! Uses Cramer's Rule which is faster (branchless) but numerically less stable than other methods like Gaussian Elimination.
	//! Example 1:
	//!   Matrix44 im44; im44.Invert();
	//! Example 2:
	//!   Matrix44 im44 = m33.GetInverted();
	void Invert(void)
	{
		F tmp[12];
		Matrix44_tpl<F> m = *this;

		// Calculate pairs for first 8 elements (cofactors)
		tmp[0] = m.m22 * m.m33;
		tmp[1] = m.m32 * m.m23;
		tmp[2] = m.m12 * m.m33;
		tmp[3] = m.m32 * m.m13;
		tmp[4] = m.m12 * m.m23;
		tmp[5] = m.m22 * m.m13;
		tmp[6] = m.m02 * m.m33;
		tmp[7] = m.m32 * m.m03;
		tmp[8] = m.m02 * m.m23;
		tmp[9] = m.m22 * m.m03;
		tmp[10] = m.m02 * m.m13;
		tmp[11] = m.m12 * m.m03;

		// Calculate first 8 elements (cofactors)
		m00 = tmp[0] * m.m11 + tmp[3] * m.m21 + tmp[4] * m.m31;
		m00 -= tmp[1] * m.m11 + tmp[2] * m.m21 + tmp[5] * m.m31;
		m01 = tmp[1] * m.m01 + tmp[6] * m.m21 + tmp[9] * m.m31;
		m01 -= tmp[0] * m.m01 + tmp[7] * m.m21 + tmp[8] * m.m31;
		m02 = tmp[2] * m.m01 + tmp[7] * m.m11 + tmp[10] * m.m31;
		m02 -= tmp[3] * m.m01 + tmp[6] * m.m11 + tmp[11] * m.m31;
		m03 = tmp[5] * m.m01 + tmp[8] * m.m11 + tmp[11] * m.m21;
		m03 -= tmp[4] * m.m01 + tmp[9] * m.m11 + tmp[10] * m.m21;
		m10 = tmp[1] * m.m10 + tmp[2] * m.m20 + tmp[5] * m.m30;
		m10 -= tmp[0] * m.m10 + tmp[3] * m.m20 + tmp[4] * m.m30;
		m11 = tmp[0] * m.m00 + tmp[7] * m.m20 + tmp[8] * m.m30;
		m11 -= tmp[1] * m.m00 + tmp[6] * m.m20 + tmp[9] * m.m30;
		m12 = tmp[3] * m.m00 + tmp[6] * m.m10 + tmp[11] * m.m30;
		m12 -= tmp[2] * m.m00 + tmp[7] * m.m10 + tmp[10] * m.m30;
		m13 = tmp[4] * m.m00 + tmp[9] * m.m10 + tmp[10] * m.m20;
		m13 -= tmp[5] * m.m00 + tmp[8] * m.m10 + tmp[11] * m.m20;

		// Calculate pairs for second 8 elements (cofactors)
		tmp[0] = m.m20 * m.m31;
		tmp[1] = m.m30 * m.m21;
		tmp[2] = m.m10 * m.m31;
		tmp[3] = m.m30 * m.m11;
		tmp[4] = m.m10 * m.m21;
		tmp[5] = m.m20 * m.m11;
		tmp[6] = m.m00 * m.m31;
		tmp[7] = m.m30 * m.m01;
		tmp[8] = m.m00 * m.m21;
		tmp[9] = m.m20 * m.m01;
		tmp[10] = m.m00 * m.m11;
		tmp[11] = m.m10 * m.m01;

		// Calculate second 8 elements (cofactors)
		m20 = tmp[0] * m.m13 + tmp[3] * m.m23 + tmp[4] * m.m33;
		m20 -= tmp[1] * m.m13 + tmp[2] * m.m23 + tmp[5] * m.m33;
		m21 = tmp[1] * m.m03 + tmp[6] * m.m23 + tmp[9] * m.m33;
		m21 -= tmp[0] * m.m03 + tmp[7] * m.m23 + tmp[8] * m.m33;
		m22 = tmp[2] * m.m03 + tmp[7] * m.m13 + tmp[10] * m.m33;
		m22 -= tmp[3] * m.m03 + tmp[6] * m.m13 + tmp[11] * m.m33;
		m23 = tmp[5] * m.m03 + tmp[8] * m.m13 + tmp[11] * m.m23;
		m23 -= tmp[4] * m.m03 + tmp[9] * m.m13 + tmp[10] * m.m23;
		m30 = tmp[2] * m.m22 + tmp[5] * m.m32 + tmp[1] * m.m12;
		m30 -= tmp[4] * m.m32 + tmp[0] * m.m12 + tmp[3] * m.m22;
		m31 = tmp[8] * m.m32 + tmp[0] * m.m02 + tmp[7] * m.m22;
		m31 -= tmp[6] * m.m22 + tmp[9] * m.m32 + tmp[1] * m.m02;
		m32 = tmp[6] * m.m12 + tmp[11] * m.m32 + tmp[3] * m.m02;
		m32 -= tmp[10] * m.m32 + tmp[2] * m.m02 + tmp[7] * m.m12;
		m33 = tmp[10] * m.m22 + tmp[4] * m.m02 + tmp[9] * m.m12;
		m33 -= tmp[8] * m.m12 + tmp[11] * m.m22 + tmp[5] * m.m02;

		// Calculate determinant
		F det = (m.m00 * m00 + m.m10 * m01 + m.m20 * m02 + m.m30 * m03);

		// Divide the cofactor-matrix by the determinant
		F idet = (F)1.0 / det;
		m00 *= idet;
		m01 *= idet;
		m02 *= idet;
		m03 *= idet;
		m10 *= idet;
		m11 *= idet;
		m12 *= idet;
		m13 *= idet;
		m20 *= idet;
		m21 *= idet;
		m22 *= idet;
		m23 *= idet;
		m30 *= idet;
		m31 *= idet;
		m32 *= idet;
		m33 *= idet;
	}

	ILINE Matrix44_tpl<F> GetInverted() const
	{
		Matrix44_tpl<F> dst = *this;
		dst.Invert();
		return dst;
	}

	ILINE float Determinant() const
	{
		//determinant is ambiguous: only the upper-left-submatrix's determinant is calculated
		return (m00 * m11 * m22) + (m01 * m12 * m20) + (m02 * m10 * m21) - (m02 * m11 * m20) - (m00 * m12 * m21) - (m01 * m10 * m22);
	}

#if 0
	//! Transform a vector.
	ILINE Vec3 TransformVector(const Vec3& b) const
	{
		Vec3 v;
		v.x = m00 * b.x + m01 * b.y + m02 * b.z;
		v.y = m10 * b.x + m11 * b.y + m12 * b.z;
		v.z = m20 * b.x + m21 * b.y + m22 * b.z;
		return v;
	}
	//! Transform a point.
	ILINE Vec3 TransformPoint(const Vec3& b) const
	{
		Vec3 v;
		v.x = m00 * b.x + m01 * b.y + m02 * b.z + m03;
		v.y = m10 * b.x + m11 * b.y + m12 * b.z + m13;
		v.z = m20 * b.x + m21 * b.y + m22 * b.z + m23;
		return v;
	}
#endif

	// helper functions to access matrix-members
	ILINE F* GetData() { return &m00; }
	ILINE const F* GetData() const { return &m00; }

	ILINE F  operator()(uint32_t i, uint32_t j) const { F* p_data = (F*)(&m00); return p_data[i * 4 + j]; }
	ILINE F& operator()(uint32_t i, uint32_t j) { F* p_data = (F*)(&m00); return p_data[i * 4 + j]; }

#if 0
	ILINE void               SetRow(int i, const Vec3_tpl<F>& v) { CRY_MATH_ASSERT(i < 4); F* p = (F*)(&m00); p[0 + 4 * i] = v.x; p[1 + 4 * i] = v.y; p[2 + 4 * i] = v.z; }
	ILINE void               SetRow4(int i, const Vec4_tpl<F>& v) { CRY_MATH_ASSERT(i < 4); F* p = (F*)(&m00); p[0 + 4 * i] = v.x; p[1 + 4 * i] = v.y; p[2 + 4 * i] = v.z; p[3 + 4 * i] = v.w; }
	ILINE const Vec3_tpl<F>& GetRow(int i) const { CRY_MATH_ASSERT(i < 4); return *(const Vec3_tpl<F>*)(&m00 + 4 * i); }

	ILINE void               SetColumn(int i, const Vec3_tpl<F>& v) { CRY_MATH_ASSERT(i < 4); F* p = (F*)(&m00); p[i + 4 * 0] = v.x; p[i + 4 * 1] = v.y; p[i + 4 * 2] = v.z; }
	ILINE Vec3_tpl<F>        GetColumn(int i) const { CRY_MATH_ASSERT(i < 4); F* p = (F*)(&m00); return Vec3(p[i + 4 * 0], p[i + 4 * 1], p[i + 4 * 2]); }
	ILINE Vec4_tpl<F>        GetColumn4(int i) const { CRY_MATH_ASSERT(i < 4); F* p = (F*)(&m00); return Vec4(p[i + 4 * 0], p[i + 4 * 1], p[i + 4 * 2], p[i + 4 * 3]); }

	ILINE Vec3               GetTranslation() const { return Vec3(m03, m13, m23); }
	ILINE void               SetTranslation(const Vec3& t) { m03 = t.x; m13 = t.y; m23 = t.z; }
#endif
};

typedef CRY_ALIGN(16) Matrix44_tpl<float> Matrix44A;
typedef Matrix44_tpl<float>  Matrix44;   //!< Always 32 bit.

template<class T_out, class T_in>
inline void mathMatrixLookAtInverse(Matrix44_tpl<T_out>& pResult, const Matrix44_tpl<T_in>& pLookAt)
{
	pResult(0, 0) = pLookAt.m00;
	pResult(0, 1) = pLookAt.m10;
	pResult(0, 2) = pLookAt.m20;
	pResult(0, 3) = pLookAt.m03;
	pResult(1, 0) = pLookAt.m01;
	pResult(1, 1) = pLookAt.m11;
	pResult(1, 2) = pLookAt.m21;
	pResult(1, 3) = pLookAt.m13;
	pResult(2, 0) = pLookAt.m02;
	pResult(2, 1) = pLookAt.m12;
	pResult(2, 2) = pLookAt.m22;
	pResult(2, 3) = pLookAt.m23;

	pResult(3, 0) = T_out(-(double(pLookAt.m00) * double(pLookAt.m30) + double(pLookAt.m01) * double(pLookAt.m31) + double(pLookAt.m02) * double(pLookAt.m32)));
	pResult(3, 1) = T_out(-(double(pLookAt.m10) * double(pLookAt.m30) + double(pLookAt.m11) * double(pLookAt.m31) + double(pLookAt.m12) * double(pLookAt.m32)));
	pResult(3, 2) = T_out(-(double(pLookAt.m20) * double(pLookAt.m30) + double(pLookAt.m21) * double(pLookAt.m31) + double(pLookAt.m22) * double(pLookAt.m32)));
	pResult(3, 3) = pLookAt.m33;
};

template<class T>
inline bool mathMatrixIsProjection(const Matrix44_tpl<T>& m)
{
	// (0, 0) and (1, 1) are relative to FOV and theoretically could be zero if FOV was zero (probably never used by the game)
	// (2, 0) and (2, 1) are jitters so they can either be 0 or ~0.00001 (tiny values as they are in UV space)
	return m(0, 1) == 0
		&& m(0, 2) == 0
		&& m(0, 3) == 0
		&& m(1, 0) == 0
		&& m(1, 2) == 0
		&& m(1, 3) == 0
		&& m(3, 0) == 0
#if 0
		&& abs(m(2, 0)) <= 0.5
		&& abs(m(2, 1)) <= 0.5
#endif
		&& m(2, 2) != 0 // Depth related
		&& abs(m(2, 3)) == 1
		&& m(3, 2) != 0 // Depth related
		&& m(3, 3) == 0;
}

template<class T>
inline bool mathMatrixPerspectiveFovInverse(Matrix44_tpl<T>& pResult, const Matrix44_tpl<T>& pProjFov, bool bForce = true)
{
	if (bForce || mathMatrixIsProjection(pProjFov))
	{
		pResult(0, 0) = 1.0 / pProjFov.m00;
		pResult(0, 1) = 0;
		pResult(0, 2) = 0;
		pResult(0, 3) = 0;
		pResult(1, 0) = 0;
		pResult(1, 1) = 1.0 / pProjFov.m11;
		pResult(1, 2) = 0;
		pResult(1, 3) = 0;
		pResult(2, 0) = 0;
		pResult(2, 1) = 0;
		pResult(2, 2) = 0;
		pResult(2, 3) = 1.0 / pProjFov.m32;
		pResult(3, 0) = pProjFov.m20 / pProjFov.m00;
		pResult(3, 1) = pProjFov.m21 / pProjFov.m11;
		pResult(3, 2) = -1;
		pResult(3, 3) = pProjFov.m22 / pProjFov.m32;

		return true;
	}

	return false;
}

template<class TA, class TB>
inline bool mathMatrixAlmostEqual(const Matrix44_tpl<TA>& M1, const Matrix44_tpl<TB>& M2, TA fTolerance)
{
	return AlmostEqual(M1(0, 0), (TA)M2(0, 0), fTolerance)
		&& AlmostEqual(M1(0, 1), (TA)M2(0, 1), fTolerance)
		&& AlmostEqual(M1(0, 2), (TA)M2(0, 2), fTolerance)
		&& AlmostEqual(M1(0, 3), (TA)M2(0, 3), fTolerance)
		&& AlmostEqual(M1(1, 0), (TA)M2(1, 0), fTolerance)
		&& AlmostEqual(M1(1, 1), (TA)M2(1, 1), fTolerance)
		&& AlmostEqual(M1(1, 2), (TA)M2(1, 2), fTolerance)
		&& AlmostEqual(M1(1, 3), (TA)M2(1, 3), fTolerance)
		&& AlmostEqual(M1(2, 0), (TA)M2(2, 0), fTolerance)
		&& AlmostEqual(M1(2, 1), (TA)M2(2, 1), fTolerance)
		&& AlmostEqual(M1(2, 2), (TA)M2(2, 2), fTolerance)
		&& AlmostEqual(M1(2, 3), (TA)M2(2, 3), fTolerance)
		&& AlmostEqual(M1(3, 0), (TA)M2(3, 0), fTolerance)
		&& AlmostEqual(M1(3, 1), (TA)M2(3, 1), fTolerance)
		&& AlmostEqual(M1(3, 2), (TA)M2(3, 2), fTolerance)
		&& AlmostEqual(M1(3, 3), (TA)M2(3, 3), fTolerance);
}

template<class T>
inline bool mathMatrixIsIdentity(const Matrix44_tpl<T>& m)
{
	return m(0, 0) == 1
		&& m(0, 1) == 0
		&& m(0, 2) == 0
		&& m(0, 3) == 0
		&& m(1, 0) == 0
		&& m(1, 1) == 1
		&& m(1, 2) == 0
		&& m(1, 3) == 0
		&& m(2, 0) == 0
		&& m(2, 1) == 0
		&& m(2, 2) == 1
		&& m(2, 3) == 0
		&& m(3, 0) == 0
		&& m(3, 1) == 0
		&& m(3, 2) == 0
		&& m(3, 3) == 1;
}