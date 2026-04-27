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

#ifndef CAMERAPROJECTION_H
#define CAMERAPROJECTION_H

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

typedef unsigned char		u_char;
typedef unsigned short		u_short;
typedef unsigned short		u_int16;
typedef unsigned long		u_long;
typedef short int			int16;
#define PF_TABLE_BITS	12
#define PF_TABLE_SZ_16	4096

#define PF_DEEP_COLOR_AWARE 1	// make sure we get 16bpc pixels; 
// AE_Effect.h checks for this.

#include "AEConfig.h"

#ifdef AE_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
typedef unsigned short PixelType;
#include <Windows.h>
#endif

#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectCBSuites.h"
#include "String_Utils.h"
#include "AE_GeneralPlug.h"
#include "AEFX_ChannelDepthTpl.h"
#include "AEGP_SuiteHandler.h"

#include "CameraProjection_Strings.h"

/* Versioning information */

#define	MAJOR_VERSION	1
#define	MINOR_VERSION	0
#define	BUG_VERSION		0
#define	STAGE_VERSION	PF_Stage_DEVELOP
#define	BUILD_VERSION	1


/* Parameter defaults */

enum {
	CAMPROJ_INPUT = 0,
	CAMPROJ_SELECT_OBJ,
	CAMPROJ_SELECT_JSON,
	CAMPROJ_DUMP_INFO,
	CAMPROJ_DEBUG_STATUS,
	CAMPROJ_SHOW_WIREFRAME,
	CAMPROJ_NUM_PARAMS
};

enum {
	SELECT_OBJ_DISK_ID = 1,
	SELECT_JSON_DISK_ID,
	DUMP_INFO_DISK_ID,
	DEBUG_STATUS_DISK_ID,
	SHOW_WIREFRAME_DISK_ID
};

//#define CAMPROJ_OUTPUT_WIDTH_MIN		64
//#define CAMPROJ_OUTPUT_WIDTH_MAX		8192
//#define CAMPROJ_OUTPUT_WIDTH_DFLT		1920
//
//#define CAMPROJ_OUTPUT_HEIGHT_MIN		64
//#define CAMPROJ_OUTPUT_HEIGHT_MAX		8192
//#define CAMPROJ_OUTPUT_HEIGHT_DFLT		1080
//
//#define CAMPROJ_FOV_MIN					10.0
//#define CAMPROJ_FOV_MAX					170.0
//#define CAMPROJ_FOV_DFLT				50.0
//
//#define CAMPROJ_CAMERA_LAYER_INDEX_MIN	0
//#define CAMPROJ_CAMERA_LAYER_INDEX_MAX	512
//#define CAMPROJ_CAMERA_LAYER_INDEX_DFLT	0

typedef struct {
	A_Boolean		show_wireframe;
	char			mesh_path[AEGP_MAX_PATH_SIZE];
} RenderInfo;

#define FILEPATH_MAGIC	0x46505448

extern "C" {

	DllExport
		PF_Err
		EffectMain(
			PF_Cmd			cmd,
			PF_InData* in_data,
			PF_OutData* out_data,
			PF_ParamDef* params[],
			PF_LayerDef* output,
			void* extra);

}

#endif // CAMERAPROJECTION_H