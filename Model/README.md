# Model Directory

This directory contains AI model files, configuration files, and build scripts for the NE301 project.

## Directory Structure

```
Model/
├── docs/                      # Documentation
│   └── how_to_train_quant_deploy_yolov8n.md  # Model training and deployment guide
├── weights/                   # Model files (.tflite) and configuration files (.json)
├── mpools/                    # Memory pool configuration files (.mpool)
├── neural_art_reloc.json     # Neural Art relocation configuration
├── Makefile                   # Build system for model packaging
└── README.md                  # This file
```

## File Description

### `weights/`
Contains TensorFlow Lite (`.tflite`) or ONNX (`.onnx`) model files and their corresponding JSON configuration files (`.json`). Each model must have:
- A `.tflite` or `.onnx` file: The quantized model
- A `.json` file: Model metadata including input/output specifications, post-processing parameters, etc.

### `mpools/`
Memory pool configuration files for different model types and optimization profiles. These files define memory allocation strategies for the STM32N6 NPU.

### `neural_art_reloc.json`
Neural Art relocation configuration file that defines compilation profiles for different model types. Each profile specifies:
- Memory pool file
- Compiler optimization options
- Epoch controller settings

### `Makefile`
Build system that automates:
1. Model relocation (converting TFLite to relocatable binary)
2. Model packaging (combining binary with JSON metadata)
3. Output generation (`ne301_Model.bin`)

**Usage:**
```bash
# Build model package
make model

make model MODEL_NAME=yolo26_256_qdq_int8_od_coco-person-st 

# Show configuration
make info

# Clean build files
make clean
```

### `docs/how_to_train_quant_deploy_yolov8n.md`
Complete guide for training, quantizing, and deploying YOLOv8 models to NE301 devices.

## Supported Model Types

The following table lists all post-processing types defined in `app_postprocess.h`. The **Status** column indicates verification results:
- ✅ = Verified and working on device
- (empty) = Not yet verified

### Object Detection (OD)

| Status | Postprocess Type | C Define | Description | Input | Output | Verification |
|--------|-----------------|----------|-------------|-------|--------|-------------|
| | `pp_od_yolo_v2_uf` | `POSTPROCESS_OD_YOLO_V2_UF` (100) | YOLOv2 object detection | uint8 | float32 | — |
| | `pp_od_yolo_v2_ui` | `POSTPROCESS_OD_YOLO_V2_UI` (101) | YOLOv2 object detection | uint8 | int8 | — |
| | `pp_od_yolo_v5_uu` | `POSTPROCESS_OD_YOLO_V5_UU` (102) | YOLOv5 object detection | uint8 | uint8 | — |
| ✅ | `pp_od_yolo_v8_uf` | `POSTPROCESS_OD_YOLO_V8_UF` (103) | YOLOv8 object detection | uint8 | float32 | ✅ yolov8n 398ms (coco, person-st) |
| ✅ | `pp_od_yolo_v8_ui` | `POSTPROCESS_OD_YOLO_V8_UI` (104) | YOLOv8 object detection | uint8 | int8 | ✅ yolov8n 415ms (coco) |
| ✅ | `pp_od_yolo_v8_ui` | `POSTPROCESS_OD_YOLO_V8_UI` (104) | YOLOv8 object detection (meter) | uint8 | int8 | ✅ yolov8n 8/8 digits recognized |
| ✅ | `pp_od_yolo_v11_uf` | — | YOLO11n object detection | uint8 | float32 | ✅ yolo11n 348ms (person-st) |
| ✅ | `pp_od_st_yolox_uf` | `POSTPROCESS_OD_ST_YOLOX_UF` (105) | ST YOLOX object detection | uint8 | float32 | ✅ nano_480 passed || | `pp_od_st_yolox_ui` | `POSTPROCESS_OD_ST_YOLOX_UI` (106) | ST YOLOX object detection | uint8 | int8 | — |
| | `pp_od_st_ssd_uf` | `POSTPROCESS_OD_ST_SSD_UF` (107) | ST SSD object detection | uint8 | float32 | — |
| | `pp_od_blazeface_uf` | `POSTPROCESS_OD_BLAZEFACE_UF` (110) | BlazeFace face detection (OD mode) | uint8 | float32 | — |
| | `pp_od_blazeface_uu` | `POSTPROCESS_OD_BLAZEFACE_UU` (111) | BlazeFace face detection (OD mode) | uint8 | uint8 | — |
| | `pp_od_blazeface_ui` | `POSTPROCESS_OD_BLAZEFACE_UI` (112) | BlazeFace face detection (OD mode) | uint8 | int8 | — |

### Multi-Person Pose Estimation (MPE)

| Status | Postprocess Type | C Define | Description | Input | Output | Verification |
|--------|-----------------|----------|-------------|-------|--------|-------------|
| ✅ | `pp_mpe_yolo_v8_uf` | `POSTPROCESS_MPE_YOLO_V8_UF` (200) | YOLOv8 multi-person pose estimation | uint8 | float32 | ✅ 3 poses (87%/81%/77%), 17 keypoints each |
| ✅ | `pp_mpe_yolo_v8_uf` | `POSTPROCESS_MPE_YOLO_V8_UF` (200) | YOLO11n multi-person pose estimation | uint8 | float32 | ✅ yolo11n 3 poses (80%/70%/63%), 17 kpts |
| ✅ | `pp_mpe_yolo_v8_ui` | `POSTPROCESS_MPE_YOLO_V8_UI` (201) | YOLOv8 multi-person pose estimation | uint8 | int8 | ✅ yolov8n 3 poses (86%/81%/74%), 17 kpts |
| | `pp_mpe_pd_uf` | `POSTPROCESS_MPE_PD_UF` (202) | Palm detector | uint8 | float32 | — |

### Face Detection (FD)

| Status | Postprocess Type | C Define | Description | Input | Output | Verification |
|--------|-----------------|----------|-------------|-------|--------|-------------|
| ✅ | `pp_fd_blazeface_ui` | `POSTPROCESS_FD_BLAZEFACE_UI` (113) | BlazeFace face detection with keypoints | uint8 | int8 | ✅ 1 face 89%, 6 keypoints (eyes/nose/mouth/ears), 5 connections |

### Single Person Pose Estimation (SPE)

| Status | Postprocess Type | C Define | Description | Input | Output | Verification |
|--------|-----------------|----------|-------------|-------|--------|-------------|
| | `pp_spe_movenet_uf` | `POSTPROCESS_SPE_MOVENET_UF` (203) | MoveNet single person pose estimation | uint8 | float32 | — |
| | `pp_spe_movenet_ui` | `POSTPROCESS_SPE_MOVENET_UI` (204) | MoveNet single person pose estimation | uint8 | int8 | — |

### Instance Segmentation (ISEG)

| Status | Postprocess Type | C Define | Description | Input | Output | Verification |
|--------|-----------------|----------|-------------|-------|--------|-------------|
| ✅ | `pp_iseg_yolo_v8_ui` | `POSTPROCESS_ISEG_YOLO_V8_UI` (300) | YOLOv8 instance segmentation | uint8 | int8 | ✅ 4 segments (person 86%/83%/77%/73%) |

### Semantic Segmentation (SSEG)

| Status | Postprocess Type | C Define | Description | Input | Output | Verification |
|--------|-----------------|----------|-------------|-------|--------|-------------|
| | `pp_sseg_deeplab_v3_uf` | `POSTPROCESS_SSEG_DEEPLAB_V3_UF` (400) | DeepLabV3 semantic segmentation | uint8 | float32 | — |
| | `pp_sseg_deeplab_v3_ui` | `POSTPROCESS_SSEG_DEEPLAB_V3_UI` (401) | DeepLabV3 semantic segmentation | uint8 | int8 | — |

### Custom

| Status | Postprocess Type | C Define | Description | Input | Output | Verification |
|--------|-----------------|----------|-------------|-------|--------|-------------|
| | `pp_custom` | `POSTPROCESS_CUSTOM` (1000) | Custom post-processing (user implementation) | - | - | — |

### Notes
- **UF**: uint8 input, float32 output (higher precision, larger memory footprint)
- **UI**: uint8 input, int8 output (better performance, lower memory usage, recommended)
- **UU**: uint8 input, uint8 output
- **int8 output models**: `output_spec.scale` must not be default 1.0, use `0.003921569` (1/255) instead
- To add support for a new type, update this table by adding ✅ to the Status column

## Quick Start

### 1. Add a New Model

1. Place your `.tflite` or `.onnx` model file in `weights/`
2. Create a corresponding `.json` configuration file (see examples in `weights/`)
3. Update `Model/Makefile` to set `MODEL_NAME`, `MODEL_TFLITE`, and `MODEL_JSON`

### 2. Build Model Package

```bash
cd Model
make pkg-model
```

The output will be in `build/ne301_Model_xxx_pkg.bin`

### 3. Deploy to Device

From project root:
```bash
make flash-model
```

## Configuration Guide

### JSON Configuration File Structure

Each model requires a JSON configuration file with the following key sections:

1. **input_spec**: Input image dimensions, data type, normalization
2. **output_spec**: Model output dimensions, data type, quantization parameters
3. **postprocess_type**: Post-processing type (see supported types above)
4. **postprocess_params**: Post-processing parameters (thresholds, class names, etc.)

For detailed configuration instructions, see [docs/how_to_train_quant_deploy_yolov8n.md](docs/how_to_train_quant_deploy_yolov8n.md)

## Build System

The Makefile uses the following scripts:
- `../Script/generate-reloc-model.sh`: Converts TFLite to relocatable binary
- `../Script/model_packager.py`: Packages binary with JSON metadata

## References

- [Model Training & Deployment Guide](docs/how_to_train_quant_deploy_yolov8n.md)
- [Model Packaging Documentation](../Script/docs/MODEL_PACK.md)
- [Project README](../README.md)
- [stm32ai-modelzoo-services](https://github.com/STMicroelectronics/stm32ai-modelzoo-services)
- [stm32ai-modelzoo](https://github.com/STMicroelectronics/stm32ai-modelzoo/)

---

**Last Updated:** 2026-06-03
