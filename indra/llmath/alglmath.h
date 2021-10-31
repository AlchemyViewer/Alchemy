#pragma once

#include "llmath.h"
#include "llrect.h"
#include "llvector4a.h"
#include "llmatrix4a.h"

namespace ALGLMath
{
	inline static const LLMatrix4a TRANS_MAT = LLMatrix4a(
		LLVector4a(.5f, 0, 0, 0),
		LLVector4a(0, .5f, 0, 0),
		LLVector4a(0, 0, .5f, 0),
		LLVector4a(.5f, .5f, .5f, 1.f));

	inline LLMatrix4a genRot(const float a, const LLVector4a& axis)
	{
		F32 r = a * DEG_TO_RAD;

		F32 c = cosf(r);
		F32 s = sinf(r);

		F32 ic = 1.f - c;

		const LLVector4a add1(c, axis[VZ] * s, -axis[VY] * s);	//1,z,-y
		const LLVector4a add2(-axis[VZ] * s, c, axis[VX] * s);	//-z,1,x
		const LLVector4a add3(axis[VY] * s, -axis[VX] * s, c);	//y,-x,1

		LLVector4a axis_x;
		axis_x.splat<0>(axis);
		LLVector4a axis_y;
		axis_y.splat<1>(axis);
		LLVector4a axis_z;
		axis_z.splat<2>(axis);

		LLVector4a c_axis;
		c_axis.setMul(axis, ic);

		LLMatrix4a rot_mat;
		rot_mat.getRow<0>().setMul(c_axis, axis_x);
		rot_mat.getRow<0>().add(add1);
		rot_mat.getRow<1>().setMul(c_axis, axis_y);
		rot_mat.getRow<1>().add(add2);
		rot_mat.getRow<2>().setMul(c_axis, axis_z);
		rot_mat.getRow<2>().add(add3);
		rot_mat.setRow<3>(LLVector4a(0, 0, 0, 1));

		return rot_mat;
	}

	inline LLMatrix4a genRot(const float a, const float x, const float y, const float z) { return genRot(a, LLVector4a(x, y, z)); }

	inline LLMatrix4a genOrtho(const GLfloat& left, const GLfloat& right, const GLfloat& bottom, const GLfloat& top, const GLfloat& zNear, const GLfloat& zFar)
	{
		LLMatrix4a ortho_mat;
		ortho_mat.setRow<0>(LLVector4a(2.f / (right - left), 0, 0));
		ortho_mat.setRow<1>(LLVector4a(0, 2.f / (top - bottom), 0));
		ortho_mat.setRow<2>(LLVector4a(0, 0, -2.f / (zFar - zNear)));
		ortho_mat.setRow<3>(LLVector4a(-(right + left) / (right - left), -(top + bottom) / (top - bottom), -(zFar + zNear) / (zFar - zNear), 1));

		return ortho_mat;
	}

	inline LLMatrix4a genPersp(const GLfloat& fovy, const GLfloat& aspect, const GLfloat& zNear, const GLfloat& zFar)
	{
		GLfloat f = 1.f / tanf(DEG_TO_RAD * fovy / 2.f);

		LLMatrix4a persp_mat;
		persp_mat.setRow<0>(LLVector4a(f / aspect, 0, 0));
		persp_mat.setRow<1>(LLVector4a(0, f, 0));
		persp_mat.setRow<2>(LLVector4a(0, 0, (zFar + zNear) / (zNear - zFar), -1.f));
		persp_mat.setRow<3>(LLVector4a(0, 0, (2.f * zFar * zNear) / (zNear - zFar), 0));

		return persp_mat;
	}

	inline LLMatrix4a genLook(const LLVector3& pos_in, const LLVector3& dir_in, const LLVector3& up_in)
	{
		const LLVector4a pos(pos_in.mV[VX], pos_in.mV[VY], pos_in.mV[VZ], 1.f);
		LLVector4a dir(dir_in.mV[VX], dir_in.mV[VY], dir_in.mV[VZ]);
		const LLVector4a up(up_in.mV[VX], up_in.mV[VY], up_in.mV[VZ]);

		LLVector4a left_norm;
		left_norm.setCross3(dir, up);
		left_norm.normalize3fast();
		LLVector4a up_norm;
		up_norm.setCross3(left_norm, dir);
		up_norm.normalize3fast();
		LLVector4a& dir_norm = dir;
		dir.normalize3fast();

		LLVector4a left_dot;
		left_dot.setAllDot3(left_norm, pos);
		left_dot.negate();
		LLVector4a up_dot;
		up_dot.setAllDot3(up_norm, pos);
		up_dot.negate();
		LLVector4a dir_dot;
		dir_dot.setAllDot3(dir_norm, pos);

		dir_norm.negate();

		LLMatrix4a lookat_mat;
		lookat_mat.setRow<0>(left_norm);
		lookat_mat.setRow<1>(up_norm);
		lookat_mat.setRow<2>(dir_norm);
		lookat_mat.setRow<3>(LLVector4a(0, 0, 0, 1));

		lookat_mat.getRow<0>().copyComponent<3>(left_dot);
		lookat_mat.getRow<1>().copyComponent<3>(up_dot);
		lookat_mat.getRow<2>().copyComponent<3>(dir_dot);

		lookat_mat.transpose();

		return lookat_mat;
	}

	inline const LLMatrix4a& genNDCtoWC()
	{
		return TRANS_MAT;
	}


	inline bool projectf(const LLVector3& object, const LLMatrix4a& modelview, const LLMatrix4a& projection, const LLRect& viewport, LLVector3& windowCoordinate)
	{
		//Begin SSE intrinsics

		// Declare locals
		const LLVector4a obj_vector(object.mV[VX], object.mV[VY], object.mV[VZ]);
		const LLVector4a one(1.f);
		LLVector4a temp_vec;								//Scratch vector
		LLVector4a w;										//Splatted W-component.

		modelview.affineTransform(obj_vector, temp_vec);	//temp_vec = modelview * obj_vector;

		//Passing temp_matrix as v and res is safe. res not altered until after all other calculations
		projection.rotate4(temp_vec, temp_vec);				//temp_vec = projection * temp_vec

		w.splat<3>(temp_vec);								//w = temp_vec.wwww

		//If w == 0.f, use 1.f instead.
		LLVector4a div;
		div.setSelectWithMask(w.equal(_mm_setzero_ps()), one, w);	//float div = (w[N] == 0.f ? 1.f : w[N]);
		temp_vec.div(div);									//temp_vec /= div;

		//Map x, y to range 0-1
		temp_vec.mul(.5f);
		temp_vec.add(.5f);

		LLVector4Logical mask = temp_vec.equal(_mm_setzero_ps());
		if (mask.areAllSet(LLVector4Logical::MASK_W))
			return false;

		//End SSE intrinsics

		//Window coordinates
		windowCoordinate[0] = temp_vec[VX] * viewport.getWidth() + viewport.mLeft;
		windowCoordinate[1] = temp_vec[VY] * viewport.getHeight() + viewport.mBottom;
		//This is only correct when glDepthRange(0.0, 1.0)
		windowCoordinate[2] = temp_vec[VZ];

		return true;
	}

	inline bool unprojectf(const LLVector3& windowCoordinate, const LLMatrix4a& modelview, const LLMatrix4a& projection, const LLRect& viewport, LLVector3& object)
	{
		//Begin SSE intrinsics

		// Declare locals
		static const LLVector4a one(1.f);
		static const LLVector4a two(2.f);
		LLVector4a norm_view(
			((windowCoordinate.mV[VX] - (F32)viewport.mLeft) / (F32)viewport.getWidth()),
			((windowCoordinate.mV[VY] - (F32)viewport.mBottom) / (F32)viewport.getHeight()),
			windowCoordinate.mV[VZ],
			1.f);

		LLMatrix4a inv_mat;								//Inverse transformation matrix
		LLVector4a temp_vec;							//Scratch vector
		LLVector4a w;									//Splatted W-component.

		inv_mat.setMul(projection, modelview);			//inv_mat = projection*modelview

		float det = inv_mat.invert();

		//Normalize. -1.0 : +1.0
		norm_view.mul(two);								// norm_view *= vec4(.2f)
		norm_view.sub(one);								// norm_view -= vec4(1.f)

		inv_mat.rotate4(norm_view, temp_vec);			//inv_mat * norm_view

		w.splat<3>(temp_vec);							//w = temp_vec.wwww

		//If w == 0.f, use 1.f instead. Defer return if temp_vec.w == 0.f until after all SSE intrinsics.
		LLVector4a div;
		div.setSelectWithMask(w.equal(_mm_setzero_ps()), one, w);	//float div = (w[N] == 0.f ? 1.f : w[N]);
		temp_vec.div(div);								//temp_vec /= div;

		LLVector4Logical mask = temp_vec.equal(_mm_setzero_ps());
		if (mask.areAllSet(LLVector4Logical::MASK_W))
			return false;

		//End SSE intrinsics

		if (det == 0.f)
			return false;

		object.set(temp_vec.getF32ptr());

		return true;
	}
}