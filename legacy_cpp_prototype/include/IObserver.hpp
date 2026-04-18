#pragma once
#include "Enums.hpp"
#include <string>
#include <vector>
#include <memory>
#include <chrono>

namespace RateLimiterNS {

// ─── Event passed to all observers ───────────────────────────────
struct RateLimitEvent {
    std::string clientId;
    Status      status;
    Algorithm   algorithm;
    Tier        tier;
    int         remaining;
    int         limit;
    long long   retryAfterMs;
    std::chrono::system_clock::time_point timestamp;

    RateLimitEvent(const std::string& id, Status s, Algorithm a,
                   Tier t, int rem, int lim, long long retry = 0)
        : clientId(id), status(s), algorithm(a), tier(t),
          remaining(rem), limit(lim), retryAfterMs(retry),
          timestamp(std::chrono::system_clock::now()) {}
};

// ─── Observer Interface ───────────────────────────────────────────
class IObserver {
public:
    virtual ~IObserver() = default;
    virtual void onEvent(const RateLimitEvent& event) = 0;
    virtual std::string getName() const = 0;
};

// ─── Observable Interface ─────────────────────────────────────────
class IObservable {
public:
    virtual ~IObservable() = default;
    virtual void addObserver(std::shared_ptr<IObserver> observer)    = 0;
    virtual void removeObserver(const std::string& observerName)     = 0;
    virtual void notifyObservers(const RateLimitEvent& event)        = 0;
};

} // namespace RateLimiterNS
