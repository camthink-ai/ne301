#include <string.h>
#include "pp.h"

// Implemented entries
extern const pp_entry_t pp_entry_od_yolo_v2_uf;
extern const pp_entry_t pp_entry_od_yolo_v2_ui;
extern const pp_entry_t pp_entry_od_yolo_v5_uu;
extern const pp_entry_t pp_entry_od_st_yolox_uf;
extern const pp_entry_t pp_entry_od_st_yolox_ui;
extern const pp_entry_t pp_entry_od_st_ssd_uf;
extern const pp_entry_t pp_entry_od_st_ssd_ui;
extern const pp_entry_t pp_entry_od_blazeface_uf;
extern const pp_entry_t pp_entry_od_blazeface_ui;
extern const pp_entry_t pp_entry_od_blazeface_uu;
extern const pp_entry_t pp_entry_fd_blazeface_ui;
extern const pp_entry_t pp_entry_od_yolo_d_ui;
extern const pp_entry_t pp_entry_fd_yunet_ui;
extern const pp_entry_t pp_entry_mpe_yolo_v8_uf;
extern const pp_entry_t pp_entry_mpe_pd_uf;
extern const pp_entry_t pp_entry_spe_movenet_uf;
extern const pp_entry_t pp_entry_spe_movenet_ui;
extern const pp_entry_t pp_entry_iseg_yolo_v8_ui;
extern const pp_entry_t pp_entry_sseg_deeplab_v3_uf;
extern const pp_entry_t pp_entry_sseg_deeplab_v3_ui;
extern const pp_entry_t pp_entry_mpe_yolo_v8_ui;
extern const pp_entry_t pp_entry_od_yolo_v8_uf;
extern const pp_entry_t pp_entry_od_yolo_v8_ui;
extern const pp_entry_t pp_entry_od_yolo_v11_uf;
extern const pp_entry_t pp_entry_od_yolo_v11_ui;

/* Return registry (pointer array) and count */
static const pp_entry_t * const* get_registered_entries(size_t *out_count)
{
    /* Local static array: element type is "pointer to const pp_entry_t" */
    static const pp_entry_t * const entries[] = {
        &pp_entry_od_yolo_v2_uf,
        &pp_entry_od_yolo_v2_ui,
        &pp_entry_od_yolo_v5_uu,
        &pp_entry_od_st_yolox_uf,
        &pp_entry_od_st_yolox_ui,
        &pp_entry_od_st_ssd_uf,
        &pp_entry_od_st_ssd_ui,
        &pp_entry_od_blazeface_uf,
        &pp_entry_od_blazeface_ui,
        &pp_entry_od_blazeface_uu,
        &pp_entry_fd_blazeface_ui,
        &pp_entry_od_yolo_d_ui,
        &pp_entry_fd_yunet_ui,
        &pp_entry_mpe_yolo_v8_uf,
        &pp_entry_mpe_pd_uf,
        &pp_entry_spe_movenet_uf,
        &pp_entry_spe_movenet_ui,
        &pp_entry_iseg_yolo_v8_ui,
        &pp_entry_sseg_deeplab_v3_uf,
        &pp_entry_sseg_deeplab_v3_ui,
        &pp_entry_mpe_yolo_v8_ui,
        &pp_entry_od_yolo_v8_uf,
        &pp_entry_od_yolo_v8_ui,
        &pp_entry_od_yolo_v11_uf,
        &pp_entry_od_yolo_v11_ui,
    };
    if (out_count) {
        *out_count = sizeof(entries) / sizeof(entries[0]);
    }
    return entries;
}

const pp_vtable_t* pp_find(const char *name)
{
    if (!name) return NULL;

    size_t count = 0;
    const pp_entry_t * const* entries = get_registered_entries(&count);

    for (size_t i = 0; i < count; ++i) {
        if (strcmp(entries[i]->name, name) == 0) {
            return entries[i]->vt;
        }
    }
    return NULL;
}

pp_type_t pp_name_type(const char *name)
{
    size_t count = 0;
    const pp_entry_t * const *entries;
    size_t i;

    if (name == NULL) {
        return PP_TYPE_NONE;
    }
    entries = get_registered_entries(&count);
    for (i = 0; i < count; i++) {
        if (strcmp(entries[i]->name, name) == 0) {
            break;
        }
    }
    if (i == count) {
        return PP_TYPE_NONE;
    }
    if (strncmp(name, "pp_od_", 6) == 0) {
        return PP_TYPE_OD;
    }
    if (strncmp(name, "pp_mpe_", 7) == 0) {
        return PP_TYPE_MPE;
    }
    if (strncmp(name, "pp_spe_", 7) == 0) {
        return PP_TYPE_SPE;
    }
    if (strncmp(name, "pp_iseg_", 8) == 0) {
        return PP_TYPE_ISEG;
    }
    if (strncmp(name, "pp_sseg_", 8) == 0) {
        return PP_TYPE_SSEG;
    }
    return PP_TYPE_NONE;
}

// support model list
int32_t pp_model_support_list(char **list, uint32_t *nb_models)
{
    size_t pp_count = 0;
    const pp_entry_t * const* pp_entries = get_registered_entries(&pp_count);
    if (list) {
        for (size_t i = 0; i < pp_count; ++i) {
            list[i] = (char *)pp_entries[i]->name;  // Cast const char* to char* for compatibility
        }
    }
    if (nb_models) {
        *nb_models = pp_count;
    }
    return 0;
}

/* Other functions remain unchanged */
int32_t pp_init(void) { return 0; }
void pp_deinit(void) { }
