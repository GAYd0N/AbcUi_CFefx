// New implementation; see AsyncDecoder.h for the thread ownership contract.
#include "AsyncDecoder.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace video {

struct AsyncDecoder::State {
  struct Request {
    std::string Path;
    int Width = 0;
    int Height = 0;
    double Delta = 0;
    bool Loop = true;
    unsigned long long Generation = 0;
  };
  struct Result {
    std::vector<unsigned char> Pixels;
    int Width = 0;
    int Height = 0;
    int SourceWidth = 0;
    int SourceHeight = 0;
    double Seconds = -1;
    std::string Error;
  };
  std::mutex Mutex;
  std::condition_variable Wake;
  std::thread Worker;
  Request Next;
  std::unique_ptr<Result> Ready;
  bool Pending = false;
  bool Stopping = false;
  bool Paused = false;

  void run() {
    Decoder Media;
    Request Applied;
    bool Opened = false;
    for (;;) {
      Request Work;
      {
        std::unique_lock Lock(Mutex);
        Wake.wait(Lock, [&] { return Pending || Stopping; });
        if (Stopping)
          return;
        Work = Next;
        Next.Delta = 0;
        Pending = false;
      }
      const bool Changed = Work.Path != Applied.Path ||
                           Work.Width != Applied.Width ||
                           Work.Height != Applied.Height;
      if (Changed) {
        Opened = Media.open(Work.Path.c_str(), Work.Width, Work.Height);
        Applied = Work;
      }
      Media.setLooping(Work.Loop);
      const bool Resumed = Work.Generation != Applied.Generation;
      const bool Advanced = Opened && (Media.advance(Work.Delta) ||
                                       (Resumed && Media.frame().Pixels));
      Applied.Generation = Work.Generation;
      if (!Advanced && Media.error().empty())
        continue;
      auto Output = std::make_unique<Result>();
      Output->Error = Media.error();
      if (Advanced) {
        const auto &Frame = Media.frame();
        Output->Width = Frame.Width;
        Output->Height = Frame.Height;
        Output->SourceWidth = Media.sourceWidth();
        Output->SourceHeight = Media.sourceHeight();
        Output->Seconds = Media.lastSeconds();
        if (Frame.Pixels)
          Output->Pixels.assign(
              Frame.Pixels, Frame.Pixels + static_cast<size_t>(Frame.Width) *
                                               Frame.Height * 4);
      }
      std::lock_guard Lock(Mutex);
      // Source changes and pause invalidate in-flight results. At most
      // one ready frame exists; a slow UI never builds a decode backlog.
      if (!Stopping && !Paused && Work.Generation == Next.Generation)
        Ready = std::move(Output);
    }
  }
};

AsyncDecoder::AsyncDecoder() : Data(std::make_unique<State>()) {}
AsyncDecoder::~AsyncDecoder() {
  close();
}

bool AsyncDecoder::open(const char *Path, int Width, int Height) {
  if (!Path || !*Path)
    return false;
  std::lock_guard Lock(Data->Mutex);
  if (Data->Next.Path == Path && Data->Next.Width == Width &&
      Data->Next.Height == Height)
    return true;
  ++Data->Next.Generation;
  Data->Next.Path = Path;
  Data->Next.Width = Width;
  Data->Next.Height = Height;
  Data->Next.Delta = 0;
  Data->Ready.reset();
  Data->Paused = false;
  Data->Pending = true;
  Data->Stopping = false;
  Error.clear();
  if (!Data->Worker.joinable())
    Data->Worker = std::thread([this] { Data->run(); });
  Data->Wake.notify_one();
  return true;
}

bool AsyncDecoder::advance(double Delta) {
  std::unique_ptr<State::Result> Result;
  {
    std::lock_guard Lock(Data->Mutex);
    if (Data->Next.Path.empty())
      return false;
    Data->Paused = false;
    Result = std::move(Data->Ready);
    // Bound catch-up after a debugger stall or resize. The clock is never
    // advanced while hidden; resume cannot schedule seconds of stale work.
    Data->Next.Delta = std::clamp(Data->Next.Delta + Delta, 0.0, 0.25);
    Data->Pending = true;
    Data->Wake.notify_one();
  }
  if (!Result)
    return false;
  Error = std::move(Result->Error);
  if (Result->Pixels.empty())
    return false;
  Pixels = std::move(Result->Pixels);
  View = {Pixels.data(), Result->Width, Result->Height};
  SourceWidth = Result->SourceWidth;
  SourceHeight = Result->SourceHeight;
  Seconds = Result->Seconds;
  return true;
}

void AsyncDecoder::pause() {
  std::lock_guard Lock(Data->Mutex);
  if (!Data->Paused) {
    ++Data->Next.Generation;
    Data->Paused = true;
    Data->Pending = false;
    Data->Next.Delta = 0;
    Data->Ready.reset();
  }
}

void AsyncDecoder::close() {
  {
    std::lock_guard Lock(Data->Mutex);
    Data->Stopping = true;
    Data->Wake.notify_one();
  }
  if (Data->Worker.joinable())
    Data->Worker.join();
  Data->Next = {};
  Data->Ready.reset();
  Data->Pending = false;
  Data->Paused = false;
  Pixels.clear();
  View = {};
  SourceWidth = SourceHeight = 0;
  Seconds = -1;
  Error.clear();
}

bool AsyncDecoder::isOpen() const {
  std::lock_guard Lock(Data->Mutex);
  return !Data->Next.Path.empty();
}

void AsyncDecoder::setLooping(bool Value) {
  std::lock_guard Lock(Data->Mutex);
  Data->Next.Loop = Value;
}

} // namespace video
