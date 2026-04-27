fn cp_fmtPoint3 p =
(
	"[" + (p.x as string) + ", " + (p.y as string) + ", " + (p.z as string) + "]"
)

fn cp_updateBounds p boundsArr =
(
	if boundsArr[1] == undefined then
	(
		boundsArr[1] = p
		boundsArr[2] = p
	)
	else
	(
		boundsArr[1] = [amin #(boundsArr[1].x, p.x), amin #(boundsArr[1].y, p.y), amin #(boundsArr[1].z, p.z)]
		boundsArr[2] = [amax #(boundsArr[2].x, p.x), amax #(boundsArr[2].y, p.y), amax #(boundsArr[2].z, p.z)]
	)
)

fn cp_readObjVertsAndBounds objPath sampleCount:5 =
(
	local verts = #()
	local boundsArr = #(undefined, undefined)

	local f = openFile objPath mode:"rt"
	if f == undefined then
	(
		format "ERROR: Cannot open OBJ file: %\n" objPath
		return #(verts, boundsArr[1], boundsArr[2])
	)

	while not eof f do
	(
		local line = trimRight (readLine f)
		if matchPattern line pattern:"v *" then
		(
			local tokens = for s in (filterString line " \t") where s != "" collect s
			if tokens.count >= 4 then
			(
				local p = [(tokens[2] as float), (tokens[3] as float), (tokens[4] as float)]
				cp_updateBounds p boundsArr
				if verts.count < sampleCount do append verts p
			)
		)
	)

	close f
	#(verts, boundsArr[1], boundsArr[2])
)

fn cp_collectNodeVertsAndBounds nd sampleCount:5 =
(
	local snap = snapshotAsMesh nd
	local invTm = inverse nd.objecttransform

	local worldVerts = #()
	local localVerts = #()

	local worldBounds = #(undefined, undefined)
	local localBounds = #(undefined, undefined)

	for i = 1 to snap.numverts do
	(
		local pWorld = getVert snap i
		local pLocal = pWorld * invTm

		cp_updateBounds pWorld worldBounds
		cp_updateBounds pLocal localBounds

		if worldVerts.count < sampleCount do append worldVerts pWorld
		if localVerts.count < sampleCount do append localVerts pLocal
	)

	#(
		worldVerts,
		localVerts,
		worldBounds[1], worldBounds[2],
		localBounds[1], localBounds[2]
	)
)

if selection.count != 1 then
(
	messageBox "请先只选择一个要导出的物体。" title:"Inspect Max / OBJ Coordinates"
)
else
(
	local nd = selection[1]
	local objPath = getOpenFileName caption:"Select exported OBJ" types:"OBJ (*.obj)|*.obj|"
	if objPath != undefined then
	(
		local objData = cp_readObjVertsAndBounds objPath sampleCount:5
		local maxData = cp_collectNodeVertsAndBounds nd sampleCount:5

		format "\n================ MAX / OBJ DIAGNOSTIC ================\n"
		format "Selected node: %\n" nd.name
		format "OBJ path: %\n\n" objPath

		format "[Node Transform]\n"
		format "row1 = %\n" nd.objecttransform.row1
		format "row2 = %\n" nd.objecttransform.row2
		format "row3 = %\n" nd.objecttransform.row3
		format "row4 = %\n\n" nd.objecttransform.row4

		format "[OBJ Bounds]\n"
		format "min = %\n" (cp_fmtPoint3 objData[2])
		format "max = %\n\n" (cp_fmtPoint3 objData[3])

		format "[Max Snapshot / World Bounds]\n"
		format "min = %\n" (cp_fmtPoint3 maxData[3])
		format "max = %\n\n" (cp_fmtPoint3 maxData[4])

		format "[Max Local Guess Bounds]  (snapshotVert * inverse objectTM)\n"
		format "min = %\n" (cp_fmtPoint3 maxData[5])
		format "max = %\n\n" (cp_fmtPoint3 maxData[6])

		format "[First OBJ vertices]\n"
		for i = 1 to objData[1].count do
		(
			format "% : %\n" i (cp_fmtPoint3 objData[1][i])
		)
		format "\n"

		format "[First Max snapshot/world vertices]\n"
		for i = 1 to maxData[1].count do
		(
			format "% : %\n" i (cp_fmtPoint3 maxData[1][i])
		)
		format "\n"

		format "[First Max local-guess vertices]\n"
		for i = 1 to maxData[2].count do
		(
			format "% : %\n" i (cp_fmtPoint3 maxData[2][i])
		)
		format "\n"

		local cam = viewport.getCamera()
		format "[Active Viewport Camera]\n"
		if isKindOf cam Camera then
		(
			format "name   = %\n" cam.name
			format "pos    = %\n" (cp_fmtPoint3 cam.position)

			if cam.target != undefined then
				format "target = %\n" (cp_fmtPoint3 cam.target.position)
			else
				format "target = <none>\n"

			format "rot    = %\n" (cam.rotation as eulerAngles)

			try
			(
				format "fov    = %\n" (cam.fov as float)
			)
			catch
			(
				format "fov    = <unavailable>\n"
			)
		)
		else
		(
			format "No active camera viewport.\n"
		)

	)
)