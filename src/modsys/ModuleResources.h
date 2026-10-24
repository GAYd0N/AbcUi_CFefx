#pragma once

// Framework-only ownership of module registrations. No C++ state crosses the
// SDK boundary. Main-thread scopes restore identity across nested callbacks.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace framework::module_resources {

void reportException(const char *id, const char *what);
void reportInitializationFailure(const char *id, const char *reason);

enum class Lifetime { Process, Context };

struct Cleanup {
  std::function<void()> fn;
  Lifetime lifetime = Lifetime::Process;
};

struct Owner {
  explicit Owner(std::string id) : id(std::move(id)) {}
  std::string id;
  std::vector<std::string> dependencies;
  bool enabled = true;
  bool accepting = true;
  bool loading = false;
  bool initializing = false;
  bool initializationFailed = false;
  std::shared_ptr<void> imagePin;
  uint64_t nextCleanup = 1;
  std::map<uint64_t, Cleanup> cleanups;
};

using OwnerHandle = std::shared_ptr<Owner>;

inline OwnerHandle &current() {
  static OwnerHandle instance;
  return instance;
}

inline bool &contextRegistrationsOpen() {
  static bool open = false;
  return open;
}

inline bool enabled(const OwnerHandle &owner) {
  return !owner || owner->enabled;
}

inline bool accepting() {
  return !current() || current()->accepting;
}

class Scope {
public:
  explicit Scope(OwnerHandle owner)
      : Previous(current()), ImagePin(owner ? owner->imagePin : nullptr) {
    current() = std::move(owner);
  }
  ~Scope() {
    current() = std::move(Previous);
  }
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;

private:
  OwnerHandle Previous;
  // A re-entrant teardown cannot unmap the currently executing callback.
  std::shared_ptr<void> ImagePin;
};

template <typename Fn>
bool attempt(OwnerHandle owner, const char *what, Fn &&fn) {
  Scope scope(owner);
  try {
    fn();
    return true;
  } catch (...) {
    reportException(owner ? owner->id.c_str() : "<framework>", what);
    return false;
  }
}

template <typename Fn, typename... Args>
auto invoke(OwnerHandle owner, const char *what, Fn fn, Args... args)
    -> std::invoke_result_t<Fn, Args...> {
  using Result = std::invoke_result_t<Fn, Args...>;
  if constexpr (std::is_void_v<Result>) {
    if (fn && enabled(owner))
      attempt(owner, what, [&] { fn(args...); });
  } else {
    Result result{};
    if (fn && enabled(owner))
      attempt(owner, what, [&] { result = fn(args...); });
    return result;
  }
}

class Registration {
public:
  Registration() : Owner_(current()) {}
  ~Registration() {
    forget();
  }
  Registration(const Registration &) = delete;
  Registration &operator=(const Registration &) = delete;
  Registration(Registration &&other) noexcept
      : Owner_(std::move(other.Owner_)), Id(std::exchange(other.Id, 0)) {}
  Registration &operator=(Registration &&other) noexcept {
    if (this != &other) {
      forget();
      Owner_ = std::move(other.Owner_);
      Id = std::exchange(other.Id, 0);
    }
    return *this;
  }
  const OwnerHandle &owner() const {
    return Owner_;
  }
  bool tracked() const {
    return Id != 0;
  }
  void track(std::function<void()> cleanup,
             Lifetime lifetime = Lifetime::Process) {
    forget();
    if (Owner_) {
      Id = Owner_->nextCleanup++;
      Owner_->cleanups.emplace(Id, Cleanup{std::move(cleanup), lifetime});
    }
  }
  void forget() {
    if (Owner_)
      Owner_->cleanups.erase(Id);
    Id = 0;
  }

private:
  OwnerHandle Owner_;
  uint64_t Id = 0;
};

inline void stop(const OwnerHandle &owner) {
  if (owner) {
    owner->enabled = false;
    owner->accepting = false;
  }
}

inline bool failInitialization(const char *reason) {
  const auto owner = current();
  if (!owner || !owner->initializing || owner->initializationFailed)
    return false;
  owner->initializationFailed = true;
  stop(owner);
  reportInitializationFailure(owner->id.c_str(), reason);
  return true;
}

inline void release(const OwnerHandle &owner) {
  if (!owner)
    return;
  stop(owner);
  auto cleanups = std::move(owner->cleanups);
  owner->cleanups.clear();
  for (auto it = cleanups.rbegin(); it != cleanups.rend(); ++it)
    attempt(owner, "registration cleanup", it->second.fn);
}

inline void releaseContext(const OwnerHandle &owner) {
  if (!owner)
    return;
  std::map<uint64_t, Cleanup> cleanups;
  for (auto it = owner->cleanups.begin(); it != owner->cleanups.end();) {
    if (it->second.lifetime == Lifetime::Context) {
      auto entry = owner->cleanups.extract(it++);
      cleanups.insert(std::move(entry));
    } else {
      ++it;
    }
  }
  for (auto it = cleanups.rbegin(); it != cleanups.rend(); ++it)
    attempt(owner, "context registration cleanup", it->second.fn);
}

} // namespace framework::module_resources
