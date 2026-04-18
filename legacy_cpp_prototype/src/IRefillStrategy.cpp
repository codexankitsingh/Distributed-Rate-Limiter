#include "IRefillStrategy.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace RateLimiterNS {

// ─────────────────────────────────────────────────────────────────
// Helper: convert RefillUnit → seconds
// ─────────────────────────────────────────────────────────────────
static double unitToSeconds(RefillUnit unit) {
    switch (unit) {
        case RefillUnit::SECOND: return 1.0;
        case RefillUnit::MINUTE: return 60.0;
        case RefillUnit::HOUR:   return 3600.0;
        default:                 return 60.0;
    }
}

// ─────────────────────────────────────────────────────────────────
// Helper: elapsed seconds since last refill
// ─────────────────────────────────────────────────────────────────
static double elapsedSeconds(const IToken& token) {
    auto now     = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(
                       now - token.getLastRefillTime()).count();
    return elapsed;
}

// ═════════════════════════════════════════════════════════════════
// FixedRateRefillStrategy
// Adds tokens in discrete chunks every full cycle.
//
// Example: refillRate=10, unit=MINUTE
//   → every 60s, add 10 tokens (capped at maxTokens)
//
// Math:
//   cycles  = floor(elapsed / unitSeconds)
//   toAdd   = cycles * refillRate
//   new     = min(maxTokens, current + toAdd)
// ═════════════════════════════════════════════════════════════════
bool FixedRateRefillStrategy::shouldRefill(const IToken& token) const {
    double elapsed    = elapsedSeconds(token);
    double unitSecs   = unitToSeconds(token.getConfig().refillUnit);
    return elapsed >= unitSecs;
}

int FixedRateRefillStrategy::calculateRefillAmount(
    const IToken& token) const
{
    double elapsed    = elapsedSeconds(token);
    double unitSecs   = unitToSeconds(token.getConfig().refillUnit);
    int    cycles     = static_cast<int>(std::floor(elapsed / unitSecs));
    int    toAdd      = cycles * token.getConfig().refillRate;

    // Cap at maxTokens
    int available = token.getAvailable();
    int maxTok    = token.getConfig().maxTokens;
    return std::min(toAdd, maxTok - available);
}

void FixedRateRefillStrategy::refill(IToken& token) {
    if (!shouldRefill(token)) return;

    int toAdd = calculateRefillAmount(token);
    if (toAdd <= 0) return;

    // We call consume(-toAdd) trick via refill() on Token
    // But Token::refill() resets to max — so we need a smarter approach.
    // We use the config to manually set available via repeated consume logic.
    // Since IToken doesn't expose setAvailable(), we use refill() + consume().
    //
    // Design note: Token::refill() resets to maxTokens.
    // Then we consume (maxTokens - desired) to land at the right value.
    int desired = std::min(token.getConfig().maxTokens,
                           token.getAvailable() + toAdd);
    token.refill();                                    // reset to max
    int excess = token.getConfig().maxTokens - desired;
    if (excess > 0) token.consume(excess);             // trim back down
}

// ═════════════════════════════════════════════════════════════════
// ContinuousDripRefillStrategy
// Adds fractional tokens continuously — smoother than fixed rate.
//
// Math:
//   tokensPerSecond = refillRate / unitSeconds
//   toAdd           = elapsed * tokensPerSecond   (fractional)
//   new             = min(maxTokens, current + floor(toAdd))
// ═════════════════════════════════════════════════════════════════
bool ContinuousDripRefillStrategy::shouldRefill(
    const IToken& token) const
{
    // Always check — even 1ms elapsed might add a fractional token
    double elapsed         = elapsedSeconds(token);
    double unitSecs        = unitToSeconds(token.getConfig().refillUnit);
    double tokensPerSecond = token.getConfig().refillRate / unitSecs;
    double fractional      = elapsed * tokensPerSecond;
    return fractional >= 1.0; // only refill when at least 1 full token ready
}

int ContinuousDripRefillStrategy::calculateRefillAmount(
    const IToken& token) const
{
    double elapsed         = elapsedSeconds(token);
    double unitSecs        = unitToSeconds(token.getConfig().refillUnit);
    double tokensPerSecond = token.getConfig().refillRate / unitSecs;
    int    toAdd           = static_cast<int>(
                                 std::floor(elapsed * tokensPerSecond));

    int available = token.getAvailable();
    int maxTok    = token.getConfig().maxTokens;
    return std::min(toAdd, maxTok - available);
}

void ContinuousDripRefillStrategy::refill(IToken& token) {
    if (!shouldRefill(token)) return;

    int toAdd = calculateRefillAmount(token);
    if (toAdd <= 0) return;

    int desired = std::min(token.getConfig().maxTokens,
                           token.getAvailable() + toAdd);
    token.refill();
    int excess = token.getConfig().maxTokens - desired;
    if (excess > 0) token.consume(excess);
}

// ═════════════════════════════════════════════════════════════════
// NoRefillStrategy
// Used by Fixed/Sliding Window — window resets externally,
// not via token refill mechanism.
// ═════════════════════════════════════════════════════════════════
bool NoRefillStrategy::shouldRefill(const IToken&) const {
    return false;
}

int NoRefillStrategy::calculateRefillAmount(const IToken&) const {
    return 0;
}

void NoRefillStrategy::refill(IToken&) {
    // Intentionally empty — window algorithms manage their own reset
}

} // namespace RateLimiterNS
