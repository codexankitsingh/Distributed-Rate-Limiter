#pragma once
#include <string>

namespace RateLimiterNS {

// ─── Which algorithm to use ───────────────────────────────────────
enum class Algorithm {
    TOKEN_BUCKET,       // Allows bursts, refills at fixed rate
    LEAKY_BUCKET,       // Smooths traffic, constant output rate
    FIXED_WINDOW,       // Simple counter per time window
    SLIDING_WINDOW      // Weighted hybrid, no boundary bursts
};

// ─── Client tier (determines limits & priority) ───────────────────
enum class Tier {
    FREE,               // e.g., 10 req/min
    BASIC,              // e.g., 100 req/min
    PREMIUM,            // e.g., 1000 req/min
    ENTERPRISE          // e.g., unlimited / custom
};

// ─── Time unit for refill rate ────────────────────────────────────
enum class RefillUnit {
    SECOND,
    MINUTE,
    HOUR
};

// ─── Result of a rate limit check ────────────────────────────────
enum class Status {
    ALLOWED,            // Request goes through
    DENIED,             // Limit exceeded → HTTP 429
    THROTTLED,          // Soft limit → slow down
    ERROR               // Internal error (Redis down, etc.)
};

// ─── Helper: convert enums to strings (for logging) ──────────────
inline std::string toString(Algorithm a) {
    switch(a) {
        case Algorithm::TOKEN_BUCKET:   return "TOKEN_BUCKET";
        case Algorithm::LEAKY_BUCKET:   return "LEAKY_BUCKET";
        case Algorithm::FIXED_WINDOW:   return "FIXED_WINDOW";
        case Algorithm::SLIDING_WINDOW: return "SLIDING_WINDOW";
        default: return "UNKNOWN";
    }
}

inline std::string toString(Tier t) {
    switch(t) {
        case Tier::FREE:       return "FREE";
        case Tier::BASIC:      return "BASIC";
        case Tier::PREMIUM:    return "PREMIUM";
        case Tier::ENTERPRISE: return "ENTERPRISE";
        default: return "UNKNOWN";
    }
}

inline std::string toString(Status s) {
    switch(s) {
        case Status::ALLOWED:   return "ALLOWED";
        case Status::DENIED:    return "DENIED";
        case Status::THROTTLED: return "THROTTLED";
        case Status::ERROR:     return "ERROR";
        default: return "UNKNOWN";
    }
}

} // namespace RateLimiterNS
