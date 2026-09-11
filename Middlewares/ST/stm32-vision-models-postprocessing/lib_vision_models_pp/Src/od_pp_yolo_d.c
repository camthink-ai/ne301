/*---------------------------------------------------------------------------------------------
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file in
 * the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *--------------------------------------------------------------------------------------------*/

#include "od_pp_loc.h"
#include "od_yolo_d_pp_if.h"
#include "vision_models_pp.h"

/* Can't be removed if qsort is not re-written... */
static int32_t AI_YOLO_D_PP_SORT_CLASS;

typedef struct
{
  int8_t  x_center;
  int8_t  y_center;
  int8_t  width;
  int8_t  height;
  int16_t conf;
  uint8_t class_index;
  uint8_t _u32_align;
} od_yolo_d_pp_scratch_s8_t;

int32_t yolov_d_nms_comparator(const void *pa, const void *pb)
{
  od_pp_outBuffer_t a = *(od_pp_outBuffer_t *)pa;
  od_pp_outBuffer_t b = *(od_pp_outBuffer_t *)pb;

  float32_t diff = 0.0;
  float32_t a_weighted_conf = 0.0;
  float32_t b_weighted_conf = 0.0;

  if (a.class_index == AI_YOLO_D_PP_SORT_CLASS)
  {
    a_weighted_conf = a.conf;
  }
  else
  {
    a_weighted_conf = 0.0;
  }

  if (b.class_index == AI_YOLO_D_PP_SORT_CLASS)
  {
    b_weighted_conf = b.conf;
  }
  else
  {
    b_weighted_conf = 0.0;
  }

  diff = a_weighted_conf - b_weighted_conf;

  if (diff < 0) return 1;
  else if (diff > 0) return -1;
  return 0;
}

int32_t yolov_d_nms_comparator_is8(const void *pa, const void *pb)
{
  od_yolo_d_pp_scratch_s8_t a = *(od_yolo_d_pp_scratch_s8_t *)pa;
  od_yolo_d_pp_scratch_s8_t b = *(od_yolo_d_pp_scratch_s8_t *)pb;

  int16_t diff = 0;
  int16_t a_weighted_conf = INT16_MIN;
  int16_t b_weighted_conf = INT16_MIN;

  if (a.class_index == AI_YOLO_D_PP_SORT_CLASS)
  {
    a_weighted_conf = a.conf;
  }
  else
  {
    a_weighted_conf = INT8_MIN;
  }

  if (b.class_index == AI_YOLO_D_PP_SORT_CLASS)
  {
    b_weighted_conf = b.conf;
  }
  else
  {
    b_weighted_conf = INT8_MIN;
  }

  diff = a_weighted_conf - b_weighted_conf;

  if (diff < 0) return 1;
  else if (diff > 0) return -1;
  return 0;
}


int32_t yolov_d_pp_nmsFiltering_centroid(od_pp_out_t *pOutput,
                                        od_yolo_d_pp_static_param_t *pInput_static_param)
{
  int32_t j, k, limit_counter, detections_per_class;

  for (k = 0; k < pInput_static_param->nb_classes; ++k)
  {
    limit_counter = 0;
    detections_per_class = 0;
    AI_YOLO_D_PP_SORT_CLASS = k;

    /* Counts the number of detections with class k */
    for (int32_t i = 0; i < pInput_static_param->nb_detect ; i ++)
    {
      if(pOutput->pOutBuff[i].class_index == k)
      {
          detections_per_class++;
      }
    }

    if (detections_per_class > 0)
    {
      /* Sorts detections based on class k */
      qsort(pOutput->pOutBuff,
            pInput_static_param->nb_detect,
            sizeof(od_pp_outBuffer_t),
            yolov_d_nms_comparator);

      for (int32_t i = 0; i < detections_per_class ; i ++)
      {
        if (pOutput->pOutBuff[i].conf == 0) continue; // Already filtered
        float32_t *a = &(pOutput->pOutBuff[i].x_center);
        for (j = i + 1; j < detections_per_class; j ++)
        {
          if (pOutput->pOutBuff[j].conf == 0) continue; // Already filtered
          float32_t *b = &(pOutput->pOutBuff[j].x_center);
          if (vision_models_box_iou(a, b) > pInput_static_param->iou_threshold)
          {
            pOutput->pOutBuff[j].conf = 0;
          }
        }
      }

      /* Limits detections count */
      for (int32_t i = 0; i < detections_per_class; i++)
      {
        if ((limit_counter < pInput_static_param->max_boxes_limit) &&
            (pOutput->pOutBuff[i].conf != 0))
        {
          limit_counter++;
        }
        else
        {
          pOutput->pOutBuff[i].conf = 0;
        }
      } // for detection_per_class
    } // if detection_per_class
  } // for nb_classes
  return (AI_OD_POSTPROCESS_ERROR_NO);
}

int32_t yolov_d_pp_nmsFiltering_centroid_is8(od_yolo_d_pp_scratch_s8_t *ptrScratch,
                                            od_yolo_d_pp_static_param_t *pInput_static_param)
{
  int32_t j, k, limit_counter, detections_per_class;

  for (k = 0; k < pInput_static_param->nb_classes; ++k)
  {
    limit_counter = 0;
    detections_per_class = 0;
    AI_YOLO_D_PP_SORT_CLASS = k;

    /* Counts the number of detections with class k */
    for (int32_t i = 0; i < pInput_static_param->nb_detect ; i ++)
    {
      if(ptrScratch[i].class_index == k)
      {
        detections_per_class++;
      }
    }

    if (detections_per_class > 0)
    {
      /* Sorts detections based on class k */
      qsort(ptrScratch,
            pInput_static_param->nb_detect,
            sizeof(od_yolo_d_pp_scratch_s8_t),
            yolov_d_nms_comparator_is8);

      for (int32_t i = 0; i < detections_per_class ; i ++)
      {
        if (ptrScratch[i].conf == INT16_MIN) continue; // Already filtered
        int8_t *a = &(ptrScratch[i].x_center);
        for (j = i + 1; j < detections_per_class; j ++)
        {
          if (ptrScratch[j].conf == INT16_MIN) continue; // Already filtered
          int8_t *b = &(ptrScratch[j].x_center);
          if (vision_models_box_iou_is8(a, b, pInput_static_param->raw_output_zero_point) > pInput_static_param->iou_threshold)
          {
            ptrScratch[j].conf = INT16_MIN;
          }
        }
      }

      /* Limits detections count */
      for (int32_t i = 0; i < detections_per_class; i++)
      {
        if ((limit_counter < pInput_static_param->max_boxes_limit) &&
            (ptrScratch[i].conf != INT16_MIN))
        {
          limit_counter++;
        }
        else
        {
          ptrScratch[i].conf = INT16_MIN;
        }
      } // for detection_per_class
    } // if detection_per_class
  } // for nb_classes
  return (AI_OD_POSTPROCESS_ERROR_NO);
}

int32_t yolov_d_pp_scoreFiltering_centroid(od_pp_out_t *pOutput,
                                          od_yolo_d_pp_static_param_t *pInput_static_param)
{
  int32_t det_count = 0;

  for (int32_t i = 0; i < pInput_static_param->nb_detect; i++)
  {
    if (pOutput->pOutBuff[i].conf >= pInput_static_param->conf_threshold)
    {
      if (det_count >= pInput_static_param->max_boxes_limit) {
        break;
      }
      pOutput->pOutBuff[det_count].x_center    = pOutput->pOutBuff[i].x_center;
      pOutput->pOutBuff[det_count].y_center    = pOutput->pOutBuff[i].y_center;
      pOutput->pOutBuff[det_count].width       = pOutput->pOutBuff[i].width;
      pOutput->pOutBuff[det_count].height      = pOutput->pOutBuff[i].height;
      pOutput->pOutBuff[det_count].conf        = pOutput->pOutBuff[i].conf;
      pOutput->pOutBuff[det_count].class_index = pOutput->pOutBuff[i].class_index;
      det_count++;
    }
  }
  pOutput->nb_detect = det_count;

  return (AI_OD_POSTPROCESS_ERROR_NO);
}

int32_t yolov_d_pp_scoreFiltering_centroid_is8(od_yolo_d_pp_scratch_s8_t *ptrScratch,
                                              od_pp_out_t *pOutput,
                                              od_yolo_d_pp_static_param_t *pInput_static_param)
{
  int32_t det_count = 0;
  int8_t zero_point = pInput_static_param->raw_output_zero_point;
  float32_t scale = pInput_static_param->raw_output_scale;
  int16_t conf_threshold_s16 = (int16_t)(pInput_static_param->conf_threshold / (scale * scale) + 0.5f);

  for (int32_t i = 0; i < pInput_static_param->nb_detect; i++)
  {
    if (ptrScratch[i].conf >= conf_threshold_s16)
    {
      if (det_count >= pInput_static_param->max_boxes_limit) {
        break;
      }
      pOutput->pOutBuff[det_count].x_center    = scale * (float32_t)((int32_t)ptrScratch[i].x_center    - (int32_t)zero_point);
      pOutput->pOutBuff[det_count].y_center    = scale * (float32_t)((int32_t)ptrScratch[i].y_center    - (int32_t)zero_point);
      pOutput->pOutBuff[det_count].width       = scale * (float32_t)((int32_t)ptrScratch[i].width       - (int32_t)zero_point);
      pOutput->pOutBuff[det_count].height      = scale * (float32_t)((int32_t)ptrScratch[i].height      - (int32_t)zero_point);
      pOutput->pOutBuff[det_count].conf        = scale * scale * (float32_t)((int32_t)ptrScratch[i].conf);
      pOutput->pOutBuff[det_count].class_index = (int32_t)ptrScratch[i].class_index;
      det_count++;
    }
  }
  pOutput->nb_detect = det_count;

  return (AI_OD_POSTPROCESS_ERROR_NO);
}

  static int32_t _yolov_d_pp_getNNBoxes_centroid_stride(float32_t **pRaw_detections_ptr,
                                                       od_pp_out_t *pOutput,
                                                       uint32_t width,
                                                       uint32_t  height,
                                                       uint8_t stride,
                                                       uint32_t nb_classes,
                                                       float32_t conf_threshold,
                                                       uint32_t *p_nb_detect,
                                                       int32_t det_cap)
{
  int32_t error   = AI_OD_POSTPROCESS_ERROR_NO;

  float32_t *pRaw_detections = *pRaw_detections_ptr;
  uint32_t grid_width = width / stride;
  uint32_t  grid_height = height /stride;
  float32_t inv_width = 1.0f / (float32_t)width;
  float32_t inv_height = 1.0f / (float32_t)height;
  float32_t best_score_f;
  int32_t nb_detect = *p_nb_detect;
  od_pp_outBuffer_t *pOutBuff = (od_pp_outBuffer_t *)pOutput->pOutBuff;


  for (uint32_t iy = 0; iy < grid_height; iy++)
  {
    uint32_t remaining_boxes = grid_width;
    for (uint32_t ix = 0; ix < grid_width; ix+=4)
    {

      float32_t best_score_array[4];
      uint32_t class_index_array[4];

      uint32_t parallelize = MIN(remaining_boxes,4);
      // Max prob over classes
      vision_models_maxi_p_if32ou32(&pRaw_detections[AI_YOLO_D_PP_CLASSPROB],
                                    nb_classes,
                                    AI_YOLO_D_PP_CLASSPROB + nb_classes,
                                    best_score_array,
                                    class_index_array,
                                    parallelize);

      for (int _i = 0; _i < parallelize; _i++)
      {
        best_score_f = best_score_array[_i]*pRaw_detections[AI_YOLO_D_PP_OBJPROB];

        if ((best_score_f >= conf_threshold) && (nb_detect < det_cap))
        {
          /* AI_YOLO_D_PP_XCENTER */
          float32_t x_center = pRaw_detections[AI_YOLO_D_PP_XCENTER];
          x_center = (x_center + ix + _i) * stride * inv_width;
          pOutBuff[nb_detect].x_center    = x_center;
          /* AI_YOLO_D_PP_YCENTER */
          float32_t y_center = pRaw_detections[AI_YOLO_D_PP_YCENTER];
          y_center = (y_center + iy) * stride * inv_height;
          pOutBuff[nb_detect].y_center    = y_center;
          /* AI_YOLO_D_PP_WIDTHREL */
          float32_t w = pRaw_detections[AI_YOLO_D_PP_WIDTHREL];
          w = expf(w) * stride * inv_width;
          pOutBuff[nb_detect].width    = w;
          /* AI_YOLO_D_PP_HEIGHTREL */
          float32_t h = pRaw_detections[AI_YOLO_D_PP_HEIGHTREL];
          h = expf(h) * stride * inv_height;
          pOutBuff[nb_detect].height      = h;

          pOutBuff[nb_detect].conf        = best_score_f;
          pOutBuff[nb_detect].class_index = class_index_array[_i];
          nb_detect++;
        }
        pRaw_detections += (AI_YOLO_D_PP_CLASSPROB + nb_classes);
      }
      remaining_boxes -= parallelize;
    } // for ix
  } // for iy
  *p_nb_detect = nb_detect;
  *pRaw_detections_ptr = pRaw_detections;

  return (error);
}

int32_t yolov_d_pp_getNNBoxes_centroid(od_yolo_d_pp_in_centroid_t *pInput,
                                       od_pp_out_t *pOutput,
                                       od_yolo_d_pp_static_param_t *pInput_static_param)
{
  int32_t error   = AI_OD_POSTPROCESS_ERROR_NO;
  float32_t *pRaw_detections = (float32_t *)pInput->pRaw_detections;
  int32_t det_cap = pInput_static_param->max_boxes_limit; /* ne301: pOutBuff is sized to max_detections */

  // To be done 3 time for each stride
  for ( uint8_t stride_idx = 0; stride_idx < pInput_static_param->strides_nb; stride_idx++) {
    uint8_t stride = pInput_static_param->strides[stride_idx];

    error = _yolov_d_pp_getNNBoxes_centroid_stride(&pRaw_detections,
                                   pOutput,
                                   pInput_static_param->width,
                                   pInput_static_param->height,
                                   stride,
                                   pInput_static_param->nb_classes,
                                   pInput_static_param->conf_threshold,
                                   &pInput_static_param->nb_detect,
                                   det_cap);
    if (error != AI_OD_POSTPROCESS_ERROR_NO) {
      break;
    }
  }
  return error;
}

static int32_t _yolov_d_pp_getNNBoxes_centroid_int8_stride(int8_t **pRaw_detections_ptr,
                                                          od_pp_out_t *pOutput,
                                                          uint32_t width,
                                                          uint32_t  height,
                                                          uint8_t stride,
                                                          uint32_t nb_classes,
                                                          int16_t conf_threshold_s16,
                                                          uint32_t *p_nb_detect,
                                                          int8_t zero_point,
                                                          float32_t scale,
                                                          int32_t det_cap)

{
  int32_t error   = AI_OD_POSTPROCESS_ERROR_NO;

  int8_t *pRaw_detections = *pRaw_detections_ptr;
  uint32_t grid_width = width / stride;
  uint32_t  grid_height = height /stride;
  float32_t inv_width = 1.0f / (float32_t)width;
  float32_t inv_height = 1.0f / (float32_t)height;
  int32_t nb_detect = *p_nb_detect;
  od_pp_outBuffer_t *pOutBuff = (od_pp_outBuffer_t *)pOutput->pOutBuff;


  for (uint32_t iy = 0; iy < grid_height; iy++)
  {
    uint32_t remaining_boxes = grid_width;
    for (uint32_t ix = 0; ix < grid_width; ix+=16)
    {

    int8_t best_score_array[16];
    uint8_t class_index_array[16];

      uint32_t parallelize = MIN(remaining_boxes,16);
      // Max prob over classes
      vision_models_maxi_p_is8ou8(&pRaw_detections[AI_YOLO_D_PP_CLASSPROB],
                                    nb_classes,
                                    AI_YOLO_D_PP_CLASSPROB + nb_classes,
                                    best_score_array,
                                    class_index_array,
                                    parallelize);

      for (int _i = 0; _i < parallelize; _i++)
      {
        int16_t best_score_s16 = ((int16_t)best_score_array[_i]-zero_point)*((int16_t)(pRaw_detections[AI_YOLO_D_PP_OBJPROB])-zero_point);

        if ( (best_score_s16 >= conf_threshold_s16) && (nb_detect < det_cap))
        {
          float32_t best_score_f = best_score_s16 * scale;
          best_score_f *= scale;

          /* AI_YOLO_D_PP_XCENTER */
          float32_t x_center = scale * (float32_t)((int32_t)pRaw_detections[AI_YOLO_D_PP_XCENTER] - (int32_t)zero_point);
          x_center = (x_center + ix + _i) * stride * inv_width;
          pOutBuff[nb_detect].x_center    = x_center;
          /* AI_YOLO_D_PP_YCENTER */
          float32_t y_center = scale * (float32_t)((int32_t)pRaw_detections[AI_YOLO_D_PP_YCENTER] - (int32_t)zero_point);
          y_center = (y_center + iy) * stride * inv_height;
          pOutBuff[nb_detect].y_center    = y_center;
          /* AI_YOLO_D_PP_WIDTHREL */
          float32_t w = scale * (float32_t)((int32_t)pRaw_detections[AI_YOLO_D_PP_WIDTHREL] - (int32_t)zero_point);
          w = expf(w) * stride * inv_width;
          pOutBuff[nb_detect].width    = w;
          /* AI_YOLO_D_PP_HEIGHTREL */
          float32_t h = scale * (float32_t)((int32_t)pRaw_detections[AI_YOLO_D_PP_HEIGHTREL] - (int32_t)zero_point);
          h = expf(h) * stride * inv_height;
          pOutBuff[nb_detect].height      = h;

          pOutBuff[nb_detect].conf        = best_score_f;
          pOutBuff[nb_detect].class_index = class_index_array[_i];
          nb_detect++;
        }
        pRaw_detections += (AI_YOLO_D_PP_CLASSPROB + nb_classes);
      }
      remaining_boxes -= parallelize;
    } // for ix
  } // for iy
  *p_nb_detect = nb_detect;
  *pRaw_detections_ptr = pRaw_detections;

  return (error);
}

int32_t yolov_d_pp_getNNBoxes_centroid_int8(od_yolo_d_pp_in_centroid_t *pInput,
                                           od_pp_out_t *pOutput,
                                           od_yolo_d_pp_static_param_t *pInput_static_param)
{
  int32_t error   = AI_OD_POSTPROCESS_ERROR_NO;
  int8_t *pRaw_detections = (int8_t *)pInput->pRaw_detections;
  int16_t conf_threshold_s16 = (int16_t)(pInput_static_param->conf_threshold / (pInput_static_param->raw_output_scale * pInput_static_param->raw_output_scale) + 0.5f);
  int32_t det_cap = pInput_static_param->max_boxes_limit; /* ne301: pOutBuff is sized to max_detections */

  // To be done 3 time for each stride
  for ( uint8_t stride_idx = 0; stride_idx < pInput_static_param->strides_nb; stride_idx++) {
    uint8_t stride = pInput_static_param->strides[stride_idx];

    error = _yolov_d_pp_getNNBoxes_centroid_int8_stride(&pRaw_detections,
                                                        pOutput,
                                                        pInput_static_param->width,
                                                        pInput_static_param->height,
                                                        stride,
                                                        pInput_static_param->nb_classes,
                                                        conf_threshold_s16,
                                                        &pInput_static_param->nb_detect,
                                                        pInput_static_param->raw_output_zero_point,
                                                        pInput_static_param->raw_output_scale,
                                                        det_cap);
    if (error != AI_OD_POSTPROCESS_ERROR_NO) {
      break;
    }
  }
  return error;

}


/* ----------------------       Exported routines      ---------------------- */

int32_t od_yolo_d_pp_reset(od_yolo_d_pp_static_param_t *pInput_static_param)
{
  /* Initializations */
  pInput_static_param->nb_detect = 0;

  return (AI_OD_POSTPROCESS_ERROR_NO);
}


int32_t od_yolo_d_pp_process(od_yolo_d_pp_in_centroid_t *pInput,
                             od_pp_out_t *pOutput,
                             od_yolo_d_pp_static_param_t *pInput_static_param)
{
  int32_t error   = AI_OD_POSTPROCESS_ERROR_NO;

  /* Call Get NN boxes first */
  error = yolov_d_pp_getNNBoxes_centroid(pInput,
                                        pOutput,
                                        pInput_static_param);
  if (error != AI_OD_POSTPROCESS_ERROR_NO) return (error);

  /* Then NMS */
  error = yolov_d_pp_nmsFiltering_centroid(pOutput,
                                          pInput_static_param);
  if (error != AI_OD_POSTPROCESS_ERROR_NO) return (error);

  /* And score re-filtering */
  error = yolov_d_pp_scoreFiltering_centroid(pOutput,
                                            pInput_static_param);

  return (error);
}


int32_t od_yolo_d_pp_process_int8(od_yolo_d_pp_in_centroid_t *pInput,
                                  od_pp_out_t *pOutput,
                                  od_yolo_d_pp_static_param_t *pInput_static_param)
{
  int32_t error   = AI_OD_POSTPROCESS_ERROR_NO;
  /* Call Get NN boxes first */
  error = yolov_d_pp_getNNBoxes_centroid_int8(pInput,
                                             pOutput,
                                             pInput_static_param);
  if (error != AI_OD_POSTPROCESS_ERROR_NO) return (error);

  /* Then NMS */

  error = yolov_d_pp_nmsFiltering_centroid(pOutput,
                                          pInput_static_param);
  if (error != AI_OD_POSTPROCESS_ERROR_NO) return (error);

  /* And score re-filtering */
  error = yolov_d_pp_scoreFiltering_centroid(pOutput,
                                            pInput_static_param);
  return (error);
}

