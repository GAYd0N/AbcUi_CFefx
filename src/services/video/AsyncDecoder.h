#pragma once

// New implementation: bounded asynchronous adapter over the existing Decoder.
// Only the worker touches FFmpeg. Callers own presentation and GPU uploads;
// pause cancels queued work, close joins before destroying the decoder.
#include "Decoder.h"

#include <memory>
#include <string>
#include <vector>

namespace video {

class AsyncDecoder {
public:
  AsyncDecoder();
  ~AsyncDecoder();
  AsyncDecoder(const AsyncDecoder &) = delete;
  AsyncDecoder &operator=(const AsyncDecoder &) = delete;

  bool open(const char *Path, int Width, int Height);
  bool advance(double Delta);
  void pause();
  void close();
  bool isOpen() const;
  void setLooping(bool Value);
  const FrameView &frame() const {
    return View;
  }
  int sourceWidth() const {
    return SourceWidth;
  }
  int sourceHeight() const {
    return SourceHeight;
  }
  double lastSeconds() const {
    return Seconds;
  }
  const std::string &error() const {
    return Error;
  }

private:
  struct State;
  std::unique_ptr<State> Data;
  std::vector<unsigned char> Pixels;
  FrameView View;
  int SourceWidth = 0;
  int SourceHeight = 0;
  double Seconds = -1;
  std::string Error;
};

} // namespace video
