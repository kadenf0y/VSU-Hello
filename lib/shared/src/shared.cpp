#include <shared.h>
SharedState G;


static QueueHandle_t sCmdQ = nullptr;

void shared_cmdq_init() {
  if (!sCmdQ) {
    sCmdQ = xQueueCreate(8, sizeof(Cmd)); // small, fast queue
  }
}

QueueHandle_t shared_cmdq() { return sCmdQ; }

bool shared_cmd_post(const Cmd& c) {
  if (!sCmdQ) return false;
  return xQueueSend(sCmdQ, &c, 0) == pdPASS;  // non-blocking
}
