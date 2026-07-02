 /**
 ******************************************************************************
 * @file    app_postprocess.h
 * @author  GPM Application Team
 *
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __PP_H
#define __PP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct {
	float score_threshold;
	float nms_threshold;
	uint32_t max_detections;
	uint32_t input_width;
	uint32_t input_height;
} pp_params_t;

typedef enum {
	PP_TYPE_NONE = 0,
	PP_TYPE_OD,
	PP_TYPE_MPE,
	PP_TYPE_SEG,
	PP_TYPE_CLASS,
	PP_TYPE_PD,
	PP_TYPE_SPE,
	PP_TYPE_ISEG,
	PP_TYPE_SSEG,
} pp_type_t;

/* OD postprocess output */
typedef struct
{
	float x;
	float y;
	float width;
	float height;
	float conf;
	char *class_name;
} od_detect_t;

typedef struct {
	od_detect_t *detects;
	uint8_t nb_detect;
}pp_od_out_t;

/* MPE postprocess output */
typedef struct {
	float x;
	float y;
	float conf;
} keypoint_t;

typedef struct {
	float x;
	float y;
	float width;
	float height;
	float conf;
	char *class_name;
	keypoint_t keypoints[33];
	uint32_t nb_keypoints;
	char **keypoint_names;  // array of C strings (may be NULL entries)
	uint8_t	num_connections;		// number of connections
	uint8_t *keypoint_connections;  // flattened pairs: [from0, to0, from1, to1, ...]
} mpe_detect_t;

typedef struct {
	mpe_detect_t *detects;
	uint8_t nb_detect;
}pp_mpe_out_t;

/* Class postprocess output */
typedef struct {

}pp_class_out_t;

/* SPE (Single Person Pose Estimation) postprocess output */
typedef keypoint_t spe_keypoint_t;  // same layout as the MPE keypoint

typedef struct {
	spe_keypoint_t *keypoints;
	uint32_t nb_keypoints;
	char **keypoint_names;  // array of C strings (may be NULL entries)
	uint8_t num_connections;
	uint8_t *keypoint_connections;  // flattened pairs: [from0, to0, from1, to1, ...]
}pp_spe_out_t;

/* ISEG (Instance Segmentation) postprocess output */
typedef struct {
	float x;
	float y;
	float width;
	float height;
	float conf;
	char *class_name;
	uint8_t *mask;  // mask buffer
	uint32_t mask_size;  // mask size (width * height)
} iseg_detect_t;

typedef struct {
	iseg_detect_t *detects;
	uint8_t nb_detect;
}pp_iseg_out_t;

/* SSEG (Semantic Segmentation) postprocess output */
typedef struct {
	uint8_t *class_map;  // class map array (width * height)
	uint32_t width;
	uint32_t height;
	uint32_t num_classes;
	char **class_names;  // array of C strings (may be NULL entries)
}pp_sseg_out_t;

/* Postprocess result */
typedef struct {
	pp_type_t type;
	uint8_t is_valid;
	union {
		pp_od_out_t od;
		pp_mpe_out_t mpe;
		pp_class_out_t class;
		pp_spe_out_t spe;
		pp_iseg_out_t iseg;
		pp_sseg_out_t sseg;
	};
}pp_result_t;

typedef struct {
	int32_t  (*init)(const char *json_str, void **params, void *nn_inst);
	int32_t  (*run)(void *pInput[], uint32_t nb_input, void *pResult, void *pInput_param, void *nn_inst);
	int32_t (*set_confidence_threshold)(void *params, float threshold);
	int32_t (*get_confidence_threshold)(void *params, float *threshold);
	int32_t (*set_nms_threshold)(void *params, float threshold);
	int32_t (*get_nms_threshold)(void *params, float *threshold);
	int32_t (*deinit)(void *params);
} pp_vtable_t;

typedef struct {
	const char *name;
	const pp_vtable_t *vt;
} pp_entry_t;

// Post-processing module initialization/deinitialization
int32_t pp_init(void);
void pp_deinit(void);

// Find post-processing implementation
const pp_vtable_t* pp_find(const char *name);

// support model list
int32_t pp_model_support_list(char **list, uint32_t *nb_models);

#ifdef __cplusplus
}
#endif
#endif /*__APP_POSTPROCESS_H */

