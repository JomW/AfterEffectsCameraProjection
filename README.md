# AfterEffectsCameraProjection

After Effects Camera Projection plugin example repository.

语言 / Language: [中文](README.md) | [English](README.en.md)

导航

- [项目简介](#项目简介)
- [目录结构与作用](#目录结构与作用)
- [推荐工作流](#推荐工作流)
- [适合谁看这个仓库](#适合谁看这个仓库)
- [English README](README.en.md)

## 项目简介

这是一个用于 After Effects 的 Camera Projection 插件示例仓库。核心能力是将摄像机视角投射到 3D 网格的 UV 空间上，用于裸眼动画校准、屏幕贴图回投、DCC 到 AE 的相机匹配等工作流。

仓库同时包含四类内容：

- `Source`：插件源码、工程文件、导出脚本，以及 Adobe AE SDK 相关头文件与工具代码。
- `Plugins`：编译后的插件和运行时示例数据，可直接复制到 AE 的 Plug-ins 目录进行测试。
- `FBX`：给 DCC 软件使用的参考 FBX 资源，用于辅助搭建动画和保持相机关系。
- `AE_Project`：示例 AE 工程和预览视频，用于查看校准结果和对照最终效果。

## 目录结构与作用

### Source

`Source` 是整个项目的开发目录，主要用于编译和维护 AE 插件。

关键内容如下：

- `Source/Effects/CameraProjection/`
	- 插件核心实现目录。
	- `CameraProjection.cpp` 是主要逻辑入口，负责读取 OBJ 和 JSON、构建场景数据、执行投射渲染以及调试显示。
	- `CameraProjection.h` 定义插件参数、版本信息和导出入口 `EffectMain`。
	- `CameraProjection_IO.*` 负责场景 JSON 的读取与解析。
	- `CameraProjection_Strings.*` 定义 AE 面板中的插件名称、描述和参数文案。
	- `OBJParser.h` 用于读取网格 OBJ 数据。
	- `ProjectionMath.h` 封装投射相关的数学运算。
	- `Export_CameraProjection_JSON.ms` 是 3ds Max 导出脚本源码，用来导出插件所需的 `.json` 和 `.obj` 数据。
	- `Inspect_Max_OBJ_Coords.ms` 是用于检查坐标系或导出结果的辅助脚本。

- `Source/Effects/CameraProjection/Win/`
	- Windows 下的 Visual Studio 工程目录。
	- 包含 `CameraProjection.sln`、`CameraProjection.vcxproj` 等工程文件，用于编译 `.aex` 插件。

- `Source/Headers/`
	- Adobe After Effects SDK 相关头文件。
	- 这些文件为插件开发提供宿主接口、参数定义、像素格式、Suite 接口等基础能力。

- `Source/Util/`
	- Adobe 示例工程常见的工具代码与封装。
	- 主要用于简化 Suite 调用、字符串处理、通道处理、Smart Render 辅助等通用工作。

一句话概括：`Source` 负责“开发和编译插件”。

### Plugins

`Plugins` 是运行时分发目录，适合直接给 AE 使用或交付测试。

包含的主要文件：

- `CameraProjection.aex`
	- 编译好的 AE 插件二进制。

- `data.json`
	- 示例场景参数文件。
	- 里面记录了 OBJ 路径、物体变换、摄像机位置、方向基向量和视角等信息。

- `data.obj`
	- 与 `data.json` 配套的网格文件。
	- 插件会读取 OBJ 几何数据，并结合 JSON 中的摄像机参数完成投射。

- `Export_CameraProjection_JSON.mse`
	- 3ds Max 导出脚本的封装版本，供美术或动画流程直接使用。

- `howto.txt`
	- 安装说明。
	- 当前说明要求关闭 AE 后，将 `CameraProjection.aex`、`data.json`、`data.obj` 复制到 AE 安装目录下的 `Plug-ins` 文件夹，再重新打开 AE。
	- 说明中还提到该插件目前主要在 Windows 环境下的 AE 2024 中完成测试。

一句话概括：`Plugins` 负责“给 AE 直接运行的插件和示例数据”。

### FBX

`FBX` 目录提供 DCC 软件侧使用的参考资源。

根据 `readme.txt`，这一目录中的 FBX 文件主要用于：

- 导入到 DCC 软件中辅助动画制作。
- 允许整体位移，但尽量避免缩放和破坏相对位置关系。
- 避免在制作过程中随意改动摄像机参数。
- 作为后续导出 JSON/OBJ 的参考基准。

当前说明里还明确了两点：

- 现阶段参数导出程序仅适配 3ds Max，其他 DCC 软件还在适配中。
- 导入后建议输出分辨率设置为 `2000 x 1600`，或保持相同比例的分辨率。

一句话概括：`FBX` 负责“DCC 制作阶段的参考场景和动画对位基准”。

### AE_Project

`AE_Project` 是 AE 侧的示例与验收目录，当前包含：

- `裸眼动画校准.aep`
	- After Effects 示例工程。
	- 用于打开后直接查看插件接入后的工程配置、图层组织以及校准结果。

- `裸眼动画01_preview.avi`
	- 对应的预览视频。
	- 适合在不打开工程的情况下快速查看最终效果或对照输出结果。

一句话概括：`AE_Project` 负责“演示项目效果和提供校验参考”。

## 推荐工作流

1. 在 DCC 软件中导入 `FBX` 里的参考资源，按照既定相机和相对位置制作动画。
2. 在 3ds Max 中使用 `Export_CameraProjection_JSON.ms` 或 `Export_CameraProjection_JSON.mse` 导出 `.obj` 和 `.json`。
3. 将 `Plugins` 中的 `CameraProjection.aex`、`data.json`、`data.obj` 放到 AE 的 `Plug-ins` 目录。
4. 在 AE 中打开示例工程，或将插件应用到目标图层，选择对应的 OBJ 和 JSON 数据。
5. 通过插件里的调试选项检查线框、调试信息和投射状态，确认相机与模型匹配是否正确。

## 适合谁看这个仓库

- 需要把 DCC 中的相机和网格关系同步到 AE 的技术美术。
- 需要维护或继续开发 AE 原生插件的开发者。
- 需要复用裸眼动画校准流程的项目成员。

如果后续要继续扩展仓库，建议把 `Plugins` 中示例数据的来源、AE 中的使用步骤，以及 3ds Max 导出脚本的操作方式再单独补成一份更细的使用文档。
