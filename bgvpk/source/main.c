// Based on download_enabler by theflow0

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <taihen.h>

#include "Archives.h"
#include "promote.c"
#include "bgvpk.h"
#include "offsets.c"
#include "notification.c"

static SceUID hooks[7];
static tai_hook_ref_t ExportFileRef;
static tai_hook_ref_t GetFileTypeRef;
static tai_hook_ref_t ToastRef;
static const unsigned char nop32[4] = { 0xaf, 0xf3, 0x00, 0x80 };
static const int BGDL_DETECT_CODE = 0x80101A09;
static unsigned char is_download_enabler = 0;
static unsigned char is_notification_enable = 1;

int ToastPatch(void *off, unsigned int arg)
{
    if(!arg && off != NULL)
    {
        char download_path[1024];
        sceClibSnprintf(download_path, sizeof(download_path), "ux0:bgdl/t/%08x/scbgdl.installed", *(uint32_t *)off);
        SceIoStat stat;
        if(sceIoGetstat(download_path, &stat) >= 0) {
            return 0; // Notification should already be handled in export function
        }
    }

    return TAI_CONTINUE(int, ToastRef, off, arg);
}

static int ExportFilePatched(uint32_t* data) {

    int res = TAI_CONTINUE(int, ExportFileRef, data);

    if (res == BGDL_DETECT_CODE) {
        char download_path[1024];
        char bgdl_path[1024];
        char file_name[128];
        char dl_title[64];
        char flags[0xB];
        uint32_t num = *(uint32_t*)data[0];
        uint16_t is_install_app = 0;

        /* scbgdl param */
        sceClibSnprintf(download_path, sizeof(download_path), "ux0:bgdl/t/%08x/scbgdl_param.ini", num);
        scbgdl_export_param_struct export_params;
        export_params.magic = 0;
        SceUID fd = sceIoOpen(download_path, SCE_O_RDONLY, 0);
        if (fd < 0) {
          if (!is_download_enabler) {
            return BGDL_DETECT_CODE; // return to other plugin
          }
        }
        if (fd >= 0) {
            sceIoRead(fd, &export_params, 16); // read export_params
            sceIoClose(fd);
            if (export_params.magic == (BGVPK_MAGIC | BGVPK_CFG_VER)) {
                is_install_app = export_params.target;
            } else {
              return fd;
            }
        }
        /*/ scbgdl param */

        /* read d0.pdb */
        sceClibSnprintf(bgdl_path, sizeof(bgdl_path), "ux0:bgdl/t/%08x/d0.pdb", num);
        // Get bgdl title and file name
        uint16_t cur_off = 0xD3;
        fd = sceIoOpen(bgdl_path, SCE_O_RDONLY, 0);
        if (fd < 0) {
          return fd;
        }
        sceIoPread(fd, flags, 0xB, 0xD3); // read title flags
        sceIoPread(fd, dl_title, *(uint16_t*)(flags + 3), 0xDE); // read the title itself
        cur_off -= -(*(uint16_t*)(flags + 3) + 0xC);
        sceIoPread(fd, flags, 0xB, cur_off); // read flags for the next entry
        if (*(uint16_t*)(flags + 3) == 1) { // skip this entry, title is probably the URL and the next entry is the file
            cur_off -= -(*(uint16_t*)(flags + 3) + 0xC);
            sceIoPread(fd, flags, 0xB, cur_off);
            sceIoPread(fd, file_name, *(uint16_t*)(flags + 3), cur_off + 0xB);
        } else { // this entry is the target file from custom bgdl
            sceIoPread(fd, file_name, *(uint16_t*)(flags + 3), cur_off + 0xB);
        }
        sceIoClose(fd);
        /*/ read d0.pdb */

        // stop notification
        sceClibSnprintf(download_path, sizeof(download_path), "ux0:bgdl/t/%08x/scbgdl_param.ini", num);
        sceClibSnprintf(bgdl_path, sizeof(bgdl_path), "ux0:bgdl/t/%08x/scbgdl.installed", num);
        sceIoRename(download_path, bgdl_path);

        // the path of download file
        sceClibSnprintf(bgdl_path, sizeof(bgdl_path), "ux0:bgdl/t/%08x/%s", num, file_name);

        // Install the app
        if (is_install_app) {
            // Unzip the VPK to temp bgdl folder/X/ | Unzip the zip to ux0:data/
            sceClibSnprintf(download_path, sizeof(download_path), "ux0:bgdl/t/%08x/X", num);
            Zip* handle = ZipOpen(bgdl_path);
            res = ZipExtract(handle, NULL, download_path);
            ZipClose(handle);
            if (res == 0) {
              // Install/promote the app
              res = promoteApp(download_path, export_params.title_id);
              if (res) {
                sceClibSnprintf(download_path, sizeof(download_path), "%s\n%s", dl_title, "安装成功");
                notification_send(export_params.title_id, download_path, 0x52, 0x3);
              }
            } else {
              sceClibSnprintf(download_path, sizeof(download_path), "%s\n%s", dl_title, "安装失败");
              notification_send(export_params.title_id, download_path, 0x51, 0x2);
            }
        } else {
          // Save the downloaded file to ux0:download/*
          sceClibSnprintf(download_path, sizeof(download_path), "ux0:download/%s", file_name);
          res = sceIoMkdir("ux0:download", 0006);
          if (res >= 0 || res == 0x80010011) {
              sceIoRemove(download_path);
              res = sceIoRename(bgdl_path, download_path);
              sceClibSnprintf(download_path, sizeof(download_path), "%s\n%s", dl_title, "下载成功");
          } else {
              sceClibSnprintf(download_path, sizeof(download_path), "%s\n%s", dl_title, "下载失败");
          }
          notification_send(export_params.title_id, download_path, 0x51, 0x2);
        }
    }

    return res;
}

static int GetFileTypePatched(int unk, int* type, char** filename, char** mime_type) {
    int res = TAI_CONTINUE(int, GetFileTypeRef, unk, type, filename, mime_type);

    if (res == 0x80103A21) {
        *type = 1; // Type photo
        return 0;
    }

    return res;
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize args, void* argp) {
    tai_module_info_t den_info;
    den_info.size = sizeof(den_info);
    if (taiGetModuleInfo("DownloadEnabler", &den_info) >= 0) {
        is_download_enabler = 1;
        sceKernelStopUnloadModule(den_info.modid, 0, NULL, 0, NULL, NULL);
    }

    tai_module_info_t info;
    info.size = sizeof(info);
    uint32_t get_off, exp_off, rec_off, lock_off, notif_off;
    if (taiGetModuleInfo("SceShell", &info) >= 0 && get_shell_offsets(info.module_nid, &get_off, &exp_off, &rec_off, &lock_off) >= 0) {
        hooks[0] = taiInjectData(info.modid, 0, get_off, "GET", 4);
        if (hooks[0] >= 0) { // we use the fact that there can be only one tai inject per offset to make sure we dont hook twice
            hooks[1] = taiHookFunctionOffset(&ExportFileRef, info.modid, 0, exp_off, 1, ExportFilePatched);
            hooks[2] = taiHookFunctionOffset(&GetFileTypeRef, info.modid, 0, rec_off, 1, GetFileTypePatched);
            hooks[3] = taiInjectData(info.modid, 0, lock_off, nop32, 4);
            hooks[4] = taiInjectData(info.modid, 0, lock_off + 8, nop32, 4);
            hooks[5] = taiInjectData(info.modid, 0, lock_off + 16, nop32, 4);
            hooks[6] = taiHookFunctionOffset(&ToastRef, info.modid, 0, notif_off, 1, ToastPatch);

            // notification
            switch(info.module_nid){
              case 0x0552F692: // 3.60 Retail
                module_get_offset(info.modid, 0, 0x42930C | 1, &sceShellNoticeInit);
                module_get_offset(info.modid, 0, 0x408E14 | 1, &sceShellSetUtf8);
                module_get_offset(info.modid, 0, 0x4163E8 | 1, &sceShellNoticeClean);
                break;
              case 0x6CB01295: // 3.60 Devkit
                module_get_offset(info.modid, 0, 0x41AA30 | 1, &sceShellNoticeInit);
                module_get_offset(info.modid, 0, 0x3FAD88 | 1, &sceShellSetUtf8);
                module_get_offset(info.modid, 0, 0x408298 | 1, &sceShellNoticeClean);
                break;
              case 0x5549BF1F: // 3.65 Retail
                module_get_offset(info.modid, 0, 0x429754 | 1, &sceShellNoticeInit);
                module_get_offset(info.modid, 0, 0x40925C | 1, &sceShellSetUtf8);
                module_get_offset(info.modid, 0, 0x416830 | 1, &sceShellNoticeClean);
                break;
              default:
                is_notification_enable = 0;
            }
            taiGetModuleExportFunc("SceLsdb", 0xFFFFFFFF, 0x315B9FD6, (uintptr_t *)&sceLsdbSendNotification);
        }
    } else
        return SCE_KERNEL_START_FAILED;

    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void* argp) {
    if (hooks[6] >= 0)
        taiHookRelease(hooks[6], ToastRef);
    if (hooks[5] >= 0)
        taiInjectRelease(hooks[5]);
    if (hooks[4] >= 0)
        taiInjectRelease(hooks[4]);
    if (hooks[3] >= 0)
        taiInjectRelease(hooks[3]);
    if (hooks[2] >= 0)
        taiHookRelease(hooks[2], GetFileTypeRef);
    if (hooks[1] >= 0)
        taiHookRelease(hooks[1], ExportFileRef);
    if (hooks[0] >= 0)
        taiInjectRelease(hooks[0]);

    return SCE_KERNEL_STOP_SUCCESS;
}
