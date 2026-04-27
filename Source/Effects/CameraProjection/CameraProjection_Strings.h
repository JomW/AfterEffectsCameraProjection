#pragma once

typedef enum {
	StrID_NONE,
	StrID_Name,
	StrID_Description,
	StrID_SelectOBJ_Param_Name,
	StrID_SelectOBJ_Button_Name,
	StrID_SelectJSON_Param_Name,
	StrID_SelectJSON_Button_Name,
	StrID_DumpInfo_Param_Name,
	StrID_DumpInfo_Button_Name,
	StrID_DebugStatus_Param_Name,
	StrID_ShowWireframe_Param_Name,
	StrID_NUMTYPES
} StrIDType;

#ifdef __cplusplus
extern "C" {
#endif

	char* GetStringPtr(int strNum);

#ifdef __cplusplus
}
#endif