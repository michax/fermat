/*
 * Fermat
 *
 * Copyright (c) 2016-2018, NVIDIA CORPORATION. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of the NVIDIA CORPORATION nor the
 *      names of its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NVIDIA CORPORATION BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "types.h"

#include <optix_prime/optix_primepp.h>
#include <optixu/optixu_matrix.h>

#include <cugar/linalg/vector.h>

struct Camera
{
	float3	eye;
	float3	aim;
	float3	up;
	float3  dx;
	float   fov;

	Camera() :
		fov(60.0f * float(M_PI) / 180.0f)
	{
		eye = make_float3(0, -1, 0);
		aim = make_float3(0, 0, 0);
		up = make_float3(0, 0, 1);
		dx = make_float3(1, 0, 0);
	}

	Camera rotate(const float2 rot) const
	{
		Camera r;
		optix::Matrix<4, 4> rot_X = optix::Matrix<4, 4>::rotate(rot.x, dx);
		//optix::Matrix<4, 4> rot_Y = optix::Matrix<4, 4>::rotate(rot.y, up);
        optix::Matrix<4, 4> rot_Y = optix::Matrix<4, 4>::rotate(rot.y, make_float3(0,1,0));

		const float4 heye = make_float4(eye.x, eye.y, eye.z, 1.0f);
		const float4 haim = make_float4(aim.x, aim.y, aim.z, 1.0f);
		const float4 hup = make_float4(up.x, up.y, up.z, 0.0f);
		const float4 hdx = make_float4(dx.x, dx.y, dx.z, 0.0f);
		const float4 tdir = rot_Y * rot_X * (heye - haim);
		const float4 teye = haim + tdir;
		const float4 tup = rot_Y * rot_X * hup;
		const float4 tdx = rot_Y * rot_X * hdx;

		r.eye = make_float3(teye.x, teye.y, teye.z);
		r.aim = aim;
		r.up = make_float3(tup.x, tup.y, tup.z);
		r.dx = make_float3(tdx.x, tdx.y, tdx.z);
		r.fov = fov;
		return r;
	}
	Camera walk(const float delta) const
	{
		Camera r;
		r.eye = eye + (aim - eye)*delta;
		r.aim = aim /*+ (aim - eye)*delta*/;
		r.up  = up;
		r.dx  = normalize(cross(r.aim - r.eye, r.up));
		r.fov = fov;
		return r;
	}
	Camera pan(const float2 delta) const
	{
		Camera r;
		r.eye = eye + up*delta.y - dx * delta.x;
		r.aim = aim + up*delta.y - dx * delta.x;
		r.up = up;
		r.dx = dx;
		r.fov = fov;
		return r;
	}
	Camera zoom(const float delta) const
	{
		Camera r;
		r.eye = eye;
		r.aim = aim;
		r.up = up;
		r.dx = dx;
		r.fov = fov * (1.0f + delta);
		r.fov = fmaxf(fminf(r.fov, float(M_PI) - 0.1f), 0.05f);
		return r;
	}

	// return the image plane distance needed to have pixels with unit area
	FERMAT_HOST_DEVICE
	float square_pixel_focal_length(
		const uint32 res_x,
		const uint32 res_y) const
	{
		const float t = tanf(fov / 2);
		return (float(res_x * res_y) / 4.0f) / (t*t);
	}

	// return the image plane distance needed to have screen with unit area
	FERMAT_HOST_DEVICE
	float square_screen_focal_length() const
	{
		const float t = tanf(fov / 2);
		return (1.0f / 4.0f) / (t*t);
	}
};

FERMAT_HOST_DEVICE
inline cugar::Vector2f invert_camera_sampler(const cugar::Vector3f& U, const cugar::Vector3f& V, const cugar::Vector3f& W, const float W_len, const cugar::Vector3f out)
{
	const float t = cugar::dot(out, cugar::Vector3f(W)) / (W_len*W_len);
	if (t < 0.0f)
		return 0.0f;

	const cugar::Vector3f I = out / t - W;
	const float Ix = dot(I, U) / cugar::square_length(U);
	const float Iy = dot(I, V) / cugar::square_length(V);

	return cugar::Vector2f( Ix*0.5f + 0.5f, Iy*0.5f + 0.5f );
}

FERMAT_HOST_DEVICE
inline float camera_direction_pdf(const cugar::Vector3f& U, const cugar::Vector3f& V, const cugar::Vector3f& W, const float W_len, const float square_focal_length, const cugar::Vector3f out, float* out_x = 0, float* out_y = 0)
{
	const float t = cugar::dot(out, cugar::Vector3f(W)) / (W_len*W_len);
	if (t < 0.0f)
		return 0.0f;

	const cugar::Vector3f I = out / t - W;
	const float Ix = dot(I, U) / cugar::square_length(U);
	const float Iy = dot(I, V) / cugar::square_length(V);

	if (Ix >= -1.0f && Ix <= 1.0f &&
		Iy >= -1.0f && Iy <= 1.0f)
	{
		if (out_x) *out_x = Ix;
		if (out_y) *out_y = Iy;

		const float cos_theta = dot(out, W) / W_len;
		return square_focal_length / (cos_theta * cos_theta * cos_theta * cos_theta);
	}

	return 0.0f;
}

FERMAT_HOST_DEVICE
inline float camera_direction_pdf(const cugar::Vector3f& U, const cugar::Vector3f& V, const cugar::Vector3f& W, const float W_len, const float square_focal_length, const cugar::Vector3f out, bool projected = false)
{
	const float t = cugar::dot(out, cugar::Vector3f(W)) / (W_len*W_len);
	if (t < 0.0f)
		return 0.0f;

	const cugar::Vector3f I = out / t - W;
	const float Ix = dot(I, U) / cugar::square_length(U);
	const float Iy = dot(I, V) / cugar::square_length(V);

	if (Ix >= -1.0f && Ix <= 1.0f &&
		Iy >= -1.0f && Iy <= 1.0f)
	{
		const float cos_theta = dot(out, W) / W_len;
		return projected ?
			square_focal_length / (cos_theta * cos_theta * cos_theta * cos_theta) :
			square_focal_length / (cos_theta * cos_theta * cos_theta);
	}

	return 0.0f;
}

FERMAT_HOST_DEVICE
inline void camera_frame(cugar::Vector3f eye, cugar::Vector3f lookat, cugar::Vector3f up, float hfov, float aspect_ratio, cugar::Vector3f& U, cugar::Vector3f& V, cugar::Vector3f& W)
{
	float ulen, vlen, wlen;
	W.x = lookat.x - eye.x;
	W.y = lookat.y - eye.y;
	W.z = lookat.z - eye.z;

	wlen = sqrtf(cugar::dot(W, W));

	U = cugar::normalize(cugar::cross(W, up));
	V = cugar::normalize(cugar::cross(U, W));

	ulen = wlen * tanf(hfov / 2.0f);
	U.x *= ulen;
	U.y *= ulen;
	U.z *= ulen;

	vlen = ulen / aspect_ratio;
	V.x *= vlen;
	V.y *= vlen;
	V.z *= vlen;
}

FERMAT_HOST_DEVICE
inline void camera_frame(const Camera camera, float aspect_ratio, cugar::Vector3f& U, cugar::Vector3f& V, cugar::Vector3f& W)
{
	camera_frame(camera.eye, camera.aim, camera.up, camera.fov, aspect_ratio, U, V, W);
}
