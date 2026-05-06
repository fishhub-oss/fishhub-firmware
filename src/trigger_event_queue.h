#pragma once

#include <string>
#include <time.h>
#include <cstddef>

static constexpr size_t TRIGGER_EVENT_QUEUE_CAPACITY = 32;
static constexpr size_t TRIGGER_EVENT_MAX_READINGS   = 8;

struct TriggerEventReading {
    char  peripheral[48];
    float value;
    char  unit[16];
};

struct TriggerEvent {
    char               triggerEventId[65]; // base64(<triggerId>:<firedAt>) + null
    char               triggerId[37];      // UUID (36 + null)
    time_t             firedAt;
    TriggerEventReading readings[TRIGGER_EVENT_MAX_READINGS];
    size_t             readingCount = 0;
};

// Deterministic opaque ID derived from triggerId and firedAt via base64.
// Server deduplicates on (trigger_id, fired_at); this is just a stable handle.
std::string makeTriggerEventId(const char* triggerId, time_t firedAt);

class TriggerEventQueue {
public:
    // Enqueues an event. Drops the oldest entry if at capacity.
    void push(const TriggerEvent& e);

    const TriggerEvent* front() const;
    void pop();
    bool empty() const { return _count == 0; }
    size_t size() const { return _count; }

private:
    TriggerEvent _buf[TRIGGER_EVENT_QUEUE_CAPACITY];
    size_t       _head  = 0;
    size_t       _count = 0;
};
