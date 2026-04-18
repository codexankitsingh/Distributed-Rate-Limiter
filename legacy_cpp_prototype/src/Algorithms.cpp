#include "Algorithms.hpp"
#include <chrono>
#include <stdexcept>
#include <cmath>

namespace RateLimiterNS {

// ─────────────────────────────────────────────────────────────────
// Helper: elapsed seconds between two time points
// ─────────────────────────────────────────────────────────────────
static double secondsBetween(
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end)
{
    return std::chrono::duration<double>(end - start).count();
}

// ─────────────────────────────────────────────────────────────────
// Helper: convert RefillUnit → seconds (duplicated for locality)
// ─────────────────────────────────────────────────────────────────
static double unitToSecs(RefillUnit unit) {
    switch (unit) {
        case RefillUnit::SECOND: return 1.0;
        case RefillUnit::MINUTE: return 60.0;
        case RefillUnit::HOUR:   return 3600.0;
        default:                 return 60.0;
    }
}

// ═════════════════════════════════════════════════════════════════
//  1. TOKEN BUCKET ALGORITHM
// ═════════════════════════════════════════════════════════════════
TokenBucketAlgorithm::TokenBucketAlgorithm(
    std::shared_ptr<IRefillStrategy> refillStrategy)
    : refillStrategy_(std::move(refillStrategy))
{
    if (!refillStrategy_)
        throw std::invalid_argument(
            "TokenBucketAlgorithm: refillStrategy cannot be null");
}

RateLimitResult TokenBucketAlgorithm::checkAndConsume(
    IToken& token, int requestCost)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Step 1: Refill tokens based on elapsed time
    refillStrategy_->refill(token);

    const auto& cfg = token.getConfig();

    // Step 2: Check if enough tokens available
    if (token.getAvailable() < requestCost) {
        // Calculate retry time
        double unitSecs        = unitToSecs(cfg.refillUnit);
        double tokensPerSecond = cfg.refillRate / unitSecs;
        double needed          = requestCost - token.getAvailable();
        long long retryMs      = static_cast<long long>(
                                     std::ceil(needed / tokensPerSecond) * 1000);

        return RateLimitResult::denied(
            cfg.clientId, cfg.maxTokens, retryMs, Algorithm::TOKEN_BUCKET);
    }

    // Step 3: Consume and allow
    token.consume(requestCost);

    return RateLimitResult::allowed(
        cfg.clientId,
        token.getAvailable(),
        cfg.maxTokens,
        Algorithm::TOKEN_BUCKET);
}

RateLimitResult TokenBucketAlgorithm::peek(const IToken& token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& cfg = token.getConfig();

    // Peek: check without consuming
    bool wouldAllow = token.getAvailable() >= 1;
    if (wouldAllow) {
        return RateLimitResult::allowed(
            cfg.clientId,
            token.getAvailable(),
            cfg.maxTokens,
            Algorithm::TOKEN_BUCKET);
    }
    return RateLimitResult::denied(
        cfg.clientId, cfg.maxTokens, 0, Algorithm::TOKEN_BUCKET);
}

Algorithm TokenBucketAlgorithm::getType() const {
    return Algorithm::TOKEN_BUCKET;
}

void TokenBucketAlgorithm::reset(IToken& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    token.refill(); // restore to max
}

// ═════════════════════════════════════════════════════════════════
//  2. LEAKY BUCKET ALGORITHM
//
//  Analogy: water drips into a bucket with a hole at the bottom.
//  Requests enter the queue (bucket fills up).
//  They are processed (leak) at a constant rate.
//  If bucket is full → DENY.
// ═════════════════════════════════════════════════════════════════
void LeakyBucketAlgorithm::leak(BucketState& state,
                                 const TokenConfig& cfg)
{
    auto now = std::chrono::steady_clock::now();

    // How many requests should have leaked since last check?
    double elapsed         = secondsBetween(state.lastLeakTime, now);
    double unitSecs        = unitToSecs(cfg.refillUnit);
    double leakRate        = cfg.refillRate / unitSecs; // requests per second
    int    toRemove        = static_cast<int>(
                                 std::floor(elapsed * leakRate));

    // Remove leaked requests from front of queue
    for (int i = 0; i < toRemove && !state.queue.empty(); ++i) {
        state.queue.pop_front();
    }

    if (toRemove > 0) {
        state.lastLeakTime = now;
    }
}

RateLimitResult LeakyBucketAlgorithm::checkAndConsume(
    IToken& token, int requestCost)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const auto& cfg       = token.getConfig();
    auto&       state     = states_[cfg.clientId];

    // Initialize lastLeakTime on first use
    if (state.queue.empty() && state.lastLeakTime ==
        std::chrono::steady_clock::time_point{}) {
        state.lastLeakTime = std::chrono::steady_clock::now();
    }

    // Step 1: Leak processed requests
    leak(state, cfg);

    // Step 2: Check capacity
    int capacity = cfg.maxTokens;
    if (static_cast<int>(state.queue.size()) + requestCost > capacity) {
        // Queue full → DENY
        double unitSecs   = unitToSecs(cfg.refillUnit);
        double leakRate   = cfg.refillRate / unitSecs;
        long long retryMs = static_cast<long long>(
                                std::ceil(requestCost / leakRate) * 1000);

        return RateLimitResult::denied(
            cfg.clientId, capacity, retryMs, Algorithm::LEAKY_BUCKET);
    }

    // Step 3: Enqueue request
    auto now = std::chrono::steady_clock::now();
    for (int i = 0; i < requestCost; ++i) {
        state.queue.push_back(now);
    }

    int remaining = capacity - static_cast<int>(state.queue.size());
    return RateLimitResult::allowed(
        cfg.clientId, remaining, capacity, Algorithm::LEAKY_BUCKET);
}

RateLimitResult LeakyBucketAlgorithm::peek(const IToken& token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& cfg = token.getConfig();

    auto it = states_.find(cfg.clientId);
    if (it == states_.end()) {
        return RateLimitResult::allowed(
            cfg.clientId, cfg.maxTokens, cfg.maxTokens,
            Algorithm::LEAKY_BUCKET);
    }

    int remaining = cfg.maxTokens - static_cast<int>(it->second.queue.size());
    if (remaining > 0) {
        return RateLimitResult::allowed(
            cfg.clientId, remaining, cfg.maxTokens, Algorithm::LEAKY_BUCKET);
    }
    return RateLimitResult::denied(
        cfg.clientId, cfg.maxTokens, 0, Algorithm::LEAKY_BUCKET);
}

Algorithm LeakyBucketAlgorithm::getType() const {
    return Algorithm::LEAKY_BUCKET;
}

void LeakyBucketAlgorithm::reset(IToken& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    states_.erase(token.getClientId());
}

// ═════════════════════════════════════════════════════════════════
//  3. FIXED WINDOW ALGORITHM
//
//  Timeline: |--window1--|--window2--|--window3--|
//  Each window has its own counter.
//  Counter resets when window expires.
//
//  ⚠️  Boundary burst problem:
//  99 req at end of window1 + 99 req at start of window2
//  = 198 requests in 2 seconds with limit=100!
// ═════════════════════════════════════════════════════════════════
bool FixedWindowAlgorithm::isWindowExpired(
    const WindowState& ws, const TokenConfig& cfg) const
{
    auto now     = std::chrono::steady_clock::now();
    double elapsed = secondsBetween(ws.windowStart, now);
    return elapsed >= static_cast<double>(cfg.windowSizeSeconds);
}

RateLimitResult FixedWindowAlgorithm::checkAndConsume(
    IToken& token, int requestCost)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const auto& cfg   = token.getConfig();
    auto&       state = states_[cfg.clientId];

    // Initialize window on first use
    if (state.windowStart == std::chrono::steady_clock::time_point{}) {
        state.windowStart = std::chrono::steady_clock::now();
        state.count       = 0;
    }

    // Step 1: Reset window if expired
    if (isWindowExpired(state, cfg)) {
        state.count       = 0;
        state.windowStart = std::chrono::steady_clock::now();
    }

    // Step 2: Check limit
    int limit = cfg.maxTokens;
    if (state.count + requestCost > limit) {
        // Calculate time until window resets
        auto now     = std::chrono::steady_clock::now();
        double elapsed = secondsBetween(state.windowStart, now);
        double remaining = cfg.windowSizeSeconds - elapsed;
        long long retryMs = static_cast<long long>(remaining * 1000);

        return RateLimitResult::denied(
            cfg.clientId, limit, retryMs, Algorithm::FIXED_WINDOW);
    }

    // Step 3: Increment and allow
    state.count += requestCost;

    return RateLimitResult::allowed(
        cfg.clientId,
        limit - state.count,
        limit,
        Algorithm::FIXED_WINDOW);
}

RateLimitResult FixedWindowAlgorithm::peek(const IToken& token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& cfg = token.getConfig();

    auto it = states_.find(cfg.clientId);
    if (it == states_.end()) {
        return RateLimitResult::allowed(
            cfg.clientId, cfg.maxTokens, cfg.maxTokens,
            Algorithm::FIXED_WINDOW);
    }

    int remaining = cfg.maxTokens - it->second.count;
    if (remaining > 0) {
        return RateLimitResult::allowed(
            cfg.clientId, remaining, cfg.maxTokens, Algorithm::FIXED_WINDOW);
    }
    return RateLimitResult::denied(
        cfg.clientId, cfg.maxTokens, 0, Algorithm::FIXED_WINDOW);
}

Algorithm FixedWindowAlgorithm::getType() const {
    return Algorithm::FIXED_WINDOW;
}

void FixedWindowAlgorithm::reset(IToken& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    states_.erase(token.getClientId());
}

// ═════════════════════════════════════════════════════════════════
//  4. SLIDING WINDOW ALGORITHM
//
//  Fixes Fixed Window's boundary burst problem.
//
//  Key Formula:
//
//    rate = prevCount × ((W - t) / W) + currCount
//
//  Where:
//    W         = window size in seconds
//    t         = elapsed time in current window (0 ≤ t < W)
//    prevCount = requests in previous window
//    currCount = requests in current window so far
//
//  Intuition: as we move through the current window,
//  the previous window's weight linearly decreases from 1→0.
//  This gives a smooth, continuous rate estimate.
// ═════════════════════════════════════════════════════════════════
double SlidingWindowAlgorithm::calculateCurrentRate(
    const SlidingState& state, const TokenConfig& cfg) const
{
    auto   now     = std::chrono::steady_clock::now();
    double elapsed = secondsBetween(state.windowStart, now);
    double W       = static_cast<double>(cfg.windowSizeSeconds);

    // Weight of previous window decreases linearly
    double prevWeight = (W - elapsed) / W;

    // rate = P × weight + C
    return state.prevWindowCount * prevWeight + state.currWindowCount;
}

RateLimitResult SlidingWindowAlgorithm::checkAndConsume(
    IToken& token, int requestCost)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const auto& cfg   = token.getConfig();
    auto&       state = states_[cfg.clientId];

    // Initialize on first use
    if (state.windowStart == std::chrono::steady_clock::time_point{}) {
        state.windowStart     = std::chrono::steady_clock::now();
        state.prevWindowCount = 0;
        state.currWindowCount = 0;
    }

    auto   now     = std::chrono::steady_clock::now();
    double elapsed = secondsBetween(state.windowStart, now);
    double W       = static_cast<double>(cfg.windowSizeSeconds);

    // Step 1: Advance window if current window expired
    if (elapsed >= W) {
        // Slide: current becomes previous, reset current
        state.prevWindowCount = state.currWindowCount;
        state.currWindowCount = 0;
        state.windowStart     = now;
        elapsed               = 0.0;
    }

    // Step 2: Calculate effective rate using sliding formula
    double rate  = calculateCurrentRate(state, cfg);
    int    limit = cfg.maxTokens;

    if (rate + requestCost > static_cast<double>(limit)) {
        // Estimate retry time
        double prevWeight  = (W - elapsed) / W;
        double prevContrib = state.prevWindowCount * prevWeight;
        double available   = limit - prevContrib - state.currWindowCount;
        double waitFraction = (available <= 0)
            ? 1.0
            : (requestCost - available) / (limit / W);
        long long retryMs = static_cast<long long>(
                                std::max(0.0, waitFraction) * 1000);

        return RateLimitResult::denied(
            cfg.clientId, limit, retryMs, Algorithm::SLIDING_WINDOW);
    }

    // Step 3: Increment current window count
    state.currWindowCount += requestCost;

    int remaining = static_cast<int>(limit - rate - requestCost);
    return RateLimitResult::allowed(
        cfg.clientId,
        std::max(0, remaining),
        limit,
        Algorithm::SLIDING_WINDOW);
}

RateLimitResult SlidingWindowAlgorithm::peek(const IToken& token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& cfg = token.getConfig();

    auto it = states_.find(cfg.clientId);
    if (it == states_.end()) {
        return RateLimitResult::allowed(
            cfg.clientId, cfg.maxTokens, cfg.maxTokens,
            Algorithm::SLIDING_WINDOW);
    }

    double rate      = calculateCurrentRate(it->second, cfg);
    int    remaining = static_cast<int>(cfg.maxTokens - rate);
    if (remaining > 0) {
        return RateLimitResult::allowed(
            cfg.clientId, remaining, cfg.maxTokens,
            Algorithm::SLIDING_WINDOW);
    }
    return RateLimitResult::denied(
        cfg.clientId, cfg.maxTokens, 0, Algorithm::SLIDING_WINDOW);
}

Algorithm SlidingWindowAlgorithm::getType() const {
    return Algorithm::SLIDING_WINDOW;
}

void SlidingWindowAlgorithm::reset(IToken& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    states_.erase(token.getClientId());
}

} // namespace RateLimiterNS
