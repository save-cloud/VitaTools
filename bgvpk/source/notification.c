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
                      int action_type) {
  char fname[128];
  sceClibSnprintf(fname, sizeof(fname), "ux0:app/%s/sce_sys/icon0.png", titleid);

  char data[0x100];
  sceShellNoticeInit(data);

  // caller titld id
  sceShellSetUtf8(&data[0x0], "NPXS19999", sceClibStrnlen("NPXS19999", 0x10));
  // item id
  sceShellSetUtf8(&data[0xC], titleid, sceClibStrnlen(titleid, 0x10));
  // AppInstallFailed = 0, // Happened when int wasnt set
  // LiveAreaRefreshed = 0x900,
  // AppInstalledSuccessfully = 0x52,
  // DownloadComplete = 0x51,
  // Custom = 0x100, // One single line of text (desc, or title, desc takes
  // priority, used by SCE for testing?) UserDefined = 0x102, // type used by
  // sceNotificationUtilSendNotification (same as Custom??)
  // msg type
  data[0x22] = msg_type;
  // action type
  // AppBound        = 1,
  // AppOpen         = 2, // Opens LA
  // AppHighlight    = 3,
  // Unk             = 0xb // ?? found in RE (possibly some app launch)
  *(uint32_t *)(&data[0x28]) = action_type;
  // new flags
  // data[0x2a] = 0x01;
  // display type
  // << possibly popup_no << 0 = no popup & no highlight in notif centre 1 =
  // popup & no highlight, if 1 when @new_flag = 0 then popup
  data[0x2C] = 0x01;
  // icon path
  sceShellSetUtf8(&data[0x30], fname, sceClibStrnlen(fname, 0xFFFF));
  // message
  sceShellSetUtf8(&data[0xBC], message, sceClibStrnlen(message, 0xFFFF));
  // exec mode
  *(uint32_t *)(&data[0xC8]) = 0x20000;
  // exec title id
  sceShellSetUtf8(&data[0xCC], titleid, sceClibStrnlen(titleid, 0x10));
  // exec arguments
  sceShellSetUtf8(&data[0xD8], "scbgdl", sceClibStrnlen("scbgdl", 0x10));

  sceLsdbSendNotification(data, 0);
  return sceShellNoticeClean(data);
}
