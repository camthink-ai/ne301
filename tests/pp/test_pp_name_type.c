#include <stdio.h>
#include <string.h>
#include "pp.h"

static int g_fail;

static void expect(const char *name, pp_type_t want)
{
    pp_type_t got = pp_name_type(name);
    if (got != want) {
        printf("FAIL %s -> %d (want %d)\n", name ? name : "NULL", (int)got, (int)want);
        g_fail++;
    }
}

const pp_entry_t pp_entry_od_yolo_v2_uf = {"pp_od_yolo_v2_uf", NULL};
const pp_entry_t pp_entry_od_yolo_v2_ui = {"pp_od_yolo_v2_ui", NULL};
const pp_entry_t pp_entry_od_yolo_v5_uu = {"pp_od_yolo_v5_uu", NULL};
const pp_entry_t pp_entry_od_st_yolox_uf = {"pp_od_st_yolox_uf", NULL};
const pp_entry_t pp_entry_od_st_yolox_ui = {"pp_od_st_yolox_ui", NULL};
const pp_entry_t pp_entry_od_st_ssd_uf = {"pp_od_st_ssd_uf", NULL};
const pp_entry_t pp_entry_od_st_ssd_ui = {"pp_od_st_ssd_ui", NULL};
const pp_entry_t pp_entry_od_blazeface_uf = {"pp_od_blazeface_uf", NULL};
const pp_entry_t pp_entry_od_blazeface_ui = {"pp_od_blazeface_ui", NULL};
const pp_entry_t pp_entry_od_blazeface_uu = {"pp_od_blazeface_uu", NULL};
const pp_entry_t pp_entry_fd_blazeface_ui = {"pp_fd_blazeface_ui", NULL};
const pp_entry_t pp_entry_od_yolo_d_ui = {"pp_od_yolo_d_ui", NULL};
const pp_entry_t pp_entry_fd_yunet_ui = {"pp_fd_yunet_ui", NULL};
const pp_entry_t pp_entry_mpe_yolo_v8_uf = {"pp_mpe_yolo_v8_uf", NULL};
const pp_entry_t pp_entry_mpe_pd_uf = {"pp_mpe_pd_uf", NULL};
const pp_entry_t pp_entry_spe_movenet_uf = {"pp_spe_movenet_uf", NULL};
const pp_entry_t pp_entry_spe_movenet_ui = {"pp_spe_movenet_ui", NULL};
const pp_entry_t pp_entry_iseg_yolo_v8_ui = {"pp_iseg_yolo_v8_ui", NULL};
const pp_entry_t pp_entry_sseg_deeplab_v3_uf = {"pp_sseg_deeplab_v3_uf", NULL};
const pp_entry_t pp_entry_sseg_deeplab_v3_ui = {"pp_sseg_deeplab_v3_ui", NULL};
const pp_entry_t pp_entry_mpe_yolo_v8_ui = {"pp_mpe_yolo_v8_ui", NULL};
const pp_entry_t pp_entry_od_yolo_v8_uf = {"pp_od_yolo_v8_uf", NULL};
const pp_entry_t pp_entry_od_yolo_v8_ui = {"pp_od_yolo_v8_ui", NULL};
const pp_entry_t pp_entry_od_yolo_v11_uf = {"pp_od_yolo_v11_uf", NULL};
const pp_entry_t pp_entry_od_yolo_v11_ui = {"pp_od_yolo_v11_ui", NULL};

int main(void)
{
    expect("pp_od_yolo_v11_ui", PP_TYPE_OD);
    expect("pp_od_yolo_v8_ui", PP_TYPE_OD);
    expect("pp_od_yolo_v2_ui", PP_TYPE_OD);
    expect("pp_od_blazeface_uu", PP_TYPE_OD);
    expect("pp_mpe_yolo_v8_ui", PP_TYPE_MPE);
    expect("pp_spe_movenet_ui", PP_TYPE_SPE);
    expect("pp_iseg_yolo_v8_ui", PP_TYPE_ISEG);
    expect("pp_sseg_deeplab_v3_ui", PP_TYPE_SSEG);
    expect("pp_fd_yunet_ui", PP_TYPE_NONE);
    expect("od_yolo_v8_ui", PP_TYPE_NONE);
    expect("pp_od_fake_unregistered", PP_TYPE_NONE);
    expect("pp_od_yolo_v11_ui_modified", PP_TYPE_NONE);
    expect("garbage", PP_TYPE_NONE);
    expect(NULL, PP_TYPE_NONE);
    printf("test_pp_name_type: %s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
