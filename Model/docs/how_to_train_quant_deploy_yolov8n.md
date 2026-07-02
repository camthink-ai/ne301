# YOLOv8 Model Training, Quantization, and Deployment Guide

This document introduces two methods to complete YOLOv8 model training, quantization, and deployment to NE301 devices:

- **Method 1: Using AI Tool Stack (Recommended)** - Use graphical tools to complete the full workflow from data collection, annotation, training, quantization to deployment
- **Method 2: Manual Command Line Method** - Manually complete each step using command line tools

---

## Method 1: Using AI Tool Stack (Recommended)

[AI Tool Stack](https://github.com/camthink-ai/AIToolStack) is an end-to-end edge AI toolkit dedicated to NeoEyes NE301, covering the complete workflow of data collection, annotation, training, quantization, and deployment.

### 1.1 Environment Setup

**Prerequisites:**
- Docker & docker-compose (required)
- If you need to generate NE301 quantized model packages, pull the image in advance:
  ```sh
  docker pull camthink/ne301-dev:v4.0
  ```

### 1.2 Deploy AI Tool Stack

```sh
# Clone repository
git clone https://github.com/camthink-ai/AIToolStack.git
cd AIToolStack

# Deploy with Docker
docker-compose build
docker-compose up
```

> **Note:** Configuration parameters are now defined in the configuration file.  
> To modify parameters such as `MQTT_BROKER_HOST`, please edit the environment variables in `docker-compose.yml`.  
> Ensure the address is accessible by NE301 devices (usually use the host machine's actual IP address, not `localhost`).

### 1.3 Data Collection

1. **Configure MQTT Connection**
   - Configure MQTT connection on NE301 device, pointing to AI Tool Stack's MQTT broker
   - Device will automatically upload collected image data via MQTT

2. **Create AI Model Project**
   - Create a new project in AI Tool Stack
   - Supports multi-device access with real-time viewing and filtering of collection progress

3. **Data Management**
   - Unified management of collected image data in project space
   - Supports real-time viewing of collection progress and filtering

### 1.4 Data Annotation

1. **Enter Annotation Workbench**
   - Select the dataset to be annotated in the project
   - Open the annotation workbench

2. **Efficient Annotation**
   - Provides shortcut-driven efficient annotation workflow
   - Supports multiple annotation types such as object detection and classification
   - Built-in class management for flexible label addition and deletion

3. **Dataset Import/Export**
   - Supports import and export in COCO / YOLO / project annotation ZIP formats
   - Can export annotated data for subsequent training

### 1.5 Model Training

1. **Configure Training Parameters**
   - Set training parameters in the training tool (epochs, batch size, learning rate, etc.)
   - Custom dataset allocation (train/validation split ratio)

2. **Start Training**
   - YOLO architecture-based model training tool
   - Currently supports yolov8n, with more models and algorithm support to be added in the future
   - Training functionality depends on the ultralytics/ultralytics project

3. **View Training Results**
   - Real-time viewing of training logs
   - View training result reports and metrics

### 1.6 Model Quantization and Deployment

1. **Model Quantization**
   - Integrated NE301 quantization tools
   - Automatically completes model quantization, generating quantized models suitable for NE301 devices

2. **Model Packaging**
   - One-click export of model file packages suitable for NE301 devices
   - Automatic compatibility checking and inference speed evaluation

3. **Deploy to Device**
   - Deploy to edge AI devices without coding
   - Supports direct model deployment through Web interface

### 1.7 Model Space Management

- **Version Management**: Each trained and quantized model is automatically saved as an independent version, can be rolled back or exported at any time
- **Model Testing**: Supports result testing of different model versions to help select the best model for deployment to devices
- **External Model Quantization**: Supports importing existing YOLO models and quantizing them into NE301 model resources without retraining

### 1.8 Complete Workflow

AI Tool Stack works with devices to complete the following full cycle:

1. **Device Raw Image Data Collection** → Automatically upload via MQTT
2. **Data Annotation** → Complete in annotation workbench
3. **Model Training** → Use built-in training tools
4. **Model Quantization** → Automatically complete NE301 optimization
5. **Edge Deployment** → One-click deployment to device
6. **Image Collection** → Continue collecting new data
7. **Dataset Enrichment** → Add newly annotated data
8. **Retraining** → Use enhanced dataset
9. **Redeployment** → Update device model

> **Detailed Documentation:** To understand the complete workflow of AI Tool Stack with NE301, please read the detailed documentation: [NE301 and AI Tool Stack Guide](https://github.com/camthink-ai/AIToolStack)

---

## Method 2: Manual Command Line Method

### 2.1 Environment Setup

#### 2.1.1 Install YOLO Training Environment

Refer to the official [ultralytics](https://github.com/ultralytics/ultralytics) installation guide. Install via Docker:

```sh
sudo docker pull ultralytics/ultralytics:latest-export
```

#### 2.1.2 Enter Docker Container (with GPU)

```sh
sudo docker run -it --ipc=host --runtime=nvidia --gpus all -v ./your/host/path:/ultralytics/output ultralytics/ultralytics:latest-export /bin/bash
```

#### 2.1.3 Verify YOLO Environment

```sh
yolo detect predict model=yolov8n.pt source='https://ultralytics.com/images/bus.jpg' device=0
```

**Expected Output:**
```output
Downloading https://github.com/ultralytics/assets/releases/download/v8.3.0/yolov8n.pt to 'yolov8n.pt': 100% ━━━━━━━━━━━━ 6.2MB 1.6MB/s 4.0s
Ultralytics 8.3.213 🚀 Python-3.11.13 torch-2.8.0+cu128 CUDA:0 (NVIDIA A800 80GB PCIe, 81051MiB)
YOLOv8n summary (fused): 72 layers, 3,151,904 parameters, 0 gradients, 8.7 GFLOPs

Downloading https://ultralytics.com/images/bus.jpg to 'bus.jpg': 100% ━━━━━━━━━━━━ 134.2KB 1.2MB/s 0.1s
image 1/1 /ultralytics/bus.jpg: 640x480 4 persons, 1 bus, 1 stop sign, 64.5ms
Speed: 4.9ms preprocess, 64.5ms inference, 117.6ms postprocess per image at shape (1, 3, 640, 480)
Results saved to /ultralytics/runs/detect/predict
💡 Learn more at https://docs.ultralytics.com/modes/predict
```

#### 2.1.4 Install NE301 Project Deployment Environment

To deploy models to NE301 devices, you need to set up the project development environment. Please refer to the [SETUP.md](../../SETUP.md) document in the project root directory for environment setup.

### 2.2 Model Training and Export

#### 2.2.1 Train Model (Optional)

```sh
# Based on COCO pre-trained model
yolo detect train data=data.yaml model=yolov8n.pt epochs=100 imgsz=256 device=0

# Or train from scratch
yolo detect train data=data.yaml model=yolov8n.yaml epochs=100 imgsz=256 device=0
```

#### 2.2.2 Export to TFLite Format

```sh
yolo export model=yolov8n.pt format=tflite imgsz=256 int8=True data=data.yaml fraction=0.2
```

**Example Output:**
```output
TensorFlow SavedModel: export success ✅ 35.3s, saved as 'yolov8n_saved_model' (40.1 MB)

TensorFlow Lite: starting export with tensorflow 2.19.0...
TensorFlow Lite: export success ✅ 0.0s, saved as 'yolov8n_saved_model/yolov8n_int8.tflite' (3.2 MB)

Export complete (35.4s)
Results saved to /ultralytics
Predict:         yolo predict task=detect model=yolov8n_saved_model/yolov8n_int8.tflite imgsz=256 int8 
Validate:        yolo val task=detect model=yolov8n_saved_model/yolov8n_int8.tflite imgsz=256 data=coco.yaml int8 
Visualize:       https://netron.app
💡 Learn more at https://docs.ultralytics.com/modes/export
```

### 2.3 Model Quantization

#### 2.3.1 Download Quantization Tools and Dataset

- Download quantization scripts and configuration files:
  - [tflite_quant.py](https://github.com/STMicroelectronics/stm32ai-modelzoo-services/blob/main/tutorials/scripts/yolov8_quantization/tflite_quant.py)
  - [user_config_quant.yaml](https://github.com/STMicroelectronics/stm32ai-modelzoo-services/blob/main/tutorials/scripts/yolov8_quantization/user_config_quant.yaml)

- Download quantization validation dataset: [coco8](https://github.com/ultralytics/assets/releases/download/v0.0.0/coco8.zip)

#### 2.3.2 Configure Quantization Parameters

Modify the configuration file `user_config_quant.yaml`:

```yaml
model:
    name: yolov8n_256
    uc: od_coco
    model_path: ./yolov8n_saved_model
    input_shape: [256, 256, 3]
quantization:
    fake: False
    quantization_type: per_channel
    quantization_input_type: uint8 # float
    quantization_output_type: int8 # float
    calib_dataset_path: ./coco8/images/val # Calibration dataset, important! Can use part of training set
    export_path: ./quantized_models
pre_processing:
    rescaling: {scale : 255, offset : 0}
```

#### 2.3.3 Execute Quantization

```sh
# Install dependencies
pip install hydra-core munch

# Start quantization
python tflite_quant.py --config-name user_config_quant.yaml
```

**Example Output:**
```output
WARNING: All log messages before absl::InitializeLog() is called are written to STDERR
W0000 00:00:1760435045.538259     834 tf_tfl_flatbuffer_helpers.cc:365] Ignored output_format.
W0000 00:00:1760435045.538286     834 tf_tfl_flatbuffer_helpers.cc:368] Ignored drop_control_dependency.
I0000 00:00:1760435045.567572     834 mlir_graph_optimization_pass.cc:425] MLIR V1 optimization pass is not enabled
100%|██████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████| 32/32 [00:05<00:00,  5.46it/s]
fully_quantize: 0, inference_type: 6, input_inference_type: UINT8, output_inference_type: INT8
Quantized model generated: yolov8n_256_quant_pc_ui_od_coco.tflite
```

#### 2.3.4 Evaluate Quantized Model (Optional)

```sh
yolo val model=./quantized_models/yolov8n_256_quant_pc_ui_od_coco.tflite data=coco.yaml imgsz=256
```

### 2.4 Deploy Model to NE301 Device

#### 2.4.1 Prepare Model Files

Copy the quantized model file to the project `Model/weights/` directory and create or modify the corresponding JSON configuration file (model metadata):

```sh
# Navigate to project root directory
cd /path/to/ne301

# Copy model file
cp /your/path/quantized_models/yolov8n_256_quant_pc_ui_od_coco.tflite Model/weights/

# Create corresponding JSON configuration file
# Refer to example files in Model/weights/ directory
```

**Create JSON Configuration File**

The JSON configuration file needs to be configured according to the actual model. Key configuration items are as follows:

**Key Configuration Items:**

1. **input_spec**: Input specification
   - `width/height`: Model input dimensions (e.g., 256)
   - `data_type`: Input data type (`uint8` or `float32`)
   - `normalization`: Normalization parameters (uint8 typically uses `mean: [0,0,0]`, `std: [255,255,255]`)

2. **output_spec**: Output specification
   - `height/width`: Output dimensions (check actual model output, YOLOv8 typically uses `height: 84`, `width: 1344`)
   - `data_type`: Output data type (`int8` or `float32`)
   - `scale/zero_point`: Quantization parameters (must match model quantization parameters)

3. **postprocess_type**: Post-processing type
   - `pp_od_yolo_v8_uf`: uint8 input, float32 output
   - `pp_od_yolo_v8_ui`: uint8 input, int8 output (recommended)

4. **postprocess_params**: Post-processing parameters
   - `num_classes`: Number of classes (COCO=80)
   - `class_names`: List of class names (must match training order)
   - `confidence_threshold`: Confidence threshold (0.0-1.0)
   - `iou_threshold`: IoU threshold for NMS (0.0-1.0)
   - `max_detections`: Maximum number of detection boxes
   - `total_boxes`: Total number of boxes (YOLOv8 256x256 typically uses 1344)
   - `raw_output_scale/zero_point`: Must match quantization parameters in `output_spec`

**Reference Examples:** Refer to existing JSON files in the `Model/weights/` directory as templates. Use tools (such as Netron) to view model output dimensions.

#### 2.4.2 Build and Deploy Using Makefile

The project provides a Makefile to simplify the build and deployment process:

**Step 1: Configure Model**

Modify the model configuration in `Model/Makefile`:

```makefile
MODEL_NAME = yolov8n_256_quant_pc_ui_od_coco
MODEL_TFLITE = $(WEIGHTS_DIR)/$(MODEL_NAME).tflite
MODEL_JSON = $(WEIGHTS_DIR)/$(MODEL_NAME).json
```

**Step 2: Build Model**

```sh
# In project root directory
make model
make pkg-model

# Build result is in build/ne301_Model_{vresion}_pkg.bin
```

**Step 3: Flash to Device**

```sh
# In project root directory
make flash-model
```

**Notes:**
- `make model` automatically completes all steps including model conversion and packaging
- `make flash-model` flashes the model to device address `0x70900000`
- For detailed information, please refer to [Script/docs/MODEL_PACK.md](../../Script/docs/MODEL_PACK.md)

#### 2.4.3 Verify Deployment

After deployment, you can verify through the following methods:

1. **Check Device Logs**: Confirm model loaded successfully
2. **Web Interface**: Access device web interface to view model information
3. **Functional Testing**: Use camera for object detection testing

---

## Reference Documentation

- **AI Tool Stack**: [https://github.com/camthink-ai/AIToolStack](https://github.com/camthink-ai/AIToolStack)
- Project environment setup: [SETUP.md](../../SETUP.md)
- Model packaging details: [Script/docs/MODEL_PACK.md](../../Script/docs/MODEL_PACK.md)
- OTA packaging guide: [Script/docs/OTA_PACK.md](../../Script/docs/OTA_PACK.md)
- Project build system: [README.md](../../README.md)
