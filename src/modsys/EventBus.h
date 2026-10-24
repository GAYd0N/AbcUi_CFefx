#pragma once

// framework::EventBus - named event channels with priority buckets, built on
// AbcCore's eventpp vocabulary (abc::CallbackList).
//
// The bus is the module-facing exchange: the framework data layer publishes
// typed occurrences (network messages, user operations, lifecycle ticks) and
// modules subscribe through the C ABI with a priority, so behaviour never
// depends on module discovery or load order. Channel ids are lowercase dotted
// strings owned by the publishing subsystem ("netmsg.health", "input.key",
// "client.frame"); payloads are C-compatible PODs shared with sdk/abcui and a
// sink receives a const pointer that is only valid for the dispatch.
//
// Ordering: buckets dispatch in descending priority (priority > 0 runs before
// the publisher's default 0 as interception, < 0 runs after as fallback), and
// within a bucket the eventpp CallbackList keeps registration order. Veto
// channels stop at the first sink that returns true - a higher-priority
// interception hides the event from every sink below it. A sink must not
// subscribe or unsubscribe its own data/short-circuit-veto channel while a
// dispatch is running. Aggregate veto dispatch snapshots its handlers: removed
// handlers are skipped and new ones join the next dispatch.
//
// Subscriptions return an opaque token; unsubscribe is idempotent. The bus is
// a process-lifetime service: SDK sinks retain their creating module's scope
// and are revoked automatically on failure/final unload, not HUD_Shutdown.
// Framework-only subscriptions with no module scope retain explicit lifetime.

#include <cstdint>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <abc/Events.h>

#include "ModuleResources.h"

namespace framework {

using EventPriority = int;

// Opaque subscription token; 0 is never issued.
using EventSubscription = std::uint64_t;

// Module-facing data sink. The payload pointer is only valid for the call.
using EventSinkFn = void (*)(void *userdata, const void *payload);
// Matches the C SDK callback exactly; every nonzero result is a veto.
using EventVetoFn = int (*)(void *userdata, const void *payload);

class EventBus {
public:
  ~EventBus();
  // Subscribes a module-facing sink. Same priority keeps registration order.
  // Returns 0 for a null channel or sink.
  EventSubscription subscribe(const char *channel, EventPriority priority,
                              EventSinkFn sink, void *userdata);
  // Subscribes to a veto channel; a nonzero result joins the channel vote and
  // a true result stops the dispatch at this sink.
  EventSubscription subscribeVeto(const char *channel, EventPriority priority,
                                  EventVetoFn sink, void *userdata);
  // Aggregate veto variant: every sink runs in priority order regardless of
  // the other sinks' results (the chained-command semantics - each
  // subscriber independently decides whether the engine's original runs).
  EventSubscription subscribeVetoAggregate(const char *channel,
                                           EventPriority priority,
                                           EventVetoFn sink, void *userdata);
  // Idempotent; unknown tokens are ignored.
  void unsubscribe(EventSubscription subscription);

  // C++ sugar: subscribes a callable taking the typed payload. The closure
  // is owned by the bus and destroyed on unsubscribe.
  template <typename T, typename Fn>
  EventSubscription subscribeFn(const char *channel, EventPriority priority,
                                Fn &&fn) {
    using Wrapper = FnWrapper<T, std::decay_t<Fn>>;
    auto wrapper = std::make_unique<Wrapper>(std::forward<Fn>(fn));
    const EventSubscription token =
        subscribe(channel, priority, &Wrapper::thunk, wrapper.get());
    ownWrapper(token, std::move(wrapper));
    return token;
  }

  // Dispatches a data channel; the payload stays caller-owned.
  template <typename T> void publish(const char *channel, const T &payload) {
    publishRaw(channel, static_cast<const void *>(&payload));
  }
  void publishRaw(const char *channel, const void *payload);

  // Dispatches a veto channel; true when some sink vetoed.
  template <typename T>
  bool publishVeto(const char *channel, const T &payload) {
    return publishVetoRaw(channel, static_cast<const void *>(&payload));
  }
  bool publishVetoRaw(const char *channel, const void *payload);

  // Dispatches an aggregate veto channel; true when any sink returned true
  // (all sinks ran).
  template <typename T>
  bool publishVetoAggregate(const char *channel, const T &payload) {
    return publishVetoAggregateRaw(channel,
                                   static_cast<const void *>(&payload));
  }
  bool publishVetoAggregateRaw(const char *channel, const void *payload);

  // Debug/introspection helper: total sinks registered on a channel.
  size_t subscriberCount(const char *channel) const;

private:
  struct FnWrapperBase {
    virtual ~FnWrapperBase() = default;
    virtual void invoke(const void *payload) = 0;
  };

  template <typename T, typename Fn> struct FnWrapper final : FnWrapperBase {
    FnWrapper(Fn &&fn_) : fn(std::move(fn_)) {}
    void invoke(const void *payload) override {
      fn(*static_cast<const T *>(payload));
    }
    static void thunk(void *userdata, const void *payload) {
      static_cast<FnWrapper *>(userdata)->invoke(payload);
    }
    Fn fn;
  };

  using DataList = abc::CallbackList<void(const void *)>;
  using VetoList = abc::CallbackList<bool(const void *)>;

  struct Channel {
    // Descending priority order.
    std::map<EventPriority, DataList, std::greater<EventPriority>> data;
    std::map<EventPriority, VetoList, std::greater<EventPriority>> veto;
    std::map<EventPriority, VetoList, std::greater<EventPriority>>
        vetoAggregate;
    size_t count = 0;
  };

  // Dispatch modes for a subscription (Record::mode).
  enum {
    kModeData = 0,
    kModeVeto = 1,
    kModeVetoAggregate = 2,
  };

  struct Record {
    std::string channel;
    EventPriority priority = 0;
    int mode = kModeData;
    DataList::Handle dataHandle;
    VetoList::Handle vetoHandle;
    VetoList::Handle vetoAggregateHandle;
    std::unique_ptr<FnWrapperBase> wrapper; // set through ownWrapper
    module_resources::Registration lifetime;
  };

  Channel *find(const char *channel);
  const Channel *find(const char *channel) const;
  void ownWrapper(EventSubscription token,
                  std::unique_ptr<FnWrapperBase> wrapper);

  std::map<std::string, Channel, std::less<>> Channels;
  std::map<EventSubscription, Record> Records;
  EventSubscription NextToken = 1;
};

// Process-wide bus (function-local static, per the AGENTS.md service rule).
EventBus &eventBus();

} // namespace framework
