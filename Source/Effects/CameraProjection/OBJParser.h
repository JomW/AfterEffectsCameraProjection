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
				// Face (supporting format: v/vt/vn)
				Face face;
				std::string vertex_data[3];
				
				for (int i = 0; i < 3; i++) {
					if (!(iss >> vertex_data[i])) {
						break;
					}
					
					// Parse v/vt/vn format
					size_t pos1 = vertex_data[i].find('/');
					size_t pos2 = vertex_data[i].find('/', pos1 + 1);
					
					if (pos1 != std::string::npos) {
						face.v[i] = std::stoi(vertex_data[i].substr(0, pos1)) - 1;
						
						if (pos2 != std::string::npos) {
							// Has vt and vn
							std::string vt_str = vertex_data[i].substr(pos1 + 1, pos2 - pos1 - 1);
							if (!vt_str.empty()) {
								face.vt[i] = std::stoi(vt_str) - 1;
							}
							face.vn[i] = std::stoi(vertex_data[i].substr(pos2 + 1)) - 1;
						}
						else {
							// Only has vt
							std::string vt_str = vertex_data[i].substr(pos1 + 1);
							if (!vt_str.empty()) {
								face.vt[i] = std::stoi(vt_str) - 1;
							}
						}
					}
					else {
						// Only vertex index
						face.v[i] = std::stoi(vertex_data[i]) - 1;
					}
				}
				
				mesh.faces.push_back(face);
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
};

#endif // OBJPARSER_H
