// framework::EventBus implementation - see EventBus.h.
#include "EventBus.h"

#include <vector>

namespace framework {
namespace {

constexpr EventSubscription kNoSubscription = 0;

} // namespace

EventBus::~EventBus() {
  while (!Records.empty())
    unsubscribe(Records.begin()->first);
}

EventBus::Channel *EventBus::find(const char *channel) {
  const auto it = Channels.find(channel);
  return it == Channels.end() ? nullptr : &it->second;
}

const EventBus::Channel *EventBus::find(const char *channel) const {
  const auto it = Channels.find(channel);
  return it == Channels.end() ? nullptr : &it->second;
}

EventSubscription EventBus::subscribe(const char *channel,
                                      EventPriority priority, EventSinkFn sink,
                                      void *userdata) {
  if (!channel || !sink || !module_resources::accepting())
    return kNoSubscription;
  Channel &target = Channels[channel];
  DataList &list = target.data[priority];
  const auto owner = module_resources::current();
  const auto handle = list.append([sink, userdata, owner](const void *payload) {
    module_resources::invoke(owner, "event", sink, userdata, payload);
  });
  ++target.count;
  const EventSubscription token = NextToken++;
  Record &record = Records[token];
  record.channel = channel;
  record.priority = priority;
  record.mode = kModeData;
  record.dataHandle = handle;
  record.lifetime.track([this, token] { unsubscribe(token); });
  return token;
}

EventSubscription EventBus::subscribeVeto(const char *channel,
                                          EventPriority priority,
                                          EventVetoFn sink, void *userdata) {
  if (!channel || !sink || !module_resources::accepting())
    return kNoSubscription;
  Channel &target = Channels[channel];
  VetoList &list = target.veto[priority];
  const auto owner = module_resources::current();
  const auto handle = list.append([sink, userdata, owner](const void *payload) {
    return module_resources::invoke(owner, "veto event", sink, userdata,
                                    payload) != 0;
  });
  ++target.count;
  const EventSubscription token = NextToken++;
  Record &record = Records[token];
  record.channel = channel;
  record.priority = priority;
  record.mode = kModeVeto;
  record.vetoHandle = handle;
  record.lifetime.track([this, token] { unsubscribe(token); });
  return token;
}

EventSubscription EventBus::subscribeVetoAggregate(const char *channel,
                                                   EventPriority priority,
                                                   EventVetoFn sink,
                                                   void *userdata) {
  if (!channel || !sink || !module_resources::accepting())
    return kNoSubscription;
  Channel &target = Channels[channel];
  VetoList &list = target.vetoAggregate[priority];
  const auto owner = module_resources::current();
  const EventSubscription token = NextToken++;
  const auto handle =
      list.append([this, token, sink, userdata, owner](const void *payload) {
        if (Records.find(token) == Records.end())
          return false;
        return module_resources::invoke(owner, "aggregate veto event", sink,
                                        userdata, payload) != 0;
      });
  ++target.count;
  Record &record = Records[token];
  record.channel = channel;
  record.priority = priority;
  record.mode = kModeVetoAggregate;
  record.vetoAggregateHandle = handle;
  record.lifetime.track([this, token] { unsubscribe(token); });
  return token;
}

void EventBus::unsubscribe(EventSubscription subscription) {
  if (subscription == kNoSubscription)
    return;
  const auto it = Records.find(subscription);
  if (it == Records.end())
    return;
  Record &record = it->second;
  if (Channel *target = find(record.channel.c_str())) {
    --target->count;
    switch (record.mode) {
    case kModeVeto: {
      const auto bucket = target->veto.find(record.priority);
      if (bucket != target->veto.end())
        bucket->second.remove(record.vetoHandle);
      break;
    }
    case kModeVetoAggregate: {
      const auto bucket = target->vetoAggregate.find(record.priority);
      if (bucket != target->vetoAggregate.end())
        bucket->second.remove(record.vetoAggregateHandle);
      break;
    }
    default: {
      const auto bucket = target->data.find(record.priority);
      if (bucket != target->data.end())
        bucket->second.remove(record.dataHandle);
      break;
    }
    }
  }
  Records.erase(it);
}

void EventBus::ownWrapper(EventSubscription token,
                          std::unique_ptr<FnWrapperBase> wrapper) {
  const auto it = Records.find(token);
  if (it == Records.end())
    return; // the unique_ptr releases an unowned wrapper
  it->second.wrapper = std::move(wrapper);
}

void EventBus::publishRaw(const char *channel, const void *payload) {
  const Channel *target = find(channel);
  if (!target)
    return;
  for (const auto &bucket : target->data) {
    const DataList &list = bucket.second;
    list.forEach(
        [&](const DataList::Handle &, const DataList::Callback &callback) {
          callback(payload);
        });
  }
}

bool EventBus::publishVetoRaw(const char *channel, const void *payload) {
  const Channel *target = find(channel);
  if (!target)
    return false;
  bool vetoed = false;
  for (const auto &bucket : target->veto) {
    if (vetoed)
      break;
    const VetoList &list = bucket.second;
    list.forEachIf(
        [&](const VetoList::Handle &, const VetoList::Callback &callback) {
          if (callback(payload)) {
            vetoed = true;
            return false;
          }
          return true;
        });
  }
  return vetoed;
}

bool EventBus::publishVetoAggregateRaw(const char *channel,
                                       const void *payload) {
  const Channel *target = find(channel);
  if (!target)
    return false;
  // Copy before calling module code so hooks may change during dispatch.
  // New handlers join the next dispatch; revoked tokens are skipped.
  std::vector<VetoList::Callback> callbacks;
  for (const auto &bucket : target->vetoAggregate) {
    const VetoList &list = bucket.second;
    list.forEach(
        [&](const VetoList::Handle &, const VetoList::Callback &callback) {
          callbacks.push_back(callback);
        });
  }
  bool vetoed = false;
  for (const auto &callback : callbacks)
    if (callback(payload))
      vetoed = true;
  return vetoed;
}

size_t EventBus::subscriberCount(const char *channel) const {
  const Channel *target = find(channel);
  return target ? target->count : 0;
}

EventBus &eventBus() {
  static EventBus instance;
  return instance;
}

} // namespace framework
