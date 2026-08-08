#pragma once

#include <cstdint>
#include <string>

namespace HalSystem {
struct StackFrame {
  uint32_t sp;
  uint32_t spp[8];
};

// Must be called as the very first statement in setup(), before anything else that could hang or
// panic. Stamps this build's identity (app ELF SHA-256) into RTC memory and records whether it
// matches the identity stamped by whatever last ran setup() -- see isRebootFromPanic().
void recordBootIdentity();

void begin();

// Dump panic info to SD card if necessary
void checkPanic();
void clearPanic();

std::string getPanicInfo(bool full = false);
bool isRebootFromPanic();
}  // namespace HalSystem
