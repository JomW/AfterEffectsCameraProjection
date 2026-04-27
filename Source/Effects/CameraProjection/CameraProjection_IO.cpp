#include <fstream>
#include <sstream>
#include <cstdlib>

namespace CameraProjectionIO {

	static bool ReadTextFile(const char* path, std::string& out_text)
	{
		out_text.clear();
		if (!path || !path[0]) {
			return false;
		}

		std::ifstream ifs(path, std::ios::in | std::ios::binary);
		if (!ifs.is_open()) {
			return false;
		}

		std::ostringstream ss;
		ss << ifs.rdbuf();
		out_text = ss.str();
		return !out_text.empty();
	}

	static bool ExtractStringField(const std::string& json, const char* key, std::string& out_value)
	{
		const std::string token = std::string("\"") + key + "\"";
		size_t p = json.find(token);
		if (p == std::string::npos) {
			return false;
		}

		p = json.find(':', p + token.size());
		if (p == std::string::npos) {
			return false;
		}

		size_t q1 = json.find('"', p + 1);
		if (q1 == std::string::npos) {
			return false;
		}

		size_t q2 = json.find('"', q1 + 1);
		if (q2 == std::string::npos) {
			return false;
		}

		out_value = json.substr(q1 + 1, q2 - q1 - 1);
		return true;
	}

	static bool ExtractVec3Field(const std::string& json, const char* key, Vec3& out_v)
	{
		const std::string token = std::string("\"") + key + "\"";
		size_t p = json.find(token);
		if (p == std::string::npos) {
			return false;
		}

		p = json.find('[', p + token.size());
		if (p == std::string::npos) {
			return false;
		}

		size_t e = json.find(']', p + 1);
		if (e == std::string::npos) {
			return false;
		}

		std::string arr = json.substr(p + 1, e - p - 1);
		for (char& c : arr) {
			if (c == ',') {
				c = ' ';
			}
		}

		std::istringstream iss(arr);
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		if (!(iss >> x >> y >> z)) {
			return false;
		}

		out_v = Vec3(x, y, z);
		return true;
	}

	static bool ExtractFloatField(const std::string& json, const char* key, float& out_value)
	{
		const std::string token = std::string("\"") + key + "\"";
		size_t p = json.find(token);
		if (p == std::string::npos) {
			return false;
		}

		p = json.find(':', p + token.size());
		if (p == std::string::npos) {
			return false;
		}

		size_t v = json.find_first_not_of(" \t\r\n", p + 1);
		if (v == std::string::npos) {
			return false;
		}

		size_t end = json.find_first_of(",}\r\n\t ", v);
		std::string num = (end == std::string::npos) ? json.substr(v) : json.substr(v, end - v);

		char* ep = nullptr;
		out_value = static_cast<float>(std::strtod(num.c_str(), &ep));
		return ep != num.c_str();
	}

	bool ParseSceneJSONFile(const char* json_path, SceneJsonData& out_data)
	{
		out_data = SceneJsonData();

		std::string json;
		if (!ReadTextFile(json_path, json)) {
			return false;
		}

		Vec3 pos;
		Vec3 rot;
		Vec3 scl;
		const bool has_pos = ExtractVec3Field(json, "position", pos);
		const bool has_rot = ExtractVec3Field(json, "rotation", rot);
		const bool has_scl = ExtractVec3Field(json, "scale", scl);

		if (has_pos || has_rot || has_scl) {
			out_data.has_transform = true;
			if (has_pos) {
				out_data.position = pos;
			}
			if (has_rot) {
				out_data.rotation = rot;
			}
			if (has_scl) {
				out_data.scale = scl;
			}
		}

		Vec3 cam_pos;
		if (ExtractVec3Field(json, "cameraPosition", cam_pos)) {
			out_data.camera_pos = cam_pos;
			out_data.has_camera = true;
		}

		Vec3 cam_right;
		Vec3 cam_up;
		Vec3 cam_forward;
		const bool has_cam_right = ExtractVec3Field(json, "cameraRight", cam_right);
		const bool has_cam_up = ExtractVec3Field(json, "cameraUp", cam_up);
		const bool has_cam_forward = ExtractVec3Field(json, "cameraForward", cam_forward);

		if (has_cam_right && has_cam_up && has_cam_forward) {
			out_data.has_camera_basis = true;
			out_data.camera_right = cam_right;
			out_data.camera_up = cam_up;
			out_data.camera_forward = cam_forward;
			out_data.has_camera = true;
		}

		float cam_fov = 0.0f;
		if (ExtractFloatField(json, "cameraFovDegrees", cam_fov)) {
			out_data.has_camera_fov = true;
			out_data.camera_fov_deg = cam_fov;
			out_data.has_camera = true;
		}

		return out_data.has_camera && out_data.has_camera_basis;
	}

} // namespace CameraProjectionIO