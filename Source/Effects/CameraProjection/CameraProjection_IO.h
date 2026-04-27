#include <string>
#include "OBJParser.h"

namespace CameraProjectionIO {

	struct SceneJsonData {
		bool has_transform = false;
		Vec3 position = Vec3(0.0f, 0.0f, 0.0f);
		Vec3 rotation = Vec3(0.0f, 0.0f, 0.0f);
		Vec3 scale = Vec3(1.0f, 1.0f, 1.0f);

		bool has_camera = false;
		Vec3 camera_pos = Vec3(0.0f, 0.0f, 0.0f);

		// Final validated export path:
		// the plugin consumes camera basis directly and does not rebuild
		// orientation from target / euler / quaternion anymore.
		bool has_camera_basis = false;
		Vec3 camera_right = Vec3(1.0f, 0.0f, 0.0f);
		Vec3 camera_up = Vec3(0.0f, 1.0f, 0.0f);
		Vec3 camera_forward = Vec3(0.0f, 0.0f, 1.0f);

		bool has_camera_fov = false;
		float camera_fov_deg = 50.0f;
	};

	bool ParseSceneJSONFile(const char* json_path, SceneJsonData& out_data);

} // namespace CameraProjectionIO