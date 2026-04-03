#pragma once
#include "IRateLimitAlgorithm.hpp"
#include "IRefillStrategy.hpp"
#include <memory>
#include <unordered_map>
#include <deque>
#include <chrono>
#include <mutex>

namespace RateLimiterNS {

// ─────────────────────────────────────────────────────────────────
// 1. TOKEN BUCKET
//    • Tokens refill at fixed rate
//    • Allows bursts up to maxTokens
//    • Industry standard (Stripe, AWS)
//
//    State:  tokens (float), lastRefillTime
//    Allow:  tokens >= requestCost → consume, else DENY
//    Math:   newTokens = min(max, current + rate * elapsed)
// ─────────────────────────────────────────────────────────────────
class TokenBucketAlgorithm : public IRateLimitAlgorithm {
public:
    explicit TokenBucketAlgorithm(
        std::shared_ptr<IRefillStrategy> refillStrategy);

    RateLimitResult checkAndConsume(IToken& token,
                                    int requestCost = 1) override;
    RateLimitResult peek(const IToken& token)    const override;
    Algorithm       getType()                    const override;
    void            reset(IToken& token)               override;

private:
    std::shared_ptr<IRefillStrategy> refillStrategy_;
    mutable std::mutex               mutex_;
};

// ─────────────────────────────────────────────────────────────────
// 2. LEAKY BUCKET
//    • Requests enter a queue, processed at constant rate
//    • Smooths ALL bursts — no burst allowance
//    • Best for traffic shaping
//
//    State:  queue of timestamps, lastLeakTime
//    Allow:  queue.size() < capacity → enqueue, else DENY
//    Math:   leaked = floor(elapsed * leakRate)
// ─────────────────────────────────────────────────────────────────
class LeakyBucketAlgorithm : public IRateLimitAlgorithm {
public:
    RateLimitResult checkAndConsume(IToken& token,
                                    int requestCost = 1) override;
    RateLimitResult peek(const IToken& token)    const override;
    Algorithm       getType()                    const override;
    void            reset(IToken& token)               override;

private:
    // Per-client queue state (since Token holds config, not queue)
    struct BucketState {
        std::deque<std::chrono::steady_clock::time_point> queue;
        std::chrono::steady_clock::time_point lastLeakTime;
    };
    std::unordered_map<std::string, BucketState> states_;
    mutable std::mutex                           mutex_;

    void leak(BucketState& state, const TokenConfig& cfg);
};

// ─────────────────────────────────────────────────────────────────
// 3. FIXED WINDOW
//    • Count requests in fixed time slots (e.g., 12:00–12:01)
//    • Simple but has boundary burst problem:
//      99 req at 12:00:59 + 99 req at 12:01:01 = 198 in 2 seconds!
//
//    State:  counter, windowStart
//    Allow:  counter < limit → increment, else DENY
//    Reset:  when now >= windowStart + windowSize
// ─────────────────────────────────────────────────────────────────
class FixedWindowAlgorithm : public IRateLimitAlgorithm {
public:
    RateLimitResult checkAndConsume(IToken& token,
                                    int requestCost = 1) override;
    RateLimitResult peek(const IToken& token)    const override;
    Algorithm       getType()                    const override;
    void            reset(IToken& token)               override;

private:
    struct WindowState {
        int                                    count       = 0;
        std::chrono::steady_clock::time_point  windowStart;
    };
    std::unordered_map<std::string, WindowState> states_;
    mutable std::mutex                           mutex_;

    bool isWindowExpired(const WindowState& ws,
                         const TokenConfig& cfg) const;
};

// ─────────────────────────────────────────────────────────────────
// 4. SLIDING WINDOW
//    • Weighted hybrid: fixes Fixed Window's boundary problem
//    • Formula:
//      rate = prevCount * ((windowSize - elapsed) / windowSize)
//           + currCount
//    • Best accuracy vs memory tradeoff
//
//    State:  prevCount, currCount, windowStart
//    Allow:  rate < limit → increment currCount, else DENY
// ─────────────────────────────────────────────────────────────────
class SlidingWindowAlgorithm : public IRateLimitAlgorithm {
public:
    RateLimitResult checkAndConsume(IToken& token,
                                    int requestCost = 1) override;
    RateLimitResult peek(const IToken& token)    const override;
    Algorithm       getType()                    const override;
    void            reset(IToken& token)               override;

private:
    struct SlidingState {
        int   prevWindowCount = 0;
        int   currWindowCount = 0;
        std::chrono::steady_clock::time_point windowStart;
    };
    std::unordered_map<std::string, SlidingState> states_;
    mutable std::mutex                            mutex_;

    // The key formula — see math below
    double calculateCurrentRate(const SlidingState& state,
                                const TokenConfig&  cfg) const;
};

} // namespace RateLimiterNS
