#pragma once

// New implementation. One FBO and fake entity per element. No GL or engine
// model ownership leaks through this seam; all drawing belongs to Renderer.
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
