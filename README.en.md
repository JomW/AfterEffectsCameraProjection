# AfterEffectsCameraProjection

After Effects Camera Projection plugin example repository.

语言 / Language: [中文](README.md) | [English](README.en.md)

Navigation

- [Overview](#overview)
- [Folder Structure And Purpose](#folder-structure-and-purpose)
- [Recommended Workflow](#recommended-workflow)
- [Who This Repository Is For](#who-this-repository-is-for)
- [中文 README](README.md)

## Overview

This repository contains a Camera Projection plugin example for After Effects. Its core capability is projecting a camera view onto the UV space of a 3D mesh. It is suitable for workflows such as naked-eye animation calibration, screen projection mapping, and camera matching between DCC tools and AE.

The repository is organized into four main parts:

- `Source`: plugin source code, project files, export scripts, Adobe AE SDK headers, and utility code.
- `Plugins`: compiled plugin binaries and runtime sample data that can be copied directly into the AE Plug-ins folder for testing.
- `FBX`: reference FBX assets for DCC tools, used to help with scene setup and camera alignment.
- `AE_Project`: sample AE project files and preview media used to inspect calibration results and compare final output.

## Folder Structure And Purpose

### Source

`Source` is the development directory of the project. It is mainly used to build and maintain the AE plugin.

Key contents:

- `Source/Effects/CameraProjection/`
  - Core implementation directory of the plugin.
  - `CameraProjection.cpp` is the main logic entry. It loads OBJ and JSON data, builds scene state, performs projection rendering, and drives debug visualization.
  - `CameraProjection.h` defines plugin parameters, version information, and the exported `EffectMain` entry point.
  - `CameraProjection_IO.*` handles reading and parsing scene JSON files.
  - `CameraProjection_Strings.*` defines the plugin name, description, and parameter labels shown in the AE panel.
  - `OBJParser.h` is used to parse mesh OBJ data.
  - `ProjectionMath.h` contains the math used by projection logic.
  - `Export_CameraProjection_JSON.ms` is the 3ds Max export script source used to generate the `.json` and `.obj` files required by the plugin.
  - `Inspect_Max_OBJ_Coords.ms` is an auxiliary script for checking coordinate systems or export results.

- `Source/Effects/CameraProjection/Win/`
  - Windows Visual Studio project directory.
  - Contains `CameraProjection.sln`, `CameraProjection.vcxproj`, and related project files for building the `.aex` plugin.

- `Source/Headers/`
  - Adobe After Effects SDK header files.
  - These files provide host APIs, parameter definitions, pixel format definitions, Suite interfaces, and other plugin development dependencies.

- `Source/Util/`
  - Common utility code from Adobe sample projects.
  - Mainly used to simplify Suite access, string handling, channel processing, and Smart Render related helpers.

### Plugins

`Plugins` is the runtime distribution directory, intended for direct use inside AE or for test delivery.

Main files:

- `CameraProjection.aex`
  - The compiled AE plugin binary.

- `data.json`
  - A sample scene parameter file.
  - It stores the OBJ path, object transform, camera position, camera basis vectors, and camera field of view.

- `data.obj`
  - The mesh file paired with `data.json`.
  - The plugin reads the OBJ geometry and combines it with the camera data from JSON to perform projection.

- `Export_CameraProjection_JSON.mse`
  - A packaged 3ds Max export script intended for artists or animation workflows.

- `howto.txt`
  - Installation instructions.
  - The current instructions say to close AE, copy `CameraProjection.aex`, `data.json`, and `data.obj` into the AE installation `Plug-ins` folder, then reopen AE.
  - It also notes that the plugin has mainly been tested on Windows with AE 2024.

### FBX

The `FBX` folder provides reference assets for DCC-side use and serves as the reference scene and animation alignment baseline for the DCC production stage.

According to `readme.txt`, the FBX files in this directory are mainly used to:

- import into DCC software to assist animation production,
- allow overall translation while avoiding unnecessary scaling or changes to relative positions,
- avoid changing camera parameters during production,
- serve as the reference basis for later JSON and OBJ export.

The existing notes also make two things explicit:

- The export toolchain is currently adapted for 3ds Max only; support for other DCC tools is still in progress.
- After import, the output resolution is recommended to be set to `2000 x 1600`, or another resolution with the same aspect ratio.

### AE_Project

`AE_Project` is used on the AE side to demonstrate the project result and provide a validation reference. It currently contains:

- `裸眼动画校准.aep`
  - A sample After Effects project.
  - It can be opened directly to inspect project setup, layer organization, and calibration results after plugin integration.

- `裸眼动画01_preview.avi`
  - A preview video for the corresponding result.
  - Useful for quickly reviewing the final effect without opening the AE project.

## Recommended Workflow

1. Import the reference assets from `FBX` into your DCC tool and create animation while keeping the intended camera and spatial relationships.
2. In 3ds Max, use `Export_CameraProjection_JSON.ms` or `Export_CameraProjection_JSON.mse` to export `.obj` and `.json`.
3. Copy `CameraProjection.aex`, `data.json`, and `data.obj` from `Plugins` into the AE `Plug-ins` folder.
4. In AE, open the sample project or apply the plugin to the target layer, then select the matching OBJ and JSON data.
5. Use the plugin debug options to inspect wireframe, debug information, and projection status, and confirm that the camera and mesh match correctly.

## Who This Repository Is For

- Technical artists who need to sync camera and mesh relationships from DCC tools into AE.
- Developers who need to maintain or extend a native After Effects plugin.
- Team members who want to reuse the naked-eye animation calibration workflow.
