#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2common/kernel/modulemgr.h>
#include <psp2common/types.h>
#include <stdio.h>

int (*sceShellNoticeInit)(void *data);
int (*sceShellSetUtf8)(void *data, const char *text, SceSize len);
int (*sceShellNoticeClean)(void *data);
int (*sceLsdbSendNotification)(void *a1, int a2);

int module_get_offset(SceUID modid, SceSize segidx, uint32_t offset,
                      void *stub_out) {

  int res = 0;
  SceKernelModuleInfo info;

  if (segidx > 3) {
    return -1;
  }

  if (stub_out == NULL) {
    return -2;
  }

  res = sceKernelGetModuleInfo(modid, &info);
  if (res < 0) {
    return res;
  }

  if (offset > info.segments[segidx].memsz) {
    return -3;
  }

  *(uint32_t *)stub_out = (uint32_t)(info.segments[segidx].vaddr + offset);

  return 0;
}

// Derived from
// https://github.com/Princess-of-Sleeping/SceShell-Notice-PoC/blob/master/src/main.c
int notification_send(char *titleid, char *message, int msg_type,
                      int action_type, const char *name) {
  char buf[128];
  char data[0x100];
  sceShellNoticeInit(data);

  // caller titld id
  sceShellSetUtf8(&data[0x0], "NPXS19999", sceClibStrnlen("NPXS19999", 0x10));
  // item id
  sceClibSnprintf(buf, sizeof(buf), "SCBGDL_%s", name);
  sceShellSetUtf8(&data[0xC], buf, sceClibStrnlen(buf, 0x10));
  // AppInstallFailed = 0, // Happened when int wasnt set
  // LiveAreaRefreshed = 0x900,
  // AppInstalledSuccessfully = 0x52,
  // DownloadComplete = 0x51,
  // Custom = 0x100, One single line of text (desc, or title, desc takes
  // priority, used by SCE for testing?)
  // UserDefined = 0x102, type used by sceNotificationUtilSendNotification (same
  // as Custom??) msg type
  *(uint32_t *)(&data[0x20]) = msg_type;
  // AppBound        = 1,
  // AppOpen         = 2, // Opens LA
  // AppHighlight    = 3,
  // Unk             = 0xb // ?? found in RE (possibly some app launch)
  // action type
  *(uint32_t *)(&data[0x28]) = action_type;
  // new flags
  // data[0x2a] = 0x01;
  // << possibly popup_no << 0 = no popup & no highlight in notif centre 1 =
  // popup & no highlight, if 1 when @new_flag = 0 then popup
  // display type
  data[0x2C] = 0x01;
  // icon path
  SceIoStat stat;
  sceClibSnprintf(buf, sizeof(buf), "ux0:app/%s/sce_sys/icon0.png", titleid);
  if (sceIoGetstat(buf, &stat) < 0) {
    sceClibSnprintf(buf, sizeof(buf), "vs0:app/%s/sce_sys/icon0.png", titleid);
  }
  sceShellSetUtf8(&data[0x30], buf, sceClibStrnlen(buf, 0xFFFF));
  // message
  sceShellSetUtf8(&data[0xBC], message, sceClibStrnlen(message, 0xFFFF));
  // exec mode
  *(uint32_t *)(&data[0xC8]) = 0x20000;
  // exec title id
  sceShellSetUtf8(&data[0xCC], titleid, sceClibStrnlen(titleid, 0x10));
  // exec arguments
  sceShellSetUtf8(&data[0xD8], "scbgdl", sceClibStrnlen("scbgdl", 0x10));

  sceLsdbSendNotification(data, 1 /* allowReplace */);
  return sceShellNoticeClean(data);
}
