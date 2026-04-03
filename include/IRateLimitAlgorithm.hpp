#pragma once
#include "Token.hpp"
#include "Enums.hpp"
#include <string>
#include <chrono>

namespace RateLimiterNS {

// ─── Result returned by every algorithm check ─────────────────────
struct RateLimitResult {
    Status      status;           // ALLOWED / DENIED / THROTTLED
    int         remaining;        // Tokens/requests left
    int         limit;            // Total limit
    long long   retryAfterMs;     // Milliseconds until retry (for 429)
    std::string clientId;
    Algorithm   algorithm;

    // ── Convenience constructors ──
    static RateLimitResult allowed(const std::string& id, int rem, int lim,
                                   Algorithm algo) {
        return {Status::ALLOWED, rem, lim, 0, id, algo};
    }
    static RateLimitResult denied(const std::string& id, int lim,
                                  long long retryMs, Algorithm algo) {
        return {Status::DENIED, 0, lim, retryMs, id, algo};
    }
};

// ─── Algorithm Strategy Interface ────────────────────────────────
class IRateLimitAlgorithm {
public:
    virtual ~IRateLimitAlgorithm() = default;

    // Main entry point: should this request be allowed?
    virtual RateLimitResult checkAndConsume(IToken& token,
                                            int requestCost = 1) = 0;

    // Peek without consuming (for monitoring/preview)
    virtual RateLimitResult peek(const IToken& token) const = 0;

    // Which algorithm is this?
    virtual Algorithm getType() const = 0;

    // Reset state (for testing or admin override)
    virtual void reset(IToken& token) = 0;
};

} // namespace RateLimiterNS
