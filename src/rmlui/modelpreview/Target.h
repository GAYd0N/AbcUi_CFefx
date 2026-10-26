#pragma once

// One capture target and fake entity per element. The pass runs through
// the in-house engine V_RenderView route (framework/standaloneview, issue
// #24 comment); no GL or engine model ownership leaks through this seam.
#include "Options.h"
#include <memory>
#include <string>

namespace modelpreview {
class Target {
public:
  Target();
  ~Target();
  Target(const Target &) = delete;
  Target &operator=(const Target &) = delete;
  bool render(const Options &Config, int Width, int Height);
  void invalidate();
  const std::string &uri() const;
  unsigned int texture() const;
  unsigned int frames() const;

private:
  struct State;
  std::unique_ptr<State> Data;
};

bool hasWorld();
void invalidateAll();
} // namespace modelpreview
