#include "crash.h"

#include <esp_core_dump.h>

#include <memory>

#include "log.h"

namespace crash {

static String text;

void begin() {
  if (esp_core_dump_image_check() != ESP_OK) return;  // nothing stored
  std::unique_ptr<esp_core_dump_summary_t> s(new esp_core_dump_summary_t());
  if (esp_core_dump_get_summary(s.get()) != ESP_OK) {
    text = "a core dump is stored but can't be read";
    LOGE("crash", "%s", text.c_str());
    return;
  }
  char reason[160] = "";
  esp_core_dump_get_panic_reason(reason, sizeof(reason));
  char elf[9] = "";
  memcpy(elf, s->app_elf_sha256, 8);  // first hex digits of the crashed firmware's ELF hash
  String bt;
  for (uint32_t i = 0; i < s->exc_bt_info.depth && i < 16; i++) {
    char address[12];
    snprintf(address, sizeof(address), " 0x%08lx", (unsigned long)s->exc_bt_info.bt[i]);
    bt += address;
  }
  char head[200];
  snprintf(head, sizeof(head), "task '%s', pc 0x%08lx, cause %lu, vaddr 0x%08lx, elf %s, reason: %s",
           s->exc_task, (unsigned long)s->exc_pc, (unsigned long)s->ex_info.exc_cause,
           (unsigned long)s->ex_info.exc_vaddr, elf, reason[0] ? reason : "?");
  text = String(head) + " | backtrace:" + bt + (s->exc_bt_info.corrupted ? " (corrupted)" : "");
  LOGE("crash", "LAST CRASH: %s", text.c_str());
}

bool present() { return text.length() > 0; }
String summary() { return text; }

void clear() {
  esp_core_dump_image_erase();
  text = "";
  LOGI("crash", "stored crash report erased");
}

}  // namespace crash
