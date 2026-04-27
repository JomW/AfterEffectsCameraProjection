#include "CameraProjection.h"

typedef struct {
	A_u_long	index;
	A_char		str[256];
} TableString;

TableString g_strs[StrID_NUMTYPES] = {
	StrID_NONE,						"",
	StrID_Name,						"Camera Projection",
	StrID_Description,				"Projects camera view onto 3D mesh UV space.\r\rCopyright 2023 Adobe Inc.",
	StrID_SelectOBJ_Param_Name,		"Mesh OBJ",
	StrID_SelectOBJ_Button_Name,	"Select OBJ...",
	StrID_SelectJSON_Param_Name,	"Scene JSON",
	StrID_SelectJSON_Button_Name,	"Select JSON...",
	StrID_DumpInfo_Param_Name,		"Debug Info",
	StrID_DumpInfo_Button_Name,		"Show Debug Popup",
	StrID_DebugStatus_Param_Name,	"Debug Status Colors",
	StrID_ShowWireframe_Param_Name,	"Show Wireframe"
};

char* GetStringPtr(int strNum)
{
	return g_strs[strNum].str;
}