#include "TokenFactory.hpp"
#include <stdexcept>

namespace RateLimiterNS {

// ─────────────────────────────────────────────────────────────────
// Tier → Default Config Mapping
//
//  Tier       │ maxTokens │ refillRate │ window
//  ───────────┼───────────┼────────────┼────────
//  FREE       │    10     │     10     │  60s
//  BASIC      │   100     │    100     │  60s
//  PREMIUM    │  1000     │   1000     │  60s
//  ENTERPRISE │ 10000     │  10000     │  60s
// ─────────────────────────────────────────────────────────────────
TokenConfig TieredTokenFactory::buildConfigForTier(
    const std::string& clientId,
    Tier               tier,
    Algorithm          algorithm) const
{
    switch (tier) {
        case Tier::FREE:
            return TokenConfig(
                clientId,
                Tier::FREE,
                algorithm,
                /*maxTokens=*/   10,
                /*refillRate=*/  10,
                RefillUnit::MINUTE,
                /*windowSec=*/   60
            );

        case Tier::BASIC:
            return TokenConfig(
                clientId,
                Tier::BASIC,
                algorithm,
                /*maxTokens=*/   100,
                /*refillRate=*/  100,
                RefillUnit::MINUTE,
                /*windowSec=*/   60
            );

        case Tier::PREMIUM:
            return TokenConfig(
                clientId,
                Tier::PREMIUM,
                algorithm,
                /*maxTokens=*/   1000,
                /*refillRate=*/  1000,
                RefillUnit::MINUTE,
                /*windowSec=*/   60
            );

        case Tier::ENTERPRISE:
            return TokenConfig(
                clientId,
                Tier::ENTERPRISE,
                algorithm,
                /*maxTokens=*/   10000,
                /*refillRate=*/  10000,
                RefillUnit::MINUTE,
                /*windowSec=*/   60
            );

        default:
            throw std::invalid_argument(
                "TieredTokenFactory: Unknown tier for client: "
                + clientId);
    }
}

// ─────────────────────────────────────────────────────────────────
// createToken: build from explicit config
// ─────────────────────────────────────────────────────────────────
std::unique_ptr<IToken> TieredTokenFactory::createToken(
    const TokenConfig& config)
{
    return std::make_unique<Token>(config);
}

// ─────────────────────────────────────────────────────────────────
// createTokenForTier: build from tier defaults
// ─────────────────────────────────────────────────────────────────
std::unique_ptr<IToken> TieredTokenFactory::createTokenForTier(
    const std::string& clientId,
    Tier               tier,
    Algorithm          algorithm)
{
    if (clientId.empty())
        throw std::invalid_argument(
            "TieredTokenFactory: clientId cannot be empty");

    TokenConfig config = buildConfigForTier(clientId, tier, algorithm);
    return std::make_unique<Token>(config);
}

} // namespace RateLimiterNS
