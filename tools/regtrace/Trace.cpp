/*
 * Copyright (c) 2017 Trail of Bits, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "pin.H"

#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>

KNOB<uintptr_t> gEntrypoint(
    KNOB_MODE_WRITEONCE, "pintool", "entrypoint", "0",
    "Entrypoint of lifted program. Usually address of `main`.");

struct RegInfo final {
  const char *name;
  LEVEL_BASE::REG reg;
};

static uintptr_t gLowAddr = 0;
static uintptr_t gHighAddr = 0;

static uintptr_t gLowExcludeAddr = 0;
static uintptr_t gHighExcludeAddr = 0;
#ifdef __x86_64__

static const struct RegInfo gGprs[] = {
  {"RIP", LEVEL_BASE::REG_RIP},
  {"RAX", LEVEL_BASE::REG_RAX},
  {"RBX", LEVEL_BASE::REG_RBX},
  {"RCX", LEVEL_BASE::REG_RCX},
  {"RDX", LEVEL_BASE::REG_RDX},
  {"RSI", LEVEL_BASE::REG_RSI},
  {"RDI", LEVEL_BASE::REG_RDI},
  {"RBP", LEVEL_BASE::REG_RBP},
  {"RSP", LEVEL_BASE::REG_RSP},
  {"R8", LEVEL_BASE::REG_R8},
  {"R9", LEVEL_BASE::REG_R9},
  {"R10", LEVEL_BASE::REG_R10},
  {"R11", LEVEL_BASE::REG_R11},
  {"R12", LEVEL_BASE::REG_R12},
  {"R13", LEVEL_BASE::REG_R13},
  {"R14", LEVEL_BASE::REG_R14},
  {"R15", LEVEL_BASE::REG_R15},
};

#else

static const struct RegInfo gGprs[] = {
  {"EIP", LEVEL_BASE::REG_EIP},
  {"EAX", LEVEL_BASE::REG_EAX},
  {"EBX", LEVEL_BASE::REG_EBX},
  {"ECX", LEVEL_BASE::REG_ECX},
  {"EDX", LEVEL_BASE::REG_EDX},
  {"ESI", LEVEL_BASE::REG_ESI},
  {"EDI", LEVEL_BASE::REG_EDI},
  {"EBP", LEVEL_BASE::REG_EBP},
  {"ESP", LEVEL_BASE::REG_ESP},
};

#endif  // __x86_64__

VOID PrintRegState(CONTEXT *ctx) {
  static bool gPrinting = false;
  if (!gPrinting) {
    gPrinting = gEntrypoint.Value() == PIN_GetContextReg(ctx, gGprs[0].reg);
    if (!gPrinting) {
      return;
    }
  }

  std::stringstream ss;
  const char *sep = "";
  const auto width = sizeof(void *) * 2;
  for (auto &gpr : gGprs) {
    ss
        << sep << gpr.name << "=" << std::hex << std::setw(width)
        << PIN_GetContextReg(ctx, gpr.reg) << std::setw(0);
    sep = " ";
  }

  // `-add-reg-tracer` uses `printf`, so even though it's a bit weird, we'll
  // do it here too and hopefully achieve some similar buffering.
  fprintf(stderr, "%s\n", ss.str().c_str());
}

VOID InstrumentInstruction(INS ins, VOID *) {
  auto addr = INS_Address(ins);
  if (addr >= gLowAddr && addr < gHighAddr &&
      !(addr >= gLowExcludeAddr && addr < gHighExcludeAddr)) {
  INS_InsertCall(
      ins, IPOINT_BEFORE, (AFUNPTR)PrintRegState, IARG_CONTEXT, IARG_END);
  }
}

VOID FindEntrypoint(IMG img, void *) {
  auto low = IMG_LowAddress(img);
  auto high = IMG_HighAddress(img);
  if (low <= gEntrypoint.Value() && gEntrypoint.Value() <= high) {
    gLowAddr = low;
    gHighAddr = high;
  
    // Find 
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
      if (SEC_Name(sec) == ".plt" || SEC_Name(sec) == ".PLT") {
        gLowExcludeAddr = SEC_Address(sec);
        gHighExcludeAddr = gLowExcludeAddr + SEC_Size(sec);
      }
    }
  }
}

int main(int argc, char *argv[]) {
  PIN_InitSymbols();
  PIN_Init(argc, argv);

  for (IMG img = APP_ImgHead(); IMG_Valid(img); img = IMG_Next(img)) {
    FindEntrypoint(img, nullptr);
  }

  IMG_AddInstrumentFunction(FindEntrypoint, nullptr);
  INS_AddInstrumentFunction(InstrumentInstruction, nullptr);
  PIN_StartProgram();
  return 0;
}
