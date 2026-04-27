/*******************************************************************/
/*                                                                 */
/*                      ADOBE CONFIDENTIAL                         */
/*                   _ _ _ _ _ _ _ _ _ _ _ _ _                     */
/*                                                                 */
/* Copyright 2007-2023 Adobe Inc.                                  */
/* All Rights Reserved.                                            */
/*                                                                 */
/*******************************************************************/

#pragma once

#ifndef PROJECTIONMATH_H
#define PROJECTIONMATH_H

#include "OBJParser.h"
#include <cmath>
#include <algorithm>

// Matrix 4x4 (column-major)
struct Mat4 {
	float m[16];
	
	Mat4() {
		for (int i = 0; i < 16; i++) m[i] = 0;
		m[0] = m[5] = m[10] = m[15] = 1;  // Identity
	}
	
	static Mat4 Identity() {
		return Mat4();
	}
	
	// Look-at matrix
	static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
		Vec3 zaxis = (eye - target).normalize();
		Vec3 xaxis = up.cross(zaxis).normalize();
		Vec3 yaxis = zaxis.cross(xaxis);
		
		Mat4 mat;
		mat.m[0] = xaxis.x;  mat.m[4] = xaxis.y;  mat.m[8] = xaxis.z;   mat.m[12] = -xaxis.dot(eye);
		mat.m[1] = yaxis.x;  mat.m[5] = yaxis.y;  mat.m[9] = yaxis.z;   mat.m[13] = -yaxis.dot(eye);
		mat.m[2] = zaxis.x;  mat.m[6] = zaxis.y;  mat.m[10] = zaxis.z;  mat.m[14] = -zaxis.dot(eye);
		mat.m[3] = 0;        mat.m[7] = 0;        mat.m[11] = 0;        mat.m[15] = 1;
		
		return mat;
	}
	
	// Perspective projection matrix
	static Mat4 Perspective(float fov_degrees, float aspect, float near_plane, float far_plane) {
		float fov_rad = fov_degrees * 3.14159265f / 180.0f;
		float tan_half_fov = std::tan(fov_rad / 2.0f);
		
		Mat4 mat;
		mat.m[0] = 1.0f / (aspect * tan_half_fov);
		mat.m[5] = 1.0f / tan_half_fov;
		mat.m[10] = -(far_plane + near_plane) / (far_plane - near_plane);
		mat.m[11] = -1.0f;
		mat.m[14] = -(2.0f * far_plane * near_plane) / (far_plane - near_plane);
		mat.m[15] = 0.0f;
		
		return mat;
	}
	
	// Matrix multiplication
	Mat4 operator*(const Mat4& other) const {
		Mat4 result;
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				result.m[i * 4 + j] = 0;
				for (int k = 0; k < 4; k++) {
					result.m[i * 4 + j] += m[i * 4 + k] * other.m[k * 4 + j];
				}
			}
		}
		return result;
	}
	
	// Transform Vec4
	Vec4 Transform(const Vec4& v) const {
		Vec4 result;
		result.x = m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w;
		result.y = m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w;
		result.z = m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w;
		result.w = m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w;
		return result;
	}
};

// Projection utilities
class ProjectionMath {
public:
	// Project 3D point to screen space (0-1 range)
	static Vec2 ProjectToScreen(const Vec3& world_pos, const Mat4& viewProj, bool& is_valid) {
		Vec4 clip = viewProj.Transform(Vec4(world_pos, 1.0f));
		
		// Perspective divide
		if (clip.w <= 0.0f) {
			is_valid = false;
			return Vec2(0, 0);
		}
		
		Vec3 ndc(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);
		
		// Check if behind camera or outside frustum
		if (ndc.z < -1.0f || ndc.z > 1.0f) {
			is_valid = false;
			return Vec2(0, 0);
		}
		
		// Convert NDC to screen space (0-1)
		Vec2 screen;
		screen.x = (ndc.x + 1.0f) * 0.5f;
		screen.y = (1.0f - ndc.y) * 0.5f;  // Flip Y
		
		is_valid = (screen.x >= 0.0f && screen.x <= 1.0f && 
					screen.y >= 0.0f && screen.y <= 1.0f);
		
		return screen;
	}
	
	// Compute barycentric coordinates
	static Vec3 Barycentric(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) {
		Vec2 v0 = Vec2(b.x - a.x, b.y - a.y);
		Vec2 v1 = Vec2(c.x - a.x, c.y - a.y);
		Vec2 v2 = Vec2(p.x - a.x, p.y - a.y);
		
		float d00 = v0.x * v0.x + v0.y * v0.y;
		float d01 = v0.x * v1.x + v0.y * v1.y;
		float d11 = v1.x * v1.x + v1.y * v1.y;
		float d20 = v2.x * v0.x + v2.y * v0.y;
		float d21 = v2.x * v1.x + v2.y * v1.y;
		
		float denom = d00 * d11 - d01 * d01;
		if (std::abs(denom) < 1e-8f) {
			return Vec3(-1, -1, -1);  // Degenerate triangle
		}
		
		float v = (d11 * d20 - d01 * d21) / denom;
		float w = (d00 * d21 - d01 * d20) / denom;
		float u = 1.0f - v - w;
		
		return Vec3(u, v, w);
	}
	
	// Check if point is inside triangle (using barycentric)
	static bool IsInsideTriangle(const Vec3& bary) {
		return bary.x >= 0.0f && bary.y >= 0.0f && bary.z >= 0.0f;
	}
	
	// Interpolate UV using barycentric coordinates
	static Vec2 InterpolateUV(const Vec2& uv0, const Vec2& uv1, const Vec2& uv2, const Vec3& bary) {
		return Vec2(
			uv0.x * bary.x + uv1.x * bary.y + uv2.x * bary.z,
			uv0.y * bary.x + uv1.y * bary.y + uv2.y * bary.z
		);
	}
	
	// Check backface culling
	static bool IsFrontFacing(const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec3& view_dir) {
		Vec3 edge1 = v1 - v0;
		Vec3 edge2 = v2 - v0;
		Vec3 normal = edge1.cross(edge2).normalize();
		return normal.dot(view_dir) < 0;  // Front face if normal points away from view
	}
	
	// Clamp value to range
	static float Clamp(float value, float min, float max) {
		return std::max(min, std::min(max, value));
	}
	
	static int ClampInt(int value, int min, int max) {
		return std::max(min, std::min(max, value));
	}
};

#endif // PROJECTIONMATH_H
