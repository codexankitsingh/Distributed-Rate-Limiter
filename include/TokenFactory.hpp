#pragma once
#include "Token.hpp"
#include "Enums.hpp"
#include <memory>
#include <string>

namespace RateLimiterNS {

// ─── Abstract Factory Interface ───────────────────────────────────
class ITokenFactory {
public:
    virtual ~ITokenFactory() = default;

    // Create a token with explicit config
    virtual std::unique_ptr<IToken> createToken(
        const TokenConfig& config) = 0;

    // Create a token by tier (factory decides the config)
    virtual std::unique_ptr<IToken> createTokenForTier(
        const std::string& clientId,
        Tier               tier,
        Algorithm          algorithm = Algorithm::TOKEN_BUCKET) = 0;
};

// ─── Tiered Factory: maps Tier → sensible defaults ────────────────
//
//  Tier       │ maxTokens │ refillRate │ window
//  ───────────┼───────────┼────────────┼────────
//  FREE       │    10     │     10     │  60s
//  BASIC      │   100     │    100     │  60s
//  PREMIUM    │  1000     │   1000     │  60s
//  ENTERPRISE │ 10000     │  10000     │  60s
//
class TieredTokenFactory : public ITokenFactory {
public:
    std::unique_ptr<IToken> createToken(
        const TokenConfig& config)                         override;

    std::unique_ptr<IToken> createTokenForTier(
        const std::string& clientId,
        Tier               tier,
        Algorithm          algorithm = Algorithm::TOKEN_BUCKET) override;

private:
    TokenConfig buildConfigForTier(const std::string& clientId,
                                   Tier               tier,
                                   Algorithm          algorithm) const;
};

} // namespace RateLimiterNS
