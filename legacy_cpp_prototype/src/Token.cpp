#include "Token.hpp"
#include <stdexcept>
#include <algorithm>

namespace RateLimiterNS {

// ─────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────
Token::Token(const TokenConfig& config)
    : config_(config)
    , availableTokens_(config.maxTokens)          // start full
    , lastRefillTime_(std::chrono::steady_clock::now())
{
    if (config.maxTokens <= 0)
        throw std::invalid_argument(
            "Token: maxTokens must be > 0 for client: " + config.clientId);

    if (config.refillRate <= 0)
        throw std::invalid_argument(
            "Token: refillRate must be > 0 for client: " + config.clientId);

    if (config.clientId.empty())
        throw std::invalid_argument("Token: clientId cannot be empty");
}

// ─────────────────────────────────────────────────────────────────
// consume()
// Try to use `count` tokens.
// Returns true  → tokens deducted, request allowed
// Returns false → not enough tokens, request denied
// ─────────────────────────────────────────────────────────────────
bool Token::consume(int count) {
    if (count <= 0)
        throw std::invalid_argument("Token::consume: count must be > 0");

    if (availableTokens_ >= count) {
        availableTokens_ -= count;
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────
// refill()
// Restore tokens to max capacity.
// Called by IRefillStrategy — strategy decides WHEN and HOW MANY.
// ─────────────────────────────────────────────────────────────────
void Token::refill() {
    availableTokens_ = config_.maxTokens;
    lastRefillTime_  = std::chrono::steady_clock::now();
}

// ─────────────────────────────────────────────────────────────────
// Getters
// ─────────────────────────────────────────────────────────────────
int Token::getAvailable() const {
    return availableTokens_;
}

std::string Token::getClientId() const {
    return config_.clientId;
}

Tier Token::getTier() const {
    return config_.tier;
}

Algorithm Token::getAlgorithm() const {
    return config_.algorithm;
}

TokenConfig Token::getConfig() const {
    return config_;
}

std::chrono::steady_clock::time_point Token::getLastRefillTime() const {
    return lastRefillTime_;
}

} // namespace RateLimiterNS
