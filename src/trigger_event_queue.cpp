#include "trigger_event_queue.h"
#include <cstdio>
#include <cstring>

// Base64 alphabet
static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64Encode(const char* src, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned char b0 = (unsigned char)src[i];
        unsigned char b1 = (i + 1 < len) ? (unsigned char)src[i + 1] : 0;
        unsigned char b2 = (i + 2 < len) ? (unsigned char)src[i + 2] : 0;
        out += B64[b0 >> 2];
        out += B64[((b0 & 0x03) << 4) | (b1 >> 4)];
        out += (i + 1 < len) ? B64[((b1 & 0x0f) << 2) | (b2 >> 6)] : '=';
        out += (i + 2 < len) ? B64[b2 & 0x3f] : '=';
    }
    return out;
}

std::string makeTriggerEventId(const char* triggerId, time_t firedAt) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s:%ld", triggerId, (long)firedAt);
    return base64Encode(buf, strlen(buf));
}

void TriggerEventQueue::push(const TriggerEvent& e) {
    if (_count == TRIGGER_EVENT_QUEUE_CAPACITY) {
        // Drop oldest: advance head
        _head  = (_head + 1) % TRIGGER_EVENT_QUEUE_CAPACITY;
        _count--;
    }
    size_t tail = (_head + _count) % TRIGGER_EVENT_QUEUE_CAPACITY;
    _buf[tail] = e;
    _count++;
}

const TriggerEvent* TriggerEventQueue::front() const {
    if (_count == 0) return nullptr;
    return &_buf[_head];
}

void TriggerEventQueue::pop() {
    if (_count == 0) return;
    _head  = (_head + 1) % TRIGGER_EVENT_QUEUE_CAPACITY;
    _count--;
}
