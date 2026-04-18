#pragma once
#include "IRateLimitAlgorithm.hpp"
#include "IObserver.hpp"
#include "TokenFactory.hpp"
#include "Enums.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace RateLimiterNS {

// ─── Configuration for the entire RateLimiter system ─────────────
struct RateLimiterConfig {
    bool  failOpen       = true;   // Allow requests if system errors
    bool  enableLogging  = true;
    bool  enableMetrics  = true;
    bool  enableAlerts   = true;
    int   alertThreshold = 10;     // Denials before alert fires
};

// ─── Facade: single entry point for all rate limiting ─────────────
class RateLimiter : public IObservable {
public:
    explicit RateLimiter(
        std::shared_ptr<ITokenFactory>       factory,
        std::shared_ptr<IRateLimitAlgorithm> defaultAlgorithm,
        const RateLimiterConfig&             config = {});

    // ── Core API ──────────────────────────────────────────────────

    // Register a client with specific config
    void registerClient(const TokenConfig& config);

    // Register by tier (factory handles config)
    void registerClient(const std::string& clientId,
                        Tier               tier,
                        Algorithm          algo = Algorithm::TOKEN_BUCKET);

    // THE main method: check if request is allowed
    RateLimitResult allowRequest(const std::string& clientId,
                                 int                requestCost = 1);

    // Override algorithm for a specific client
    void setAlgorithmForClient(const std::string&                   clientId,
                               std::shared_ptr<IRateLimitAlgorithm> algorithm);

    // ── Observer management (IObservable) ─────────────────────────
    void addObserver(std::shared_ptr<IObserver> observer)    override;
    void removeObserver(const std::string& name)             override;
    void notifyObservers(const RateLimitEvent& event)        override;

    // ── Admin / Monitoring ────────────────────────────────────────
    void  resetClient(const std::string& clientId);
    void  resetAll();
    int   getAvailableTokens(const std::string& clientId) const;
    bool  isClientRegistered(const std::string& clientId) const;

private:
    // Per-client state
    struct ClientEntry {
        std::unique_ptr<IToken>              token;
        std::shared_ptr<IRateLimitAlgorithm> algorithm; // can override default
    };

    std::unordered_map<std::string, ClientEntry> clients_;
    std::shared_ptr<ITokenFactory>               factory_;
    std::shared_ptr<IRateLimitAlgorithm>         defaultAlgorithm_;
    std::vector<std::shared_ptr<IObserver>>      observers_;
    RateLimiterConfig                            config_;
    mutable std::mutex                           mutex_;

    // Auto-register unknown clients as FREE tier
    void autoRegister(const std::string& clientId);

    RateLimitResult handleError(const std::string& clientId);
};

} // namespace RateLimiterNS
