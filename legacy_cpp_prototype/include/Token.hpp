#pragma once
#include "Enums.hpp"
#include <string>
#include <chrono>
#include <memory>

namespace RateLimiterNS {

// ─── Configuration: rules for a specific client/tier ─────────────
struct TokenConfig {
    std::string  clientId;          // "user_123" or "ip_192.168.1.1"
    Tier         tier;              // FREE / BASIC / PREMIUM / ENTERPRISE
    Algorithm    algorithm;         // Which algorithm to apply
    int          maxTokens;         // Bucket capacity (max burst)
    int          refillRate;        // Tokens added per refill cycle
    RefillUnit   refillUnit;        // Per SECOND / MINUTE / HOUR
    int          windowSizeSeconds; // For Fixed/Sliding window

    // ── Constructor with sensible defaults ──
    TokenConfig(
        const std::string& id,
        Tier t               = Tier::FREE,
        Algorithm algo       = Algorithm::TOKEN_BUCKET,
        int maxTok           = 10,
        int rate             = 10,
        RefillUnit unit      = RefillUnit::MINUTE,
        int windowSec        = 60
    ) : clientId(id), tier(t), algorithm(algo),
        maxTokens(maxTok), refillRate(rate),
        refillUnit(unit), windowSizeSeconds(windowSec) {}
};

// ─── Interface: what every Token must expose ──────────────────────
class IToken {
public:
    virtual ~IToken() = default;

    virtual bool        consume(int count = 1)    = 0; // Try to use tokens
    virtual void        refill()                  = 0; // Add tokens back
    virtual int         getAvailable()      const = 0; // How many left?
    virtual std::string getClientId()       const = 0;
    virtual Tier        getTier()           const = 0;
    virtual Algorithm   getAlgorithm()      const = 0;
    virtual TokenConfig getConfig()         const = 0;
    virtual std::chrono::steady_clock::time_point
                        getLastRefillTime() const = 0;
};

// ─── Concrete Token: holds state for one client ───────────────────
class Token : public IToken {
public:
    explicit Token(const TokenConfig& config);

    bool        consume(int count = 1)    override;
    void        refill()                  override;
    int         getAvailable()      const override;
    std::string getClientId()       const override;
    Tier        getTier()           const override;
    Algorithm   getAlgorithm()      const override;
    TokenConfig getConfig()         const override;
    std::chrono::steady_clock::time_point
                getLastRefillTime() const override;

private:
    TokenConfig  config_;
    int          availableTokens_;
    std::chrono::steady_clock::time_point lastRefillTime_;
};

} // namespace RateLimiterNS
