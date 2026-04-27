#include "CameraProjection.h"
#include "OBJParser.h"
#include "ProjectionMath.h"
#include "CameraProjection_IO.h"
#include "CameraProjection_IO.cpp"

#include <vector>
#include <limits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <string>
#include <sstream>
#include <algorithm>
#include <memory>
#include <mutex>
#include <sys/stat.h>

#ifdef AE_OS_WIN
#include <commdlg.h>
#include <fstream>
#endif

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace {

	constexpr float kPi = 3.14159265358979323846f;
	static char g_selected_obj_path[AEGP_MAX_PATH_SIZE] = { 0 };
	static char g_selected_json_path[AEGP_MAX_PATH_SIZE] = { 0 };

	struct ObjectTransform {
		Vec3 pos;
		Vec3 rot_deg;
		Vec3 scale;

		ObjectTransform()
			: pos(0.0f, 0.0f, 0.0f)
			, rot_deg(0.0f, 0.0f, 0.0f)
			, scale(1.0f, 1.0f, 1.0f) {
		}
	};

	struct CameraContext;

	// -----------------------------------------------------------------------
	// SceneData: holds all resolved paths and JSON-parsed data in one place.
	// Used by both Render and ShowDebugPopup to avoid duplicated parse logic.
	// -----------------------------------------------------------------------
	struct SceneData {
		const char* obj_path = nullptr;
		const char* json_path = nullptr;

		bool json_ok = false;
		bool has_transform = false;

		bool has_camera = false;
		bool has_camera_fov = false;
		bool has_camera_basis = false;

		Vec3 cam_pos;
		Vec3 cam_right;
		Vec3 cam_up;
		Vec3 cam_forward;

		float camera_fov_deg = 50.0f;
		ObjectTransform json_xform;

		std::shared_ptr<const MeshData> meshP;
		bool mesh_ok = false;
	};

	struct SceneCacheSnapshot {
		std::string obj_path;
		std::string json_path;

#ifdef AE_OS_WIN
		__time64_t obj_mtime = 0;
		__time64_t json_mtime = 0;
#else
		time_t obj_mtime = 0;
		time_t json_mtime = 0;
#endif

		bool obj_time_valid = false;
		bool json_time_valid = false;

		bool json_ok = false;
		bool mesh_ok = false;

		CameraProjectionIO::SceneJsonData json_data;
		std::shared_ptr<const MeshData> meshP;
	};

	static std::mutex g_scene_cache_mutex;
	static std::shared_ptr<const SceneCacheSnapshot> g_scene_cache;

#ifdef AE_OS_WIN
	static char g_default_obj_path[AEGP_MAX_PATH_SIZE] = { 0 };
	static char g_default_json_path[AEGP_MAX_PATH_SIZE] = { 0 };
#endif

	enum class RenderStatus {
		Ok,
		NoOBJPath,
		OBJLoadFailed,
		NoUV,
		NoCamera,
		NoProjectionHit
	};

	// -----------------------------------------------------------------------
	// RenderPathInfo: records which combination of parameters was actually
	// used to produce pixels. Printed by ShowDebugPopup.
	// -----------------------------------------------------------------------

	struct RenderPathInfo {
		bool	used_ae_camera = false;
		bool	used_json_camera = false;
		bool	used_json_xform = false;
		bool	use_negative_z = false;
		bool	show_mesh = false;
		bool	wireframe = false;
		A_long	written_pixels = 0;
		RenderStatus status = RenderStatus::Ok;

		std::string Summary() const
		{
			std::string s;
			s += "Camera=";
			if (used_ae_camera) {
				s += "AEMatrix";
			}
			else if (used_json_camera) {
				s += "JSON-Basis";
			}
			else {
				s += "None";
			}

			s += " | Axis=Direct";

			s += " | Z=";
			s += use_negative_z ? "Neg" : "Pos";

			if (used_json_xform) {
				s += " | JSONXform";
			}
			else {
				s += " | IdentityXform";
			}

			s += " | Mode=";
			if (show_mesh) {
				s += "ShowMesh";
			}
			else if (wireframe) {
				s += "Projection+Wireframe";
			}
			else {
				s += "Projection";
			}

			s += " | Pixels=";
			char buf[32];
			snprintf(buf, sizeof(buf), "%ld", written_pixels);
			s += buf;

			return s;
		}
	};

	constexpr A_long CAMPROJ_SHOW_MESH = CAMPROJ_NUM_PARAMS;
	constexpr A_long SHOW_MESH_DISK_ID = SHOW_WIREFRAME_DISK_ID + 1;
	constexpr A_long CAMPROJ_USE_JSON_XFORM = CAMPROJ_NUM_PARAMS + 1;
	constexpr A_long USE_JSON_XFORM_DISK_ID = SHOW_MESH_DISK_ID + 1;
	constexpr A_long CAMPROJ_NUM_PARAMS_EXT = CAMPROJ_NUM_PARAMS + 2;

	struct ProjectionStats {
		A_long faces_total;
		A_long faces_index_ok;
		A_long faces_projectable;
		A_long verts_total;
		A_long verts_projected;
		A_long verts_inside_src;
		float uv_min_x, uv_min_y, uv_max_x, uv_max_y;
		float src_min_x, src_min_y, src_max_x, src_max_y;
		float depth_min, depth_max;
		bool has_uv;
		bool has_src;
		bool has_depth;

		ProjectionStats()
			: faces_total(0)
			, faces_index_ok(0)
			, faces_projectable(0)
			, verts_total(0)
			, verts_projected(0)
			, verts_inside_src(0)
			, uv_min_x(0.0f), uv_min_y(0.0f), uv_max_x(0.0f), uv_max_y(0.0f)
			, src_min_x(0.0f), src_min_y(0.0f), src_max_x(0.0f), src_max_y(0.0f)
			, depth_min(0.0f), depth_max(0.0f)
			, has_uv(false)
			, has_src(false)
			, has_depth(false) {
		}
	};

	static void AccumMinMax(float v, float& mn, float& mx, bool& has);
	static ProjectionStats CollectProjectionStats(const MeshData& mesh,
		const CameraContext& camera,
		const ObjectTransform& obj_xform,
		bool use_negative_z,
		const PF_LayerDef* input);

	static float ComputeProjectionCenterCost(const ProjectionStats& s, const CameraContext& camera);

	inline float Min3(float a, float b, float c)
	{
		return std::min(a, std::min(b, c));
	}

	inline float Max3(float a, float b, float c)
	{
		return std::max(a, std::max(b, c));
	}


	inline Vec3 ConvertPointFromOBJToRender(const Vec3& p)
	{
		// Final validated mapping for this exporter chain:
		// OBJ stores vertices in a basis that must be converted to the
		// JSON/Render basis used by cameraPosition/cameraRight/cameraUp/cameraForward.
		// Do not change unless the OBJ exporter itself changes.
		return Vec3(
			p.x,
			-p.y,
			-p.z
		);
	}

#ifdef AE_OS_WIN
	static bool FileExistsA(const char* path)
	{
		if (!path || !path[0]) {
			return false;
		}

		const DWORD attrs = GetFileAttributesA(path);
		return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
	}

	static bool GetCurrentPluginDirectoryA(char* out_dir, size_t out_size)
	{
		if (!out_dir || out_size == 0) {
			return false;
		}

		HMODULE module_handle = nullptr;
		if (!GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(&GetCurrentPluginDirectoryA),
			&module_handle)) {
			return false;
		}

		char module_path[MAX_PATH] = { 0 };
		const DWORD len = GetModuleFileNameA(module_handle, module_path, MAX_PATH);
		if (len == 0 || len >= MAX_PATH) {
			return false;
		}

		char* slash = std::strrchr(module_path, '\\');
		if (!slash) {
			slash = std::strrchr(module_path, '/');
		}
		if (!slash) {
			return false;
		}

		*slash = '\0';
		strncpy_s(out_dir, out_size, module_path, _TRUNCATE);
		return true;
	}

	static bool BuildPluginSiblingFilePathA(const char* filename, char* out_path, size_t out_size)
	{
		if (!filename || !filename[0] || !out_path || out_size == 0) {
			return false;
		}

		char plugin_dir[MAX_PATH] = { 0 };
		if (!GetCurrentPluginDirectoryA(plugin_dir, sizeof(plugin_dir))) {
			return false;
		}

		if (sprintf_s(out_path, out_size, "%s\\%s", plugin_dir, filename) <= 0) {
			return false;
		}

		return true;
	}

	static bool ResolveDefaultPluginDataFileA(const char* filename, char* out_path, size_t out_size)
	{
		if (!BuildPluginSiblingFilePathA(filename, out_path, out_size)) {
			return false;
		}
		return FileExistsA(out_path);
	}

	static bool GetFileModificationTime(const char* path, __time64_t& out_time, bool& out_valid)
	{
		out_time = 0;
		out_valid = false;

		if (!path || !path[0]) {
			return true;
		}

		struct __stat64 st {};
		if (_stat64(path, &st) != 0) {
			return false;
		}

		out_time = st.st_mtime;
		out_valid = true;
		return true;
	}
#else
	static bool GetFileModificationTime(const char* path, time_t& out_time, bool& out_valid)
	{
		out_time = 0;
		out_valid = false;

		if (!path || !path[0]) {
			return true;
		}

		struct stat st {};
		if (stat(path, &st) != 0) {
			return false;
		}

		out_time = st.st_mtime;
		out_valid = true;
		return true;
	}
#endif

	static bool CachedPathEquals(const std::string& a, const char* b)
	{
		const char* rhs = (b ? b : "");
		return a == rhs;
	}

	static bool SceneCacheMatchesPathsAndTimes(const std::shared_ptr<const SceneCacheSnapshot>& cache, const char* obj_path, const char* json_path)
	{
		if (!cache) {
			return false;
		}

		if (!CachedPathEquals(cache->obj_path, obj_path) ||
			!CachedPathEquals(cache->json_path, json_path)) {
			return false;
		}

#ifdef AE_OS_WIN
		__time64_t obj_mtime = 0;
		__time64_t json_mtime = 0;
#else
		time_t obj_mtime = 0;
		time_t json_mtime = 0;
#endif
		bool obj_valid = false;
		bool json_valid = false;

		if (!GetFileModificationTime(obj_path, obj_mtime, obj_valid)) {
			return false;
		}
		if (!GetFileModificationTime(json_path, json_mtime, json_valid)) {
			return false;
		}

		return cache->obj_time_valid == obj_valid &&
			cache->json_time_valid == json_valid &&
			(!obj_valid || cache->obj_mtime == obj_mtime) &&
			(!json_valid || cache->json_mtime == json_mtime);
	}

#ifdef AE_OS_WIN
	static bool SelectFilePathDialog(const char* filter, const char* def_ext, char* out_path, size_t out_path_size)
	{
		if (!out_path || out_path_size == 0) {
			return false;
		}
		OPENFILENAMEA ofn;
		char filename[MAX_PATH] = { 0 };
		ZeroMemory(&ofn, sizeof(ofn));
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFilter = filter;
		ofn.lpstrFile = filename;
		ofn.nMaxFile = MAX_PATH;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
		ofn.lpstrDefExt = def_ext;
		if (GetOpenFileNameA(&ofn)) {
			strncpy_s(out_path, out_path_size, filename, _TRUNCATE);
			return true;
		}
		return false;
	}

	static bool SelectOBJPathDialog(char* out_path, size_t out_path_size)
	{
		return SelectFilePathDialog("OBJ Files (*.obj)\0*.obj\0All Files (*.*)\0*.*\0", "obj", out_path, out_path_size);
	}

	static bool SelectJSONPathDialog(char* out_path, size_t out_path_size)
	{
		return SelectFilePathDialog("JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0", "json", out_path, out_path_size);
	}

	static bool GetCacheFilePath(const char* filename, char* out_path, size_t out_size)
	{
		if (!out_path || out_size == 0 || !filename) {
			return false;
		}
		char temp_dir[MAX_PATH] = { 0 };
		DWORD len = GetTempPathA(MAX_PATH, temp_dir);
		if (len == 0 || len >= MAX_PATH) {
			return false;
		}
		if (sprintf_s(out_path, out_size, "%s%s", temp_dir, filename) <= 0) {
			return false;
		}
		return true;
	}

	static void SavePathToCache(const char* cache_filename, const char* path)
	{
		if (!path || !path[0]) {
			return;
		}
		char cache_path[MAX_PATH] = { 0 };
		if (!GetCacheFilePath(cache_filename, cache_path, sizeof(cache_path))) {
			return;
		}
		std::ofstream ofs(cache_path, std::ios::out | std::ios::trunc);
		if (ofs.is_open()) {
			ofs << path;
		}
	}

	static bool LoadPathFromCache(const char* cache_filename, char* out_path, size_t out_size)
	{
		if (!out_path || out_size == 0 || !cache_filename) {
			return false;
		}
		char cache_path[MAX_PATH] = { 0 };
		if (!GetCacheFilePath(cache_filename, cache_path, sizeof(cache_path))) {
			return false;
		}
		std::ifstream ifs(cache_path);
		if (!ifs.is_open()) {
			return false;
		}
		std::string line;
		std::getline(ifs, line);
		if (line.empty()) {
			return false;
		}
		strncpy_s(out_path, out_size, line.c_str(), _TRUNCATE);
		return out_path[0] != '\0';
	}

	static void SaveSelectedOBJPathToCache(const char* path)
	{
		SavePathToCache("CameraProjection_LastOBJ.txt", path);
	}

	static void SaveSelectedJSONPathToCache(const char* path)
	{
		SavePathToCache("CameraProjection_LastJSON.txt", path);
	}

	static bool LoadSelectedOBJPathFromCache(char* out_path, size_t out_size)
	{
		return LoadPathFromCache("CameraProjection_LastOBJ.txt", out_path, out_size);
	}

	static bool LoadSelectedJSONPathFromCache(char* out_path, size_t out_size)
	{
		return LoadPathFromCache("CameraProjection_LastJSON.txt", out_path, out_size);
	}
#endif

	inline const char* ResolveMeshPath()
	{
		if (g_selected_obj_path[0] != '\0') {
			return g_selected_obj_path;
		}
#ifdef AE_OS_WIN
		if (LoadSelectedOBJPathFromCache(g_selected_obj_path, sizeof(g_selected_obj_path))) {
			return g_selected_obj_path;
		}
#endif
		const char* p = std::getenv("CAMPROJ_OBJ_PATH");
		if (p && p[0]) {
			return p;
		}
		p = std::getenv("CAMPROJ_MESH_PATH");
		if (p && p[0]) {
			return p;
		}

#ifdef AE_OS_WIN
		if (g_default_obj_path[0] != '\0') {
			return g_default_obj_path;
		}
		if (ResolveDefaultPluginDataFileA("data.obj", g_default_obj_path, sizeof(g_default_obj_path))) {
			return g_default_obj_path;
		}
#endif
		return nullptr;
	}

	inline const char* ResolveJSONPath()
	{
		if (g_selected_json_path[0] != '\0') {
			return g_selected_json_path;
		}
#ifdef AE_OS_WIN
		if (LoadSelectedJSONPathFromCache(g_selected_json_path, sizeof(g_selected_json_path))) {
			return g_selected_json_path;
		}
#endif
		const char* p = std::getenv("CAMPROJ_JSON_PATH");
		if (p && p[0]) {
			return p;
		}

#ifdef AE_OS_WIN
		if (g_default_json_path[0] != '\0') {
			return g_default_json_path;
		}
		if (ResolveDefaultPluginDataFileA("data.json", g_default_json_path, sizeof(g_default_json_path))) {
			return g_default_json_path;
		}
#endif
		return nullptr;
	}


	struct CameraContext {
		A_Matrix4	view_matrix;
		float		zoom;
		float		comp_width;
		float		comp_height;
		bool		valid;

		// false：使用 AE 相机矩阵路径
		// true ：使用 JSON 导出的相机 basis 路径
		bool		use_euler_view;

		// JSON 相机 basis 路径
		Vec3		camera_pos;
		bool		use_basis_vectors;
		Vec3		camera_right;
		Vec3		camera_up;
		Vec3		camera_forward;

		CameraContext()
			: zoom(1000.0f)
			, comp_width(1920.0f)
			, comp_height(1080.0f)
			, valid(false)
			, use_euler_view(false)
			, camera_pos(0.0f, 0.0f, 0.0f)
			, use_basis_vectors(false)
			, camera_right(1.0f, 0.0f, 0.0f)
			, camera_up(0.0f, 1.0f, 0.0f)
			, camera_forward(0.0f, 0.0f, 1.0f) {
			AEFX_CLR_STRUCT(view_matrix);
			view_matrix.mat[0][0] = 1.0;
			view_matrix.mat[1][1] = 1.0;
			view_matrix.mat[2][2] = 1.0;
			view_matrix.mat[3][3] = 1.0;
		}
	};

	static float ComputeProjectionCenterCost(const ProjectionStats& s, const CameraContext& camera)
	{
		if (!s.has_src) {
			return 1.0e30f;
		}

		const float cx = 0.5f * (s.src_min_x + s.src_max_x);
		const float cy = 0.5f * (s.src_min_y + s.src_max_y);
		const float dx = cx - camera.comp_width * 0.5f;
		const float dy = cy - camera.comp_height * 0.5f;
		return dx * dx + dy * dy;
	}

	struct Point4D {
		float p[4];
	};

	inline Point4D TransformPoint(const Point4D& in_pt, const A_Matrix4& m)
	{
		Point4D out_pt{};
		out_pt.p[0] = in_pt.p[0] * static_cast<float>(m.mat[0][0]) + in_pt.p[1] * static_cast<float>(m.mat[1][0]) + in_pt.p[2] * static_cast<float>(m.mat[2][0]) + in_pt.p[3] * static_cast<float>(m.mat[3][0]);
		out_pt.p[1] = in_pt.p[0] * static_cast<float>(m.mat[0][1]) + in_pt.p[1] * static_cast<float>(m.mat[1][1]) + in_pt.p[2] * static_cast<float>(m.mat[2][1]) + in_pt.p[3] * static_cast<float>(m.mat[3][1]);
		out_pt.p[2] = in_pt.p[0] * static_cast<float>(m.mat[0][2]) + in_pt.p[1] * static_cast<float>(m.mat[1][2]) + in_pt.p[2] * static_cast<float>(m.mat[2][2]) + in_pt.p[3] * static_cast<float>(m.mat[3][2]);
		out_pt.p[3] = in_pt.p[0] * static_cast<float>(m.mat[0][3]) + in_pt.p[1] * static_cast<float>(m.mat[1][3]) + in_pt.p[2] * static_cast<float>(m.mat[2][3]) + in_pt.p[3] * static_cast<float>(m.mat[3][3]);
		return out_pt;
	}

	static PF_Err InverseMatrix4(const A_Matrix4* m, A_Matrix4* resultP)
	{
		PF_Err err = PF_Err_NONE;

		A_FpLong d00, d01, d02, d03,
			d10, d11, d12, d13,
			d20, d21, d22, d23,
			d30, d31, d32, d33,
			m00, m01, m02, m03,
			m10, m11, m12, m13,
			m20, m21, m22, m23,
			m30, m31, m32, m33,
			D;

		m00 = m->mat[0][0];  m01 = m->mat[0][1];  m02 = m->mat[0][2];  m03 = m->mat[0][3];
		m10 = m->mat[1][0];  m11 = m->mat[1][1];  m12 = m->mat[1][2];  m13 = m->mat[1][3];
		m20 = m->mat[2][0];  m21 = m->mat[2][1];  m22 = m->mat[2][2];  m23 = m->mat[2][3];
		m30 = m->mat[3][0];  m31 = m->mat[3][1];  m32 = m->mat[3][2];  m33 = m->mat[3][3];

		d00 = m11 * m22 * m33 + m12 * m23 * m31 + m13 * m21 * m32 - m31 * m22 * m13 - m32 * m23 * m11 - m33 * m21 * m12;
		d01 = m10 * m22 * m33 + m12 * m23 * m30 + m13 * m20 * m32 - m30 * m22 * m13 - m32 * m23 * m10 - m33 * m20 * m12;
		d02 = m10 * m21 * m33 + m11 * m23 * m30 + m13 * m20 * m31 - m30 * m21 * m13 - m31 * m23 * m10 - m33 * m20 * m11;
		d03 = m10 * m21 * m32 + m11 * m22 * m30 + m12 * m20 * m31 - m30 * m21 * m12 - m31 * m22 * m10 - m32 * m20 * m11;

		d10 = m01 * m22 * m33 + m02 * m23 * m31 + m03 * m21 * m32 - m31 * m22 * m03 - m32 * m23 * m01 - m33 * m11 * m02;
		d11 = m00 * m22 * m33 + m02 * m23 * m30 + m03 * m20 * m32 - m30 * m22 * m03 - m32 * m23 * m00 - m33 * m20 * m02;
		d12 = m00 * m21 * m33 + m01 * m23 * m30 + m03 * m20 * m31 - m30 * m21 * m03 - m31 * m23 * m00 - m33 * m20 * m01;
		d13 = m00 * m21 * m32 + m01 * m22 * m30 + m02 * m20 * m31 - m30 * m21 * m02 - m31 * m22 * m00 - m32 * m20 * m01;

		d20 = m01 * m12 * m33 + m02 * m13 * m31 + m03 * m11 * m32 - m31 * m12 * m03 - m32 * m13 * m01 - m33 * m11 * m02;
		d21 = m00 * m12 * m33 + m02 * m13 * m30 + m03 * m10 * m32 - m30 * m12 * m03 - m32 * m13 * m00 - m33 * m10 * m02;
		d22 = m00 * m11 * m33 + m01 * m13 * m30 + m03 * m10 * m31 - m30 * m11 * m03 - m31 * m13 * m00 - m33 * m10 * m01;
		d23 = m00 * m11 * m32 + m01 * m12 * m30 + m02 * m10 * m31 - m30 * m11 * m02 - m31 * m12 * m00 - m32 * m10 * m01;

		d30 = m01 * m12 * m23 + m02 * m13 * m21 + m03 * m11 * m22 - m21 * m12 * m03 - m22 * m13 * m01 - m23 * m11 * m02;
		d31 = m00 * m12 * m23 + m02 * m13 * m20 + m03 * m10 * m22 - m20 * m12 * m03 - m22 * m13 * m00 - m23 * m10 * m02;
		d32 = m00 * m11 * m23 + m01 * m13 * m20 + m03 * m10 * m21 - m20 * m11 * m03 - m21 * m13 * m00 - m23 * m10 * m01;
		d33 = m00 * m11 * m22 + m01 * m12 * m20 + m02 * m10 * m21 - m20 * m11 * m02 - m21 * m12 * m00 - m22 * m10 * m01;

		D = m00 * d00 - m01 * d01 + m02 * d02 - m03 * d03;
		if (std::abs(static_cast<float>(D)) < 1e-12f) {
			return PF_Err_INTERNAL_STRUCT_DAMAGED;
		}

		resultP->mat[0][0] = d00 / D;   resultP->mat[0][1] = -d10 / D;  resultP->mat[0][2] = d20 / D;   resultP->mat[0][3] = -d30 / D;
		resultP->mat[1][0] = -d01 / D;  resultP->mat[1][1] = d11 / D;   resultP->mat[1][2] = -d21 / D;  resultP->mat[1][3] = d31 / D;
		resultP->mat[2][0] = d02 / D;   resultP->mat[2][1] = -d12 / D;  resultP->mat[2][2] = d22 / D;   resultP->mat[2][3] = -d32 / D;
		resultP->mat[3][0] = -d03 / D;  resultP->mat[3][1] = d13 / D;   resultP->mat[3][2] = -d23 / D;  resultP->mat[3][3] = d33 / D;
		return err;
	}

	static PF_Err BuildCameraContext(PF_InData* in_data, CameraContext& ctx)
	{
		PF_Err err = PF_Err_NONE;
		AEGP_SuiteHandler suites(in_data->pica_basicP);

		const float fallback_fov = 50.0f;
		ctx.comp_width = static_cast<float>(in_data->width);
		ctx.comp_height = static_cast<float>(in_data->height);
		ctx.zoom = (ctx.comp_height * 0.5f) / std::tan((fallback_fov * kPi / 180.0f) * 0.5f);
		ctx.valid = false;

		A_Time comp_time{};
		AEGP_CompH compH = nullptr;

		ERR(suites.PFInterfaceSuite1()->AEGP_ConvertEffectToCompTime(
			in_data->effect_ref,
			in_data->current_time,
			in_data->time_scale,
			&comp_time));

		if (!err) {
			AEGP_LayerH effect_layerH = nullptr;
			AEGP_ItemH comp_itemH = nullptr;
			A_long comp_w = 0;
			A_long comp_h = 0;

			ERR(suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(in_data->effect_ref, &effect_layerH));
			if (!err && effect_layerH) {
				ERR(suites.LayerSuite5()->AEGP_GetLayerParentComp(effect_layerH, &compH));
			}
			if (!err && compH) {
				ERR(suites.CompSuite4()->AEGP_GetItemFromComp(compH, &comp_itemH));
			}
			if (!err && comp_itemH) {
				ERR(suites.ItemSuite6()->AEGP_GetItemDimensions(comp_itemH, &comp_w, &comp_h));
			}
			if (!err && comp_w > 0 && comp_h > 0) {
				ctx.comp_width = static_cast<float>(comp_w);
				ctx.comp_height = static_cast<float>(comp_h);
			}
		}

		if (!err) {
			AEGP_LayerH camera_layerH = nullptr;
			ERR(suites.PFInterfaceSuite1()->AEGP_GetEffectCamera(in_data->effect_ref, &comp_time, &camera_layerH));

			if (!err && camera_layerH) {
				A_Matrix4 cam_to_world{};
				ERR(suites.LayerSuite5()->AEGP_GetLayerToWorldXform(camera_layerH, &comp_time, &cam_to_world));
				if (!err) {
					err = InverseMatrix4(&cam_to_world, &ctx.view_matrix);
				}
				if (!err) {
					AEGP_StreamVal stream_val{};
					err = suites.StreamSuite2()->AEGP_GetLayerStreamValue(
						camera_layerH,
						AEGP_LayerStream_ZOOM,
						AEGP_LTimeMode_CompTime,
						&comp_time,
						FALSE,
						&stream_val,
						nullptr);
					if (!err) {
						ctx.zoom = static_cast<float>(stream_val.one_d);
					}
				}
				ctx.valid = (err == PF_Err_NONE);
			}
		}

		if (err) {
			err = PF_Err_NONE;
		}

		return err;
	}

	// -----------------------------------------------------------------------
	// BuildSceneData: resolves all paths, parses JSON, loads the OBJ mesh.
	// Call this once at the start of both Render and ShowDebugPopup.
	// use_json_xform: when false the json_xform field is populated but the
	//                 caller decides whether to apply it.
	// -----------------------------------------------------------------------
	static void BuildSceneData(SceneData& sd)
	{
		sd.obj_path = ResolveMeshPath();
		sd.json_path = ResolveJSONPath();

		std::shared_ptr<const SceneCacheSnapshot> snapshot;

		{
			std::lock_guard<std::mutex> lock(g_scene_cache_mutex);
			if (SceneCacheMatchesPathsAndTimes(g_scene_cache, sd.obj_path, sd.json_path)) {
				snapshot = g_scene_cache;
			}
		}

		if (!snapshot) {
			std::shared_ptr<SceneCacheSnapshot> new_snapshot = std::make_shared<SceneCacheSnapshot>();
			new_snapshot->obj_path = sd.obj_path ? sd.obj_path : "";
			new_snapshot->json_path = sd.json_path ? sd.json_path : "";

			GetFileModificationTime(sd.obj_path, new_snapshot->obj_mtime, new_snapshot->obj_time_valid);
			GetFileModificationTime(sd.json_path, new_snapshot->json_mtime, new_snapshot->json_time_valid);

			if (sd.json_path && sd.json_path[0]) {
				new_snapshot->json_ok = CameraProjectionIO::ParseSceneJSONFile(sd.json_path, new_snapshot->json_data);
			}

			if (sd.obj_path && sd.obj_path[0]) {
				MeshData loaded_mesh;
				if (OBJParser::LoadOBJ(sd.obj_path, loaded_mesh)) {
					new_snapshot->meshP = std::make_shared<MeshData>(std::move(loaded_mesh));
					new_snapshot->mesh_ok = true;
				}
			}

			{
				std::lock_guard<std::mutex> lock(g_scene_cache_mutex);
				if (SceneCacheMatchesPathsAndTimes(g_scene_cache, sd.obj_path, sd.json_path)) {
					snapshot = g_scene_cache;
				}
				else {
					g_scene_cache = new_snapshot;
					snapshot = new_snapshot;
				}
			}
		}

		if (!snapshot) {
			return;
		}

		sd.json_ok = snapshot->json_ok;
		if (sd.json_ok) {
			const CameraProjectionIO::SceneJsonData& raw_json = snapshot->json_data;

			sd.has_transform = raw_json.has_transform;

			sd.has_camera = raw_json.has_camera;
			sd.has_camera_fov = raw_json.has_camera_fov;
			sd.has_camera_basis = raw_json.has_camera_basis;

			sd.cam_pos = raw_json.camera_pos;
			sd.cam_right = raw_json.camera_right;
			sd.cam_up = raw_json.camera_up;
			sd.cam_forward = raw_json.camera_forward;
			sd.camera_fov_deg = raw_json.camera_fov_deg;

			if (raw_json.has_transform) {
				sd.json_xform.pos = raw_json.position;
				sd.json_xform.rot_deg = raw_json.rotation;
				sd.json_xform.scale = raw_json.scale;
			}
		}

		sd.meshP = snapshot->meshP;
		sd.mesh_ok = snapshot->mesh_ok && static_cast<bool>(sd.meshP);
	}

	// -----------------------------------------------------------------------
	// ApplyCameraFromJSON: applies the JSON camera data into an already-
	// constructed CameraContext (which may have been built from an AE camera).
	// Returns true if the JSON camera was actually applied.
	// out_path records what rotation method was used for debugging.
	// -----------------------------------------------------------------------
	static bool ApplyCameraFromJSON(const SceneData& sd,
		CameraContext& cam,
		RenderPathInfo* out_path)
	{
		if (!sd.json_ok || !sd.has_camera || !sd.has_camera_basis) {
			return false;
		}

		cam.use_euler_view = true;
		cam.camera_pos = sd.cam_pos;
		cam.use_basis_vectors = true;
		cam.camera_right = sd.cam_right.normalize();
		cam.camera_up = sd.cam_up.normalize();
		cam.camera_forward = sd.cam_forward.normalize();

		if (sd.has_camera_fov && sd.camera_fov_deg > 1.0f && sd.camera_fov_deg < 179.0f) {
			const float aspect_ratio = cam.comp_width / cam.comp_height;
			const float hfov_rad = sd.camera_fov_deg * kPi / 180.0f;
			const float vfov_rad = 2.0f * std::atan(std::tan(hfov_rad * 0.5f) / aspect_ratio);
			cam.zoom = (cam.comp_height * 0.5f) / std::tan(vfov_rad * 0.5f);
		}

		cam.valid = true;

		if (out_path) {
			out_path->used_json_camera = true;
			out_path->used_ae_camera = false;
		}

		return true;
	}

	// -----------------------------------------------------------------------
	// BuildEffectiveCamera: builds the CameraContext that will actually be
	// used for rendering.  Encapsulates the AE-camera + JSON-fallback logic
	// shared between Render and ShowDebugPopup.
	//
	// Returns true if the JSON camera was used as fallback.
	// -----------------------------------------------------------------------
	static bool BuildEffectiveCamera(PF_InData* in_data,
		const SceneData& sd,
		CameraContext& cam,
		RenderPathInfo* out_path)
	{
		// 先构建 AE 相机上下文。
		// 目的不是优先使用 AE，而是无论最终是否被 JSON 覆盖，
		// 都先拿到“真实合成尺寸(comp_width/comp_height)”。
		// 否则 JSON FOV -> zoom 的换算会错误，Show Mesh 视角也会失真。
		BuildCameraContext(in_data, cam);

		const bool ae_valid = cam.valid;

		// JSON 相机是主路径：
		// 如果 JSON 中存在已验证通过的 basis 数据，就覆盖 AE 相机姿态和 zoom。
		if (ApplyCameraFromJSON(sd, cam, out_path)) {
			if (out_path) {
				out_path->used_json_camera = true;
				out_path->used_ae_camera = false;
			}
			return true;
		}

		// JSON 不可用时，退回 AE 相机矩阵路径。
		if (out_path) {
			out_path->used_json_camera = false;
			out_path->used_ae_camera = ae_valid;
		}

		return false;
	}

	inline Vec3 ApplyObjectTransform(const Vec3& in_p, const ObjectTransform& t)
	{
		Vec3 p = ConvertPointFromOBJToRender(in_p);

		p.x *= t.scale.x;
		p.y *= t.scale.y;
		p.z *= t.scale.z;

		const float rx = t.rot_deg.x * kPi / 180.0f;
		const float ry = t.rot_deg.y * kPi / 180.0f;
		const float rz = t.rot_deg.z * kPi / 180.0f;

		const float cx = std::cos(rx), sx = std::sin(rx);
		const float cy = std::cos(ry), sy = std::sin(ry);
		const float cz = std::cos(rz), sz = std::sin(rz);

		{
			const float y = p.y * cx - p.z * sx;
			const float z = p.y * sx + p.z * cx;
			p.y = y;
			p.z = z;
		}
		{
			const float x = p.x * cy + p.z * sy;
			const float z = -p.x * sy + p.z * cy;
			p.x = x;
			p.z = z;
		}
		{
			const float x = p.x * cz - p.y * sz;
			const float y = p.x * sz + p.y * cz;
			p.x = x;
			p.y = y;
		}

		p.x += t.pos.x;
		p.y += t.pos.y;
		p.z += t.pos.z;
		return p;
	}

	inline bool ProjectWorldToSource(const Vec3& p,
		const CameraContext& cam,
		bool use_negative_z,
		float& sx,
		float& sy,
		float& depth)
	{
		float vx = 0.0f;
		float vy = 0.0f;
		float vz = 0.0f;

		if (cam.use_euler_view) {
			// JSON 主路径：
			// JSON 已直接导出相机 basis（right/up/forward），
			// 这里严禁再从 target / euler / quaternion 重建相机朝向，
			// 否则很容易再次引入轴向错误。
			const Vec3 local(
				p.x - cam.camera_pos.x,
				p.y - cam.camera_pos.y,
				p.z - cam.camera_pos.z);

			vx = local.dot(cam.camera_right);
			vy = local.dot(cam.camera_up);
			vz = -local.dot(cam.camera_forward);
		}
		else {
			// AE fallback 路径：
			// 直接使用 AE 提供的 view matrix。
			const Point4D world{ {p.x, p.y, p.z, 1.0f} };
			const Point4D view = TransformPoint(world, cam.view_matrix);
			vx = view.p[0];
			vy = view.p[1];
			vz = view.p[2];
		}

		const float z = vz;
		float denom = use_negative_z ? -z : z;
		const bool valid = denom > 1e-4f;
		if (!valid) {
			denom = 1e-4f;
		}

		// 统一透视投影：
		// zoom 必须基于真实 comp 尺寸计算，否则 Show Mesh 和投影结果都会失真。
		sx = vx * cam.zoom / denom + cam.comp_width * 0.5f;
		sy = -vy * cam.zoom / denom + cam.comp_height * 0.5f;
		depth = denom;
		return valid;
	}

	inline void MapCompToInputPoint(const CameraContext& cam,
		const PF_LayerDef* input,
		float comp_x,
		float comp_y,
		float& input_x,
		float& input_y)
	{
		if (!input || input->width <= 0 || input->height <= 0) {
			input_x = comp_x;
			input_y = comp_y;
			return;
		}

		const float comp_w = (cam.comp_width > 1.0f) ? cam.comp_width : 1.0f;
		const float comp_h = (cam.comp_height > 1.0f) ? cam.comp_height : 1.0f;

		const float in_w = (input->width > 1) ? static_cast<float>(input->width) : 1.0f;
		const float in_h = (input->height > 1) ? static_cast<float>(input->height) : 1.0f;

		// ProjectWorldToSource 返回的是 comp/screen 空间坐标；
		// 采样 input 时必须映射到 input layer 像素空间。
		input_x = comp_x * (in_w / comp_w);
		input_y = comp_y * (in_h / comp_h);
	}

	template <typename PIXEL>
	inline PIXEL ReadPixelNearest(const PF_LayerDef* world, int x, int y)
	{
		const int sx = ProjectionMath::ClampInt(x, 0, world->width - 1);
		const int sy = ProjectionMath::ClampInt(y, 0, world->height - 1);
		const PIXEL* row = reinterpret_cast<const PIXEL*>(reinterpret_cast<const char*>(world->data) + sy * world->rowbytes);
		return row[sx];
	}

	template <typename CHANNEL>
	inline CHANNEL FloatToChannelClamped(float v)
	{
		const float mx = static_cast<float>(std::numeric_limits<CHANNEL>::max());
		v = ProjectionMath::Clamp(v, 0.0f, mx);
		return static_cast<CHANNEL>(std::lround(v));
	}

	template <typename PIXEL>
	inline PIXEL LerpPixel(const PIXEL& a, const PIXEL& b, float t)
	{
		PIXEL c{};
		c.red = FloatToChannelClamped<decltype(c.red)>(
			static_cast<float>(a.red) + (static_cast<float>(b.red) - static_cast<float>(a.red)) * t);
		c.green = FloatToChannelClamped<decltype(c.green)>(
			static_cast<float>(a.green) + (static_cast<float>(b.green) - static_cast<float>(a.green)) * t);
		c.blue = FloatToChannelClamped<decltype(c.blue)>(
			static_cast<float>(a.blue) + (static_cast<float>(b.blue) - static_cast<float>(a.blue)) * t);
		c.alpha = FloatToChannelClamped<decltype(c.alpha)>(
			static_cast<float>(a.alpha) + (static_cast<float>(b.alpha) - static_cast<float>(a.alpha)) * t);
		return c;
	}

	template <typename PIXEL>
	inline PIXEL ReadPixelBilinear(const PF_LayerDef* world, float x, float y)
	{
		const float fx = ProjectionMath::Clamp(x, 0.0f, static_cast<float>(world->width - 1));
		const float fy = ProjectionMath::Clamp(y, 0.0f, static_cast<float>(world->height - 1));

		const int x0 = ProjectionMath::ClampInt(static_cast<int>(std::floor(fx)), 0, world->width - 1);
		const int y0 = ProjectionMath::ClampInt(static_cast<int>(std::floor(fy)), 0, world->height - 1);
		const int x1 = ProjectionMath::ClampInt(x0 + 1, 0, world->width - 1);
		const int y1 = ProjectionMath::ClampInt(y0 + 1, 0, world->height - 1);

		const float tx = fx - static_cast<float>(x0);
		const float ty = fy - static_cast<float>(y0);

		const PIXEL c00 = ReadPixelNearest<PIXEL>(world, x0, y0);
		const PIXEL c10 = ReadPixelNearest<PIXEL>(world, x1, y0);
		const PIXEL c01 = ReadPixelNearest<PIXEL>(world, x0, y1);
		const PIXEL c11 = ReadPixelNearest<PIXEL>(world, x1, y1);

		const PIXEL cx0 = LerpPixel(c00, c10, tx);
		const PIXEL cx1 = LerpPixel(c01, c11, tx);
		return LerpPixel(cx0, cx1, ty);
	}

	template <typename PIXEL>
	inline void WritePixel(PF_LayerDef* world, int x, int y, const PIXEL& c)
	{
		PIXEL* row = reinterpret_cast<PIXEL*>(reinterpret_cast<char*>(world->data) + y * world->rowbytes);
		row[x] = c;
	}

	template <typename PIXEL>
	inline void ClearWorld(PF_LayerDef* world)
	{
		PIXEL zero{};
		for (int y = 0; y < world->height; ++y) {
			PIXEL* row = reinterpret_cast<PIXEL*>(reinterpret_cast<char*>(world->data) + y * world->rowbytes);
			for (int x = 0; x < world->width; ++x) {
				row[x] = zero;
			}
		}
	}

	template <typename PIXEL>
	inline void MakeOpaque(PIXEL& c)
	{
		c.alpha = static_cast<decltype(c.alpha)>(std::numeric_limits<decltype(c.alpha)>::max());
	}

	template <typename PIXEL>
	inline void FillSolid(PF_LayerDef* world,
		typename std::remove_reference<decltype(PIXEL().red)>::type r,
		typename std::remove_reference<decltype(PIXEL().green)>::type g,
		typename std::remove_reference<decltype(PIXEL().blue)>::type b)
	{
		PIXEL c{};
		c.red = r;
		c.green = g;
		c.blue = b;
		c.alpha = static_cast<decltype(c.alpha)>(std::numeric_limits<decltype(c.alpha)>::max());
		for (int y = 0; y < world->height; ++y) {
			PIXEL* row = reinterpret_cast<PIXEL*>(reinterpret_cast<char*>(world->data) + y * world->rowbytes);
			for (int x = 0; x < world->width; ++x) {
				row[x] = c;
			}
		}
	}

	template <typename PIXEL>
	inline void DrawUVLine(PF_LayerDef* output,
		int canvas_w,
		int canvas_h,
		int x0,
		int y0,
		int x1,
		int y1,
		A_long& written_pixels)
	{
		PIXEL c{};
		c.red = static_cast<decltype(c.red)>(std::numeric_limits<decltype(c.red)>::max());
		c.green = static_cast<decltype(c.green)>(0);
		c.blue = static_cast<decltype(c.blue)>(0);
		c.alpha = static_cast<decltype(c.alpha)>(std::numeric_limits<decltype(c.alpha)>::max());

		const int dx = std::abs(x1 - x0);
		const int sx = (x0 < x1) ? 1 : -1;
		const int dy = -std::abs(y1 - y0);
		const int sy = (y0 < y1) ? 1 : -1;
		int err = dx + dy;

		for (;;) {
			if (x0 >= 0 && y0 >= 0 && x0 < canvas_w && y0 < canvas_h) {
				WritePixel<PIXEL>(output, x0, y0, c);
				++written_pixels;
			}
			if (x0 == x1 && y0 == y1) {
				break;
			}
			const int e2 = 2 * err;
			if (e2 >= dy) {
				err += dy;
				x0 += sx;
			}
			if (e2 <= dx) {
				err += dx;
				y0 += sy;
			}
		}
	}

	inline Vec3 InterpolateVec3Barycentric(const Vec3& a,
		const Vec3& b,
		const Vec3& c,
		const Vec3& bary)
	{
		return a * bary.x + b * bary.y + c * bary.z;
	}

	template <typename PIXEL>
	static PF_Err RenderProjection(const PF_LayerDef* input,
		PF_LayerDef* output,
		const MeshData& mesh,
		const CameraContext& camera,
		const ObjectTransform& obj_xform,
		A_long target_w,
		A_long target_h,
		A_Boolean wireframe,
		A_Boolean use_negative_z,
		A_long* written_pixelsPL)
	{
		const int out_w = output ? output->width : 0;
		const int out_h = output ? output->height : 0;
		if (!input || !output || !input->data || !output->data ||
			out_w <= 0 || out_h <= 0 ||
			input->width <= 0 || input->height <= 0) {
			if (written_pixelsPL) {
				*written_pixelsPL = 0;
			}
			return PF_Err_NONE;
		}

		const int canvas_w = ProjectionMath::ClampInt(static_cast<int>(target_w), 1, out_w);
		const int canvas_h = ProjectionMath::ClampInt(static_cast<int>(target_h), 1, out_h);

		ClearWorld<PIXEL>(output);
		A_long written_pixels = 0;

		std::vector<float> zbuffer(
			static_cast<size_t>(canvas_w) * static_cast<size_t>(canvas_h),
			std::numeric_limits<float>::infinity());

		for (const Face& face : mesh.faces) {
			if (face.v[0] < 0 || face.v[1] < 0 || face.v[2] < 0) continue;
			if (face.vt[0] < 0 || face.vt[1] < 0 || face.vt[2] < 0) continue;

			if (face.v[0] >= static_cast<int>(mesh.vertices.size()) ||
				face.v[1] >= static_cast<int>(mesh.vertices.size()) ||
				face.v[2] >= static_cast<int>(mesh.vertices.size())) continue;

			if (face.vt[0] >= static_cast<int>(mesh.uvs.size()) ||
				face.vt[1] >= static_cast<int>(mesh.uvs.size()) ||
				face.vt[2] >= static_cast<int>(mesh.uvs.size())) continue;

			const Vec3 p0 = ApplyObjectTransform(mesh.vertices[face.v[0]], obj_xform);
			const Vec3 p1 = ApplyObjectTransform(mesh.vertices[face.v[1]], obj_xform);
			const Vec3 p2 = ApplyObjectTransform(mesh.vertices[face.v[2]], obj_xform);

			const Vec2 uv0 = mesh.uvs[face.vt[0]];
			const Vec2 uv1 = mesh.uvs[face.vt[1]];
			const Vec2 uv2 = mesh.uvs[face.vt[2]];

			const Vec2 t0(uv0.x * (canvas_w - 1), (1.0f - uv0.y) * (canvas_h - 1));
			const Vec2 t1(uv1.x * (canvas_w - 1), (1.0f - uv1.y) * (canvas_h - 1));
			const Vec2 t2(uv2.x * (canvas_w - 1), (1.0f - uv2.y) * (canvas_h - 1));

			if (wireframe) {
				DrawUVLine<PIXEL>(output, canvas_w, canvas_h,
					static_cast<int>(std::lround(t0.x)), static_cast<int>(std::lround(t0.y)),
					static_cast<int>(std::lround(t1.x)), static_cast<int>(std::lround(t1.y)),
					written_pixels);
				DrawUVLine<PIXEL>(output, canvas_w, canvas_h,
					static_cast<int>(std::lround(t1.x)), static_cast<int>(std::lround(t1.y)),
					static_cast<int>(std::lround(t2.x)), static_cast<int>(std::lround(t2.y)),
					written_pixels);
				DrawUVLine<PIXEL>(output, canvas_w, canvas_h,
					static_cast<int>(std::lround(t2.x)), static_cast<int>(std::lround(t2.y)),
					static_cast<int>(std::lround(t0.x)), static_cast<int>(std::lround(t0.y)),
					written_pixels);
			}

			float sx0 = 0.0f, sy0 = 0.0f, dz0 = 0.0f;
			float sx1 = 0.0f, sy1 = 0.0f, dz1 = 0.0f;
			float sx2 = 0.0f, sy2 = 0.0f, dz2 = 0.0f;

			const bool v0 = ProjectWorldToSource(p0, camera, use_negative_z ? true : false, sx0, sy0, dz0);
			const bool v1 = ProjectWorldToSource(p1, camera, use_negative_z ? true : false, sx1, sy1, dz1);
			const bool v2 = ProjectWorldToSource(p2, camera, use_negative_z ? true : false, sx2, sy2, dz2);

			if (!(v0 || v1 || v2)) {
				continue;
			}

			const int min_x = ProjectionMath::ClampInt(
				static_cast<int>(std::floor(Min3(t0.x, t1.x, t2.x))), 0, canvas_w - 1);
			const int min_y = ProjectionMath::ClampInt(
				static_cast<int>(std::floor(Min3(t0.y, t1.y, t2.y))), 0, canvas_h - 1);
			const int max_x = ProjectionMath::ClampInt(
				static_cast<int>(std::ceil(Max3(t0.x, t1.x, t2.x))), 0, canvas_w - 1);
			const int max_y = ProjectionMath::ClampInt(
				static_cast<int>(std::ceil(Max3(t0.y, t1.y, t2.y))), 0, canvas_h - 1);

			for (int y = min_y; y <= max_y; ++y) {
				for (int x = min_x; x <= max_x; ++x) {
					const Vec2 uv_p(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
					const Vec3 bary = ProjectionMath::Barycentric(uv_p, t0, t1, t2);
					if (!ProjectionMath::IsInsideTriangle(bary)) {
						continue;
					}

					const Vec3 world_p = InterpolateVec3Barycentric(p0, p1, p2, bary);

					float comp_src_x = 0.0f;
					float comp_src_y = 0.0f;
					float depth = 0.0f;
					if (!ProjectWorldToSource(world_p, camera, use_negative_z ? true : false, comp_src_x, comp_src_y, depth)) {
						continue;
					}

					if (!std::isfinite(comp_src_x) || !std::isfinite(comp_src_y) || !std::isfinite(depth)) {
						continue;
					}

					float input_src_x = 0.0f;
					float input_src_y = 0.0f;
					MapCompToInputPoint(camera, input, comp_src_x, comp_src_y, input_src_x, input_src_y);

					const size_t zb_idx =
						static_cast<size_t>(y) * static_cast<size_t>(canvas_w) + static_cast<size_t>(x);

					if (depth >= zbuffer[zb_idx]) {
						continue;
					}
					zbuffer[zb_idx] = depth;

					PIXEL c = ReadPixelBilinear<PIXEL>(
						input,
						input_src_x,
						input_src_y);

					MakeOpaque(c);

					if (wireframe) {
						const float e = Min3(bary.x, bary.y, bary.z);
						if (e < 0.02f) {
							c.red = static_cast<decltype(c.red)>(std::numeric_limits<decltype(c.red)>::max());
							c.green = static_cast<decltype(c.green)>(0);
							c.blue = static_cast<decltype(c.blue)>(0);
							MakeOpaque(c);
						}
					}

					if (x < out_w && y < out_h) {
						WritePixel<PIXEL>(output, x, y, c);
						++written_pixels;
					}
				}
			}
		}

		if (written_pixelsPL) {
			*written_pixelsPL = written_pixels;
		}
		return PF_Err_NONE;
	}

	template <typename PIXEL>
	static PF_Err RenderCameraMeshWireframe(PF_LayerDef* output,
		const MeshData& mesh,
		const CameraContext& camera,
		const ObjectTransform& obj_xform,
		A_Boolean use_negative_z,
		A_long* written_pixelsPL)
	{
		if (!output || output->width <= 0 || output->height <= 0) {
			if (written_pixelsPL) {
				*written_pixelsPL = 0;
			}
			return PF_Err_NONE;
		}

		ClearWorld<PIXEL>(output);
		A_long written_pixels = 0;

		const float sx_scale = (camera.comp_width > 1.0f)
			? (static_cast<float>(output->width - 1) / (camera.comp_width - 1.0f))
			: 1.0f;
		const float sy_scale = (camera.comp_height > 1.0f)
			? (static_cast<float>(output->height - 1) / (camera.comp_height - 1.0f))
			: 1.0f;

		for (const Face& face : mesh.faces) {
			if (face.v[0] < 0 || face.v[1] < 0 || face.v[2] < 0) continue;
			if (face.v[0] >= static_cast<int>(mesh.vertices.size()) ||
				face.v[1] >= static_cast<int>(mesh.vertices.size()) ||
				face.v[2] >= static_cast<int>(mesh.vertices.size())) continue;

			Vec3 p[3] = {
				ApplyObjectTransform(mesh.vertices[face.v[0]], obj_xform),
				ApplyObjectTransform(mesh.vertices[face.v[1]], obj_xform),
				ApplyObjectTransform(mesh.vertices[face.v[2]], obj_xform)
			};

			float sx[3] = { 0.0f, 0.0f, 0.0f };
			float sy[3] = { 0.0f, 0.0f, 0.0f };
			float dz[3] = { 0.0f, 0.0f, 0.0f };
			bool valid[3] = {
				ProjectWorldToSource(p[0], camera, use_negative_z, sx[0], sy[0], dz[0]),
				ProjectWorldToSource(p[1], camera, use_negative_z, sx[1], sy[1], dz[1]),
				ProjectWorldToSource(p[2], camera, use_negative_z, sx[2], sy[2], dz[2])
			};

			const int idx[3][2] = { {0, 1}, {1, 2}, {2, 0} };
			for (int e = 0; e < 3; ++e) {
				const int a = idx[e][0];
				const int b = idx[e][1];
				if (!(valid[a] && valid[b])) continue;

				const int x0 = static_cast<int>(std::lround(sx[a] * sx_scale));
				const int y0 = static_cast<int>(std::lround(sy[a] * sy_scale));
				const int x1 = static_cast<int>(std::lround(sx[b] * sx_scale));
				const int y1 = static_cast<int>(std::lround(sy[b] * sy_scale));
				DrawUVLine<PIXEL>(output, output->width, output->height, x0, y0, x1, y1, written_pixels);
			}
		}

		if (written_pixelsPL) {
			*written_pixelsPL = written_pixels;
		}
		return PF_Err_NONE;
	}

	static PF_Err About(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
	{
		AEGP_SuiteHandler suites(in_data->pica_basicP);
		suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg,
			"%s v%d.%d\r%s",
			STR(StrID_Name),
			MAJOR_VERSION,
			MINOR_VERSION,
			STR(StrID_Description));
		return PF_Err_NONE;
	}

	static PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
	{
		out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
		out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE;
		out_data->out_flags2 |= PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
		return PF_Err_NONE;
	}

	static PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
	{
		PF_Err err = PF_Err_NONE;
		PF_ParamDef def;

		PF_ADD_BUTTON(STR(StrID_SelectOBJ_Param_Name), STR(StrID_SelectOBJ_Button_Name), PF_PUI_STD_CONTROL_ONLY, PF_ParamFlag_SUPERVISE, SELECT_OBJ_DISK_ID);
		PF_ADD_BUTTON(STR(StrID_SelectJSON_Param_Name), STR(StrID_SelectJSON_Button_Name), PF_PUI_STD_CONTROL_ONLY, PF_ParamFlag_SUPERVISE, SELECT_JSON_DISK_ID);
		PF_ADD_BUTTON(STR(StrID_DumpInfo_Param_Name), STR(StrID_DumpInfo_Button_Name), PF_PUI_STD_CONTROL_ONLY, PF_ParamFlag_SUPERVISE, DUMP_INFO_DISK_ID);

		AEFX_CLR_STRUCT(def);
		PF_ADD_CHECKBOXX(STR(StrID_DebugStatus_Param_Name), FALSE, 0, DEBUG_STATUS_DISK_ID);

		AEFX_CLR_STRUCT(def);
		PF_ADD_CHECKBOXX(STR(StrID_ShowWireframe_Param_Name), FALSE, 0, SHOW_WIREFRAME_DISK_ID);

		AEFX_CLR_STRUCT(def);
		PF_ADD_CHECKBOXX("Show Mesh", FALSE, 0, SHOW_MESH_DISK_ID);

		AEFX_CLR_STRUCT(def);
		PF_ADD_CHECKBOXX("Use JSON Transform", TRUE, 0, USE_JSON_XFORM_DISK_ID);

		out_data->num_params = CAMPROJ_NUM_PARAMS_EXT;
		return err;
	}

#ifdef AE_OS_WIN
	static void ShowDebugPopup(PF_InData* in_data, PF_ParamDef* params[]);
#endif

	static PF_Err UserChangedParam(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], void* extra)
	{
		PF_Err err = PF_Err_NONE;
		PF_UserChangedParamExtra* which = reinterpret_cast<PF_UserChangedParamExtra*>(extra);
		if (!which) {
			return err;
		}

		if (which->param_index == CAMPROJ_SELECT_OBJ) {
#ifdef AE_OS_WIN
			char path_buf[AEGP_MAX_PATH_SIZE] = { 0 };
			if (SelectOBJPathDialog(path_buf, sizeof(path_buf))) {
				strncpy_s(g_selected_obj_path, sizeof(g_selected_obj_path), path_buf, _TRUNCATE);
				SaveSelectedOBJPathToCache(g_selected_obj_path);
			}
#endif
			out_data->out_flags |= PF_OutFlag_REFRESH_UI;
		}
		else if (which->param_index == CAMPROJ_SELECT_JSON) {
#ifdef AE_OS_WIN
			char path_buf[AEGP_MAX_PATH_SIZE] = { 0 };
			if (SelectJSONPathDialog(path_buf, sizeof(path_buf))) {
				strncpy_s(g_selected_json_path, sizeof(g_selected_json_path), path_buf, _TRUNCATE);
				SaveSelectedJSONPathToCache(g_selected_json_path);
			}
#endif
			out_data->out_flags |= PF_OutFlag_REFRESH_UI;
		}
		else if (which->param_index == CAMPROJ_DUMP_INFO) {
#ifdef AE_OS_WIN
			ShowDebugPopup(in_data, params);
#endif
		}

		return err;
	}



	// -----------------------------------------------------------------------
	// ShowDebugPopup: diagnostics popup triggered by the "Dump Info" button.
	// All heavy logic is delegated to BuildSceneData / BuildEffectiveCamera.
	// The popup also shows the ACTIVE RENDER PATH (which combination of
	// axis / z-sign / xform was selected) under the current user parameters.
	// -----------------------------------------------------------------------
#ifdef AE_OS_WIN
	static void ShowDebugPopup(PF_InData* in_data, PF_ParamDef* params[])
	{
		const bool use_json_xform = (params[CAMPROJ_USE_JSON_XFORM] != nullptr)
			? (params[CAMPROJ_USE_JSON_XFORM]->u.bd.value ? true : false)
			: true;
		const bool show_mesh = (params[CAMPROJ_SHOW_MESH] != nullptr)
			? (params[CAMPROJ_SHOW_MESH]->u.bd.value ? true : false)
			: false;

		SceneData sd;
		BuildSceneData(sd);

		CameraContext camera;
		const bool ae_cam_valid_before_fallback = [&]() {
			CameraContext probe;
			BuildCameraContext(in_data, probe);
			return probe.valid;
			}();

		const bool used_json_camera = BuildEffectiveCamera(
			in_data,
			sd,
			camera,
			nullptr);

		const bool effective_uses_json_xform =
			(sd.json_ok && sd.has_transform && use_json_xform);

		const ObjectTransform effective_xform =
			effective_uses_json_xform ? sd.json_xform : ObjectTransform();

		float bb_min_x = 0.0f, bb_min_y = 0.0f, bb_min_z = 0.0f;
		float bb_max_x = 0.0f, bb_max_y = 0.0f, bb_max_z = 0.0f;
		if (sd.mesh_ok && sd.meshP && !sd.meshP->vertices.empty()) {
			bb_min_x = bb_max_x = sd.meshP->vertices[0].x;
			bb_min_y = bb_max_y = sd.meshP->vertices[0].y;
			bb_min_z = bb_max_z = sd.meshP->vertices[0].z;
			for (const auto& v : sd.meshP->vertices) {
				bb_min_x = std::min(bb_min_x, v.x); bb_max_x = std::max(bb_max_x, v.x);
				bb_min_y = std::min(bb_min_y, v.y); bb_max_y = std::max(bb_max_y, v.y);
				bb_min_z = std::min(bb_min_z, v.z); bb_max_z = std::max(bb_max_z, v.z);
			}
		}

		struct Probe {
			bool ok = false;
			float sx = 0.0f;
			float sy = 0.0f;
			float d = 0.0f;
		};

		struct BasisDebug {
			Probe center;
			Probe px;
			Probe py;
			Probe pz;
		};

		auto angle_deg = [](float dx, float dy) -> float {
			return static_cast<float>(std::atan2(dy, dx) * 180.0 / kPi);
			};

		auto project_probe_for = [&](const CameraContext& cam_src, const Vec3& p, bool use_negative_z) -> Probe {
			Probe r;
			r.ok = ProjectWorldToSource(p, cam_src, use_negative_z, r.sx, r.sy, r.d);
			return r;
			};

		auto collect_basis = [&](const CameraContext& cam_src, bool use_negative_z) -> BasisDebug {
			BasisDebug b;
			if (!sd.mesh_ok || !sd.meshP || !cam_src.valid || sd.meshP->vertices.empty()) {
				return b;
			}

			const Vec3 c_local(
				0.5f * (bb_min_x + bb_max_x),
				0.5f * (bb_min_y + bb_max_y),
				0.5f * (bb_min_z + bb_max_z));

			const float step = 10.0f;
			const Vec3 cc = ApplyObjectTransform(c_local, effective_xform);
			const Vec3 px = ApplyObjectTransform(c_local + Vec3(step, 0.0f, 0.0f), effective_xform);
			const Vec3 py = ApplyObjectTransform(c_local + Vec3(0.0f, step, 0.0f), effective_xform);
			const Vec3 pz = ApplyObjectTransform(c_local + Vec3(0.0f, 0.0f, step), effective_xform);

			b.center = project_probe_for(cam_src, cc, use_negative_z);
			b.px = project_probe_for(cam_src, px, use_negative_z);
			b.py = project_probe_for(cam_src, py, use_negative_z);
			b.pz = project_probe_for(cam_src, pz, use_negative_z);
			return b;
			};

		const BasisDebug basis_neg = collect_basis(camera, true);
		const BasisDebug basis_pos = collect_basis(camera, false);

		const float neg_ang_x = angle_deg(
			basis_neg.px.sx - basis_neg.center.sx,
			basis_neg.px.sy - basis_neg.center.sy);

		const float neg_ang_y = angle_deg(
			basis_neg.py.sx - basis_neg.center.sx,
			basis_neg.py.sy - basis_neg.center.sy);

		const float neg_ang_z = angle_deg(
			basis_neg.pz.sx - basis_neg.center.sx,
			basis_neg.pz.sy - basis_neg.center.sy);

		const char* cam_mode = "AE Matrix";
		if (camera.use_euler_view) {
			cam_mode = camera.use_basis_vectors ? "JSON Camera Basis" : "JSON Camera";
		}
		const char* cam_source = used_json_camera ? "JSON Camera" : "AE Fallback";

		float fov_from_zoom = 0.0f;
		if (camera.zoom > 1e-5f) {
			fov_from_zoom = 2.0f * std::atan((camera.comp_height * 0.5f) / camera.zoom) * 180.0f / kPi;
		}

		ProjectionStats stats_json_neg;
		ProjectionStats stats_identity_neg;
		ProjectionStats stats_json_pos;
		ProjectionStats stats_identity_pos;

		float cost_json_neg = 1.0e30f;
		float cost_identity_neg = 1.0e30f;
		float cost_json_pos = 1.0e30f;
		float cost_identity_pos = 1.0e30f;

		if (sd.mesh_ok && sd.meshP && camera.valid) {
			stats_json_neg = CollectProjectionStats(
				*sd.meshP,
				camera,
				effective_xform,
				true,
				&params[CAMPROJ_INPUT]->u.ld);
			cost_json_neg = ComputeProjectionCenterCost(stats_json_neg, camera);

			stats_json_pos = CollectProjectionStats(
				*sd.meshP,
				camera,
				effective_xform,
				false,
				&params[CAMPROJ_INPUT]->u.ld);
			cost_json_pos = ComputeProjectionCenterCost(stats_json_pos, camera);

			if (sd.has_transform) {
				stats_identity_neg = CollectProjectionStats(
					*sd.meshP,
					camera,
					ObjectTransform(),
					true,
					&params[CAMPROJ_INPUT]->u.ld);
				cost_identity_neg = ComputeProjectionCenterCost(stats_identity_neg, camera);

				stats_identity_pos = CollectProjectionStats(
					*sd.meshP,
					camera,
					ObjectTransform(),
					false,
					&params[CAMPROJ_INPUT]->u.ld);
				cost_identity_pos = ComputeProjectionCenterCost(stats_identity_pos, camera);
			}
		}

		auto format_stats_line = [](const char* label, const ProjectionStats& st, float cost) -> std::string {
			char buf[1024] = { 0 };
			snprintf(buf, sizeof(buf),
				"  %-22s faces=%ld/%ld  projFaces=%ld  projVerts=%ld/%ld  insideSrc=%ld  centerCost=%.2f",
				label,
				st.faces_index_ok,
				st.faces_total,
				st.faces_projectable,
				st.verts_projected,
				st.verts_total,
				st.verts_inside_src,
				cost);
			return std::string(buf);
			};

		std::string recommended_path = "No useful projection path";
		if (camera.use_euler_view && camera.use_basis_vectors) {
			if (stats_json_neg.faces_projectable > 0 && stats_json_neg.verts_inside_src > 0) {
				recommended_path = effective_uses_json_xform
					? "JSON Camera | NEG Z | JSON Transform"
					: "JSON Camera | NEG Z | Identity Transform";
			}
			else if (sd.has_transform &&
				stats_identity_neg.faces_projectable > 0 &&
				stats_identity_neg.verts_inside_src > 0) {
				recommended_path = "JSON Camera | NEG Z | Identity Transform";
			}
			else if (stats_json_neg.faces_projectable > 0) {
				recommended_path = effective_uses_json_xform
					? "JSON Camera | NEG Z | JSON Transform (off-screen)"
					: "JSON Camera | NEG Z | Identity Transform (off-screen)";
			}
			else if (sd.has_transform && stats_identity_neg.faces_projectable > 0) {
				recommended_path = "JSON Camera | NEG Z | Identity Transform (off-screen)";
			}
		}
		else {
			const bool neg_better =
				(stats_json_neg.verts_inside_src > stats_json_pos.verts_inside_src) ||
				(stats_json_neg.verts_inside_src == stats_json_pos.verts_inside_src &&
					stats_json_neg.faces_projectable > stats_json_pos.faces_projectable) ||
				(stats_json_neg.verts_inside_src == stats_json_pos.verts_inside_src &&
					stats_json_neg.faces_projectable == stats_json_pos.faces_projectable &&
					cost_json_neg <= cost_json_pos);

			recommended_path = neg_better
				? "AE Fallback | NEG Z"
				: "AE Fallback | POS Z";
		}

		std::ostringstream oss;
		oss
			<< "========== CameraProjection Debug ==========\n\n"
			<< "[Mode]\n"
			<< "  UseJSONXform=" << (use_json_xform ? "ON" : "OFF")
			<< " | ShowMesh=" << (show_mesh ? "ON" : "OFF") << "\n\n"

			<< "[Camera]\n"
			<< "  AE Fallback valid      : " << (ae_cam_valid_before_fallback ? "YES" : "NO") << "\n"
			<< "  JSON Cam override used : " << (used_json_camera ? "YES" : "NO") << "\n"
			<< "  Effective source       : " << cam_source << "\n"
			<< "  Mode                   : " << cam_mode << "\n"
			<< "  Valid                  : " << (camera.valid ? "YES" : "NO") << "\n"
			<< "  Comp                   : " << camera.comp_width << " x " << camera.comp_height << "\n"
			<< "  Zoom                   : " << camera.zoom << "\n"
			<< "  FOV(from zoom)         : " << fov_from_zoom << "\n\n"

			<< "[Center Probe]\n"
			<< "  NEG : valid=" << (basis_neg.center.ok ? "Y" : "N")
			<< " src=(" << basis_neg.center.sx << "," << basis_neg.center.sy << ") depth=" << basis_neg.center.d << "\n"
			<< "  POS : valid=" << (basis_pos.center.ok ? "Y" : "N")
			<< " src=(" << basis_pos.center.sx << "," << basis_pos.center.sy << ") depth=" << basis_pos.center.d << "\n\n"

			<< "[Basis / NEG]\n"
			<< "  +X : " << (basis_neg.px.ok ? "Y" : "N") << " ang=" << neg_ang_x << "\n"
			<< "  +Y : " << (basis_neg.py.ok ? "Y" : "N") << " ang=" << neg_ang_y << "\n"
			<< "  +Z : " << (basis_neg.pz.ok ? "Y" : "N") << " ang=" << neg_ang_z << "\n\n"

			<< "[Projection Attempt Stats]\n"
			<< format_stats_line(effective_uses_json_xform ? "JSONXform + NEG" : "Identity + NEG", stats_json_neg, cost_json_neg) << "\n"
			<< format_stats_line(effective_uses_json_xform ? "JSONXform + POS" : "Identity + POS", stats_json_pos, cost_json_pos) << "\n";

		if (sd.has_transform) {
			oss
				<< format_stats_line("Identity + NEG", stats_identity_neg, cost_identity_neg) << "\n"
				<< format_stats_line("Identity + POS", stats_identity_pos, cost_identity_pos) << "\n";
		}

		oss
			<< "\n[Recommended Path]\n"
			<< "  " << recommended_path << "\n";

		const std::string msg = oss.str();
		MessageBoxA(NULL, msg.c_str(), "Camera Projection Debug", MB_OK | MB_ICONINFORMATION);
	}
#endif

	static PF_Err Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
	{
		PF_Err err = PF_Err_NONE;
		RenderStatus status = RenderStatus::Ok;

		const A_Boolean debug_status = params[CAMPROJ_DEBUG_STATUS]->u.bd.value;
		const bool use_json_xform = (params[CAMPROJ_USE_JSON_XFORM] != nullptr)
			? (params[CAMPROJ_USE_JSON_XFORM]->u.bd.value ? true : false)
			: true;

		SceneData sd;
		BuildSceneData(sd);

		if (!sd.obj_path || !sd.obj_path[0]) {
			status = RenderStatus::NoOBJPath;
		}
		if (status == RenderStatus::Ok && (!sd.mesh_ok || !sd.meshP)) {
			status = RenderStatus::OBJLoadFailed;
		}
		if (status == RenderStatus::Ok && sd.meshP->uvs.empty()) {
			status = RenderStatus::NoUV;
		}
		if (status == RenderStatus::Ok && sd.meshP->faces.empty()) {
			status = RenderStatus::OBJLoadFailed;
		}

		CameraContext camera;
		if (status == RenderStatus::Ok) {
			BuildEffectiveCamera(
				in_data,
				sd,
				camera,
				nullptr);

			if (!camera.valid) {
				status = RenderStatus::NoCamera;
			}
		}

		const bool current_uses_json_xform =
			(sd.json_ok && sd.has_transform && use_json_xform);

		ObjectTransform obj_xform;
		if (current_uses_json_xform) {
			obj_xform = sd.json_xform;
		}

		A_long written_pixels = 0;
		if (status == RenderStatus::Ok) {
			const A_long target_w = output->width;
			const A_long target_h = output->height;
			const A_Boolean wire = params[CAMPROJ_SHOW_WIREFRAME]->u.bd.value;
			const A_Boolean show_mesh = (params[CAMPROJ_SHOW_MESH] != nullptr)
				? (params[CAMPROJ_SHOW_MESH]->u.bd.value ? true : false)
				: false;

			auto render_one = [&](const ObjectTransform& xform, A_Boolean use_negative_z) {
				if (PF_WORLD_IS_DEEP(output)) {
					if (show_mesh) {
						err = RenderCameraMeshWireframe<PF_Pixel16>(
							output,
							*sd.meshP,
							camera,
							xform,
							use_negative_z,
							&written_pixels);
					}
					else {
						err = RenderProjection<PF_Pixel16>(
							&params[CAMPROJ_INPUT]->u.ld,
							output,
							*sd.meshP,
							camera,
							xform,
							target_w,
							target_h,
							wire,
							use_negative_z,
							&written_pixels);
					}
				}
				else {
					if (show_mesh) {
						err = RenderCameraMeshWireframe<PF_Pixel8>(
							output,
							*sd.meshP,
							camera,
							xform,
							use_negative_z,
							&written_pixels);
					}
					else {
						err = RenderProjection<PF_Pixel8>(
							&params[CAMPROJ_INPUT]->u.ld,
							output,
							*sd.meshP,
							camera,
							xform,
							target_w,
							target_h,
							wire,
							use_negative_z,
							&written_pixels);
					}
				}
				};

			auto try_render_path = [&](const ObjectTransform& xform, A_Boolean use_negative_z) -> bool {
				if (err) {
					return false;
				}
				written_pixels = 0;
				render_one(xform, use_negative_z);
				return (!err && written_pixels > 0);
				};

			bool rendered = false;

			if (camera.use_euler_view && camera.use_basis_vectors) {
				rendered = try_render_path(obj_xform, TRUE);

				if (!rendered && current_uses_json_xform) {
					rendered = try_render_path(ObjectTransform(), TRUE);
				}
			}
			else {
				rendered = try_render_path(obj_xform, TRUE);

				if (!rendered && current_uses_json_xform) {
					rendered = try_render_path(ObjectTransform(), TRUE);
				}

				if (!rendered) {
					rendered = try_render_path(obj_xform, FALSE);
				}

				if (!rendered && current_uses_json_xform) {
					rendered = try_render_path(ObjectTransform(), FALSE);
				}
			}

			if (!err && !rendered) {
				status = RenderStatus::NoProjectionHit;
			}
		}

		if (!err && status != RenderStatus::Ok) {
			if (debug_status) {
				if (PF_WORLD_IS_DEEP(output)) {
					switch (status) {
					case RenderStatus::NoOBJPath:
						FillSolid<PF_Pixel16>(output, PF_MAX_CHAN16, 0, PF_MAX_CHAN16);
						break;
					case RenderStatus::OBJLoadFailed:
						FillSolid<PF_Pixel16>(output, PF_MAX_CHAN16, PF_MAX_CHAN16, 0);
						break;
					case RenderStatus::NoUV:
						FillSolid<PF_Pixel16>(output, 0, PF_MAX_CHAN16, PF_MAX_CHAN16);
						break;
					case RenderStatus::NoCamera:
						FillSolid<PF_Pixel16>(output, PF_MAX_CHAN16, 0, 0);
						break;
					case RenderStatus::NoProjectionHit:
						FillSolid<PF_Pixel16>(output, 0, PF_MAX_CHAN16, 0);
						break;
					default:
						break;
					}
				}
				else {
					switch (status) {
					case RenderStatus::NoOBJPath:
						FillSolid<PF_Pixel8>(output, PF_MAX_CHAN8, 0, PF_MAX_CHAN8);
						break;
					case RenderStatus::OBJLoadFailed:
						FillSolid<PF_Pixel8>(output, PF_MAX_CHAN8, PF_MAX_CHAN8, 0);
						break;
					case RenderStatus::NoUV:
						FillSolid<PF_Pixel8>(output, 0, PF_MAX_CHAN8, PF_MAX_CHAN8);
						break;
					case RenderStatus::NoCamera:
						FillSolid<PF_Pixel8>(output, PF_MAX_CHAN8, 0, 0);
						break;
					case RenderStatus::NoProjectionHit:
						FillSolid<PF_Pixel8>(output, 0, PF_MAX_CHAN8, 0);
						break;
					default:
						break;
					}
				}
			}
			else {
				if (PF_WORLD_IS_DEEP(output)) {
					FillSolid<PF_Pixel16>(output, 0, 0, 0);
				}
				else {
					FillSolid<PF_Pixel8>(output, 0, 0, 0);
				}
			}
		}

		return err;
	}

	static void AccumMinMax(float v, float& mn, float& mx, bool& has)
	{
		if (!has) {
			mn = v;
			mx = v;
			has = true;
		}
		else {
			mn = std::min(mn, v);
			mx = std::max(mx, v);
		}
	}

	static ProjectionStats CollectProjectionStats(const MeshData& mesh,
		const CameraContext& camera,
		const ObjectTransform& obj_xform,
		bool use_negative_z,
		const PF_LayerDef* input)
	{
		ProjectionStats st;
		st.faces_total = static_cast<A_long>(mesh.faces.size());

		for (const Face& face : mesh.faces) {
			if (face.v[0] < 0 || face.v[1] < 0 || face.v[2] < 0) continue;
			if (face.vt[0] < 0 || face.vt[1] < 0 || face.vt[2] < 0) continue;
			if (face.v[0] >= static_cast<int>(mesh.vertices.size()) ||
				face.v[1] >= static_cast<int>(mesh.vertices.size()) ||
				face.v[2] >= static_cast<int>(mesh.vertices.size())) continue;
			if (face.vt[0] >= static_cast<int>(mesh.uvs.size()) ||
				face.vt[1] >= static_cast<int>(mesh.uvs.size()) ||
				face.vt[2] >= static_cast<int>(mesh.uvs.size())) continue;

			++st.faces_index_ok;

			const Vec2 uv0 = mesh.uvs[face.vt[0]];
			const Vec2 uv1 = mesh.uvs[face.vt[1]];
			const Vec2 uv2 = mesh.uvs[face.vt[2]];

			AccumMinMax(uv0.x, st.uv_min_x, st.uv_max_x, st.has_uv);
			AccumMinMax(uv1.x, st.uv_min_x, st.uv_max_x, st.has_uv);
			AccumMinMax(uv2.x, st.uv_min_x, st.uv_max_x, st.has_uv);
			AccumMinMax(uv0.y, st.uv_min_y, st.uv_max_y, st.has_uv);
			AccumMinMax(uv1.y, st.uv_min_y, st.uv_max_y, st.has_uv);
			AccumMinMax(uv2.y, st.uv_min_y, st.uv_max_y, st.has_uv);

			const int vids[3] = { face.v[0], face.v[1], face.v[2] };
			bool face_any_projected = false;
			for (int i = 0; i < 3; ++i) {
				++st.verts_total;
				Vec3 p = ApplyObjectTransform(mesh.vertices[vids[i]], obj_xform);

				float sx = 0.0f, sy = 0.0f, dz = 0.0f;
				if (ProjectWorldToSource(p, camera, use_negative_z, sx, sy, dz)) {
					face_any_projected = true;
					++st.verts_projected;
					AccumMinMax(sx, st.src_min_x, st.src_max_x, st.has_src);
					AccumMinMax(sy, st.src_min_y, st.src_max_y, st.has_src);
					AccumMinMax(dz, st.depth_min, st.depth_max, st.has_depth);
					if (input &&
						sx >= 0.0f && sy >= 0.0f &&
						sx <= static_cast<float>(input->width - 1) &&
						sy <= static_cast<float>(input->height - 1)) {
						++st.verts_inside_src;
					}
				}
			}

			if (face_any_projected) {
				++st.faces_projectable;
			}
		}

		return st;
	}

} // namespace

extern "C" DllExport
PF_Err PluginDataEntryFunction2(
	PF_PluginDataPtr inPtr,
	PF_PluginDataCB2 inPluginDataCallBackPtr,
	SPBasicSuite* inSPBasicSuitePtr,
	const char* inHostName,
	const char* inHostVersion)
{
	PF_Err result = PF_Err_INVALID_CALLBACK;

	result = PF_REGISTER_EFFECT_EXT2(
		inPtr,
		inPluginDataCallBackPtr,
		"Auto Camera Projection",
		"SaltedFish.CameraProjection",
		"SaltedFish",
		AE_RESERVED_INFO,
		"EffectMain",
		"https://www.jom.wang");

	return result;
}

PF_Err
EffectMain(
	PF_Cmd cmd,
	PF_InData* in_data,
	PF_OutData* out_data,
	PF_ParamDef* params[],
	PF_LayerDef* output,
	void* extra)
{
	PF_Err err = PF_Err_NONE;

	try {
		switch (cmd) {
		case PF_Cmd_ABOUT:
			err = About(in_data, out_data, params, output);
			break;
		case PF_Cmd_GLOBAL_SETUP:
			err = GlobalSetup(in_data, out_data, params, output);
			break;
		case PF_Cmd_PARAMS_SETUP:
			err = ParamsSetup(in_data, out_data, params, output);
			break;
		case PF_Cmd_RENDER:
			err = Render(in_data, out_data, params, output);
			break;
		case PF_Cmd_USER_CHANGED_PARAM:
			err = UserChangedParam(in_data, out_data, params, extra);
			break;
		default:
			break;
		}
	}
	catch (PF_Err& thrown_err) {
		err = thrown_err;
	}

	return err;
}
