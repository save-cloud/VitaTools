#include <stdint.h>

#define BGVPK_MAGIC 'BG'
#define BGVPK_CFG_VER 1

typedef struct scbgdl_export_param_struct {
    uint16_t magic; // bgvpk magic | cfg version
    uint16_t target; // 0 - ux0:download, 1- app
    char title_id[12]; // display title icon id
} scbgdl_export_param_struct;
