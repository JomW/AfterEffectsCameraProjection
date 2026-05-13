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

#ifndef OBJPARSER_H
#define OBJPARSER_H

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdlib>

// Vector structures
struct Vec2 {
	float x, y;
	Vec2() : x(0), y(0) {}
	Vec2(float x_, float y_) : x(x_), y(y_) {}
};

struct Vec3 {
	float x, y, z;
	Vec3() : x(0), y(0), z(0) {}
	Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
	
	Vec3 operator+(const Vec3& v) const { return Vec3(x + v.x, y + v.y, z + v.z); }
	Vec3 operator-(const Vec3& v) const { return Vec3(x - v.x, y - v.y, z - v.z); }
	Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
	float dot(const Vec3& v) const { return x * v.x + y * v.y + z * v.z; }
	Vec3 cross(const Vec3& v) const {
		return Vec3(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
	}
	float length() const { return std::sqrt(x * x + y * y + z * z); }
	Vec3 normalize() const {
		float len = length();
		return len > 0 ? Vec3(x / len, y / len, z / len) : Vec3(0, 0, 0);
	}
};

struct Vec4 {
	float x, y, z, w;
	Vec4() : x(0), y(0), z(0), w(1) {}
	Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
	Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
};

// Face structure (vertex/uv/normal indices)
struct Face {
	int v[3];   // vertex indices
	int vt[3];  // texture coordinate indices
	int vn[3];  // normal indices
	
	Face() {
		v[0] = v[1] = v[2] = -1;
		vt[0] = vt[1] = vt[2] = -1;
		vn[0] = vn[1] = vn[2] = -1;
	}
};

// Mesh data structure
struct MeshData {
	std::vector<Vec3> vertices;
	std::vector<Vec2> uvs;
	std::vector<Vec3> normals;
	std::vector<Face> faces;
	
	void clear() {
		vertices.clear();
		uvs.clear();
		normals.clear();
		faces.clear();
	}
};

// Camera data structure
struct CameraData {
	Vec3 position;
	Vec3 target;
	Vec3 up;
	float fov;  // field of view in degrees
	float aspect;
	float near_plane;
	float far_plane;
	
	CameraData() : position(0, 0, 5), target(0, 0, 0), up(0, 1, 0),
				   fov(50.0f), aspect(16.0f/9.0f), near_plane(0.1f), far_plane(1000.0f) {}
};

// OBJ Parser
class OBJParser {
public:
	struct FaceVertexIndices {
		int v;
		int vt;
		int vn;

		FaceVertexIndices()
			: v(-1), vt(-1), vn(-1) {
		}
	};

	static bool LoadOBJ(const char* filepath, MeshData& mesh) {
		mesh.clear();
		
		std::ifstream file(filepath);
		if (!file.is_open()) {
			return false;
		}
		
		std::string line;
		while (std::getline(file, line)) {
			std::istringstream iss(line);
			std::string prefix;
			iss >> prefix;
			
			if (prefix == "v") {
				// Vertex position
				Vec3 v;
				iss >> v.x >> v.y >> v.z;
				mesh.vertices.push_back(v);
			}
			else if (prefix == "vt") {
				// Texture coordinate
				Vec2 vt;
				iss >> vt.x >> vt.y;
				mesh.uvs.push_back(vt);
			}
			else if (prefix == "vn") {
				// Normal
				Vec3 vn;
				iss >> vn.x >> vn.y >> vn.z;
				mesh.normals.push_back(vn);
			}
			else if (prefix == "f") {
				std::vector<FaceVertexIndices> polygon_vertices;
				std::string vertex_token;
				bool face_valid = true;

				while (iss >> vertex_token) {
					if (!vertex_token.empty() && vertex_token[0] == '#') {
						break;
					}

					FaceVertexIndices parsed_vertex;
					if (!ParseFaceVertexToken(vertex_token,
						static_cast<int>(mesh.vertices.size()),
						static_cast<int>(mesh.uvs.size()),
						static_cast<int>(mesh.normals.size()),
						parsed_vertex)) {
						face_valid = false;
						break;
					}

					polygon_vertices.push_back(parsed_vertex);
				}

				if (!face_valid || polygon_vertices.size() < 3) {
					continue;
				}

				const FaceVertexIndices& v0 = polygon_vertices[0];
				for (size_t i = 1; i + 1 < polygon_vertices.size(); ++i) {
					Face tri_face;
					if (!BuildTriangleFace(v0, polygon_vertices[i], polygon_vertices[i + 1], tri_face)) {
						continue;
					}

					if (IsDegenerateTriangle(tri_face, mesh)) {
						continue;
					}

					mesh.faces.push_back(tri_face);
				}
			}
		}
		
		file.close();
		return !mesh.vertices.empty() && !mesh.faces.empty();
	}
	
	// Load camera data from a simple text file format
	// Format:
	// pos x y z
	// target x y z
	// up x y z
	// fov value
	static bool LoadCamera(const char* filepath, CameraData& camera) {
		std::ifstream file(filepath);
		if (!file.is_open()) {
			return false;
		}
		
		std::string line;
		while (std::getline(file, line)) {
			std::istringstream iss(line);
			std::string prefix;
			iss >> prefix;
			
			if (prefix == "pos" || prefix == "position") {
				iss >> camera.position.x >> camera.position.y >> camera.position.z;
			}
			else if (prefix == "target" || prefix == "lookat") {
				iss >> camera.target.x >> camera.target.y >> camera.target.z;
			}
			else if (prefix == "up") {
				iss >> camera.up.x >> camera.up.y >> camera.up.z;
			}
			else if (prefix == "fov") {
				iss >> camera.fov;
			}
			else if (prefix == "aspect") {
				iss >> camera.aspect;
			}
			else if (prefix == "near") {
				iss >> camera.near_plane;
			}
			else if (prefix == "far") {
				iss >> camera.far_plane;
			}
		}
		
		file.close();
		return true;
	}

private:
	static bool ParseOBJIndex(const std::string& token, int value_count, int& out_index) {
		if (token.empty()) {
			return false;
		}

		char* end_ptr = nullptr;
		const long raw_index = std::strtol(token.c_str(), &end_ptr, 10);
		if (end_ptr == token.c_str() || *end_ptr != '\0' || raw_index == 0) {
			return false;
		}

		long resolved_index = raw_index;
		if (raw_index > 0) {
			resolved_index = raw_index - 1;
		}
		else {
			resolved_index = static_cast<long>(value_count) + raw_index;
		}

		if (resolved_index < 0 || resolved_index >= value_count) {
			return false;
		}

		out_index = static_cast<int>(resolved_index);
		return true;
	}

	static bool ParseFaceVertexToken(const std::string& token,
		int vertex_count,
		int uv_count,
		int normal_count,
		FaceVertexIndices& out_vertex) {
		const size_t pos1 = token.find('/');
		if (pos1 == std::string::npos) {
			return ParseOBJIndex(token, vertex_count, out_vertex.v);
		}

		const std::string v_str = token.substr(0, pos1);
		if (!ParseOBJIndex(v_str, vertex_count, out_vertex.v)) {
			return false;
		}

		const size_t pos2 = token.find('/', pos1 + 1);
		if (pos2 == std::string::npos) {
			const std::string vt_str = token.substr(pos1 + 1);
			if (!vt_str.empty() && !ParseOBJIndex(vt_str, uv_count, out_vertex.vt)) {
				return false;
			}
			return true;
		}

		const std::string vt_str = token.substr(pos1 + 1, pos2 - pos1 - 1);
		if (!vt_str.empty() && !ParseOBJIndex(vt_str, uv_count, out_vertex.vt)) {
			return false;
		}

		const std::string vn_str = token.substr(pos2 + 1);
		if (!vn_str.empty() && !ParseOBJIndex(vn_str, normal_count, out_vertex.vn)) {
			return false;
		}

		return true;
	}

	static bool BuildTriangleFace(const FaceVertexIndices& a,
		const FaceVertexIndices& b,
		const FaceVertexIndices& c,
		Face& out_face) {
		if (a.v < 0 || b.v < 0 || c.v < 0) {
			return false;
		}

		out_face.v[0] = a.v;
		out_face.v[1] = b.v;
		out_face.v[2] = c.v;
		out_face.vt[0] = a.vt;
		out_face.vt[1] = b.vt;
		out_face.vt[2] = c.vt;
		out_face.vn[0] = a.vn;
		out_face.vn[1] = b.vn;
		out_face.vn[2] = c.vn;
		return true;
	}

	static bool IsDegenerateTriangle(const Face& face, const MeshData& mesh) {
		if (face.v[0] == face.v[1] || face.v[1] == face.v[2] || face.v[2] == face.v[0]) {
			return true;
		}

		if (face.v[0] < 0 || face.v[1] < 0 || face.v[2] < 0 ||
			face.v[0] >= static_cast<int>(mesh.vertices.size()) ||
			face.v[1] >= static_cast<int>(mesh.vertices.size()) ||
			face.v[2] >= static_cast<int>(mesh.vertices.size())) {
			return true;
		}

		const Vec3& p0 = mesh.vertices[face.v[0]];
		const Vec3& p1 = mesh.vertices[face.v[1]];
		const Vec3& p2 = mesh.vertices[face.v[2]];
		const Vec3 cross = (p1 - p0).cross(p2 - p0);
		const float area_sq = cross.dot(cross);
		return area_sq <= 1.0e-12f;
	}
};

#endif // OBJPARSER_H
