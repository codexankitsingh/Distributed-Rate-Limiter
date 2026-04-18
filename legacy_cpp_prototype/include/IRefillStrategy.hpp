#pragma once
#include "Token.hpp"
#include <memory>

namespace RateLimiterNS {

// ─── Strategy Interface ───────────────────────────────────────────
class IRefillStrategy {
public:
    virtual ~IRefillStrategy() = default;

    // Core: calculate how many tokens to add and apply them
    virtual void refill(IToken& token) = 0;

    // How many tokens should be added right now?
    virtual int  calculateRefillAmount(const IToken& token) const = 0;

    // Is it time to refill yet?
    virtual bool shouldRefill(const IToken& token)          const = 0;
};

// ─── Concrete Strategy 1: Fixed Rate Refill ──────────────────────
// Adds N tokens every T seconds. Used by Token Bucket.
class FixedRateRefillStrategy : public IRefillStrategy {
public:
    void refill(IToken& token)                              override;
    int  calculateRefillAmount(const IToken& token) const  override;
    bool shouldRefill(const IToken& token)          const  override;
};

// ─── Concrete Strategy 2: Continuous Drip Refill ─────────────────
// Adds fractional tokens continuously. Smoother than fixed rate.
class ContinuousDripRefillStrategy : public IRefillStrategy {
public:
    void refill(IToken& token)                              override;
    int  calculateRefillAmount(const IToken& token) const  override;
    bool shouldRefill(const IToken& token)          const  override;
};

// ─── Concrete Strategy 3: No Refill ──────────────────────────────
// Used by Fixed Window — window resets, not refills.
class NoRefillStrategy : public IRefillStrategy {
public:
    void refill(IToken& token)                              override;
    int  calculateRefillAmount(const IToken& token) const  override;
    bool shouldRefill(const IToken& token)          const  override;
};

} // namespace RateLimiterNS
