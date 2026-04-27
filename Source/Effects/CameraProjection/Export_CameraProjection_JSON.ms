global cpSelectedMesh
global cpSelectedCamera

cpSelectedMesh = undefined
cpSelectedCamera = undefined

fn cp_escapePath s =
(
	local t = substituteString s "\\" "\\\\"
	t = substituteString t "\"" "\\\""
	t
)

fn cp_fmt3 v =
(
	"[" + (v.x as string) + ", " + (v.y as string) + ", " + (v.z as string) + "]"
)

-- Max world -> plugin render space
-- Proven mapping:
--   render.x = max.x
--   render.y = -max.z
--   render.z = max.y
fn cp_toRenderPos v =
(
	[v.x, -v.z, v.y]
)

fn cp_toRenderDir v =
(
	[v.x, -v.z, v.y]
)

fn cp_cameraFilter n =
(
	isKindOf n Camera
)

fn cp_meshFilter n =
(
	(superClassOf n == GeometryClass) and (not isKindOf n Camera)
)

fn cp_exportData meshObj camObj =
(
	local jsonPath = getSaveFileName caption:"保存 CameraProjection JSON" types:"JSON (*.json)|*.json|"
	if jsonPath == undefined then
	(
		return false
	)

	local objPath = getFilenamePath jsonPath + getFilenameFile jsonPath + ".obj"
	local oldSel = selection as array

	try
	(
		select meshObj
		exportFile objPath #noPrompt selectedOnly:true using:ObjExp
	)
	catch
	(
		if oldSel.count > 0 then
		(
			select oldSel
		)
		else
		(
			clearSelection()
		)

		messageBox "OBJ 导出失败，请确认 OBJ 导出器可用。" title:"CameraProjection Export"
		return false
	)

	if oldSel.count > 0 then
	(
		select oldSel
	)
	else
	(
		clearSelection()
	)

	-- OBJ is exported baked. Keep JSON transform explicit and identity.
	local objPos = [0, 0, 0]
	local objRot = [0, 0, 0]
	local objScale = [1, 1, 1]

	-- Final validated camera basis for Max camera:
	--   right   = row1
	--   up      = row2
	--   forward = -row3
	local camPos = cp_toRenderPos camObj.position
	local camRight = cp_toRenderDir (normalize camObj.transform.row1)
	local camUp = cp_toRenderDir (normalize camObj.transform.row2)
	local camForward = cp_toRenderDir (normalize (camObj.transform.row3 * -1.0))
	local camFovDeg = camObj.fov as float

	local f = createFile jsonPath
	format "{\n" to:f
	format "  \"objPath\": \"%\",\n" (cp_escapePath objPath) to:f
	format "  \"position\": %,\n" (cp_fmt3 objPos) to:f
	format "  \"rotation\": %,\n" (cp_fmt3 objRot) to:f
	format "  \"scale\": %,\n" (cp_fmt3 objScale) to:f
	format "  \"cameraPosition\": %,\n" (cp_fmt3 camPos) to:f
	format "  \"cameraRight\": %,\n" (cp_fmt3 camRight) to:f
	format "  \"cameraUp\": %,\n" (cp_fmt3 camUp) to:f
	format "  \"cameraForward\": %,\n" (cp_fmt3 camForward) to:f
	format "  \"cameraFovDegrees\": %\n" camFovDeg to:f
	format "}\n" to:f
	close f

	messageBox ("导出完成:\nOBJ: " + objPath + "\nJSON: " + jsonPath) title:"CameraProjection Export"
	true
)

rollout DataExportTool "3DSMax 投影数据导出工具" width:227 height:269
(
	label 'lbl1' "本工具支持导出模型和摄像机数据" pos:[10,11] width:208 height:20 align:#left
	label 'lbl2' "1.请使用下方按钮选中屏幕的Mesh对象" pos:[9,34] width:208 height:20 align:#left
	pickButton 'btn2' "PickButton" pos:[9,53] width:208 height:30 align:#left autoDisplay:true
	label 'lbl3' "2.请使用下方按钮选中摄像机对象" pos:[9,103] width:208 height:20 align:#left
	pickButton 'btn3' "PickButton" pos:[9,123] width:208 height:30 align:#left autoDisplay:true
	label 'lbl4' "3.请在选中Mesh和摄像机后点击下方" pos:[9,172] width:208 height:20 align:#left
	label 'lbl5' "导出按钮进行数据导出。" pos:[21,191] width:208 height:20 align:#left
	button 'btn4' "导出数据" pos:[12,214] width:204 height:34 align:#left

	on btn2 picked obj do
	(
		if (obj == undefined) or (not cp_meshFilter obj) then
		(
			cpSelectedMesh = undefined
			btn2.text = "PickButton"
			messageBox "请选择 Mesh 几何体对象。" title:"CameraProjection Export"
		)
		else
		(
			cpSelectedMesh = obj
		)
	)

	on btn3 picked obj do
	(
		if (obj == undefined) or (not cp_cameraFilter obj) then
		(
			cpSelectedCamera = undefined
			btn3.text = "PickButton"
			messageBox "请选择摄像机对象。" title:"CameraProjection Export"
		)
		else
		(
			cpSelectedCamera = obj
		)
	)

	on btn4 pressed do
	(
		-- 点击导出时先检查 Mesh 是否已选择
		if cpSelectedMesh == undefined then
		(
			messageBox "请先通过第1步选择一个 Mesh 对象。" title:"CameraProjection Export"
			return false
		)

		if cpSelectedCamera == undefined then
		(
			messageBox "请先通过第2步选择一个摄像机对象。" title:"CameraProjection Export"
			return false
		)

		if (isValidNode cpSelectedMesh) == false then
		(
			messageBox "Mesh 对象已失效，请重新选择。" title:"CameraProjection Export"
			cpSelectedMesh = undefined
			btn2.text = "PickButton"
			return false
		)

		if (isValidNode cpSelectedCamera) == false then
		(
			messageBox "摄像机对象已失效，请重新选择。" title:"CameraProjection Export"
			cpSelectedCamera = undefined
			btn3.text = "PickButton"
			return false
		)

		cp_exportData cpSelectedMesh cpSelectedCamera
	)
)

try (destroyDialog DataExportTool) catch()
createDialog DataExportTool style:#(#style_titlebar, #style_sysmenu, #style_toolwindow)