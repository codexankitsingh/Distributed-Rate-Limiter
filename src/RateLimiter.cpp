#include "RateLimiter.hpp"
#include "Observers.hpp"
#include <stdexcept>
#include <iostream>

namespace RateLimiterNS {

// ─────────────────────────────────────────────────────────────────
// Constructor
// Wires up factory, default algorithm, and optional observers
// ─────────────────────────────────────────────────────────────────
RateLimiter::RateLimiter(
    std::shared_ptr<ITokenFactory>       factory,
    std::shared_ptr<IRateLimitAlgorithm> defaultAlgorithm,
    const RateLimiterConfig&             config)
    : factory_(std::move(factory))
    , defaultAlgorithm_(std::move(defaultAlgorithm))
    , config_(config)
{
    if (!factory_)
        throw std::invalid_argument(
            "RateLimiter: factory cannot be null");

    if (!defaultAlgorithm_)
        throw std::invalid_argument(
            "RateLimiter: defaultAlgorithm cannot be null");

    // Auto-attach built-in observers based on config
    if (config_.enableLogging)
        addObserver(std::make_shared<LoggerObserver>());

    if (config_.enableMetrics)
        addObserver(std::make_shared<MetricsObserver>());

    if (config_.enableAlerts)
        addObserver(std::make_shared<AlertObserver>(
            config_.alertThreshold));
}

// ─────────────────────────────────────────────────────────────────
// registerClient (explicit config)
// ─────────────────────────────────────────────────────────────────
void RateLimiter::registerClient(const TokenConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (clients_.count(config.clientId)) {
        // Already registered — silently skip
        return;
    }

    ClientEntry entry;
    entry.token     = factory_->createToken(config);
    entry.algorithm = nullptr; // use defaultAlgorithm_

    clients_.emplace(config.clientId, std::move(entry));
}

// ─────────────────────────────────────────────────────────────────
// registerClient (by tier)
// ─────────────────────────────────────────────────────────────────
void RateLimiter::registerClient(const std::string& clientId,
                                  Tier               tier,
                                  Algorithm          algo)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (clients_.count(clientId)) return;

    ClientEntry entry;
    entry.token     = factory_->createTokenForTier(clientId, tier, algo);
    entry.algorithm = nullptr; // use defaultAlgorithm_

    clients_.emplace(clientId, std::move(entry));
}

// ─────────────────────────────────────────────────────────────────
// allowRequest — THE core method
//
// Flow:
//   1. Find or auto-register client
//   2. Pick algorithm (per-client override or default)
//   3. Call algorithm.checkAndConsume()
//   4. Notify all observers
//   5. Return result
// ─────────────────────────────────────────────────────────────────
RateLimitResult RateLimiter::allowRequest(const std::string& clientId,
                                           int                requestCost)
{
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        // Step 1: Auto-register unknown clients as FREE tier
        if (!clients_.count(clientId)) {
            autoRegister(clientId);
        }

        auto& entry = clients_.at(clientId);

        // Step 2: Pick algorithm
        auto& algo = entry.algorithm
                         ? entry.algorithm
                         : defaultAlgorithm_;

        // Step 3: Check and consume
        RateLimitResult result =
            algo->checkAndConsume(*entry.token, requestCost);

        // Step 4: Notify observers
        RateLimitEvent event(
            clientId,
            result.status,
            result.algorithm,
            entry.token->getTier(),
            result.remaining,
            result.limit,
            result.retryAfterMs
        );
        notifyObservers(event);

        return result;

    } catch (const std::exception& ex) {
        std::cerr << "[RateLimiter] ERROR for client "
                  << clientId << ": " << ex.what() << "\n";
        return handleError(clientId);
    }
}

// ─────────────────────────────────────────────────────────────────
// setAlgorithmForClient
// Override the default algorithm for one specific client
// ─────────────────────────────────────────────────────────────────
void RateLimiter::setAlgorithmForClient(
    const std::string&                   clientId,
    std::shared_ptr<IRateLimitAlgorithm> algorithm)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!clients_.count(clientId))
        throw std::runtime_error(
            "RateLimiter::setAlgorithmForClient: "
            "client not registered: " + clientId);

    clients_.at(clientId).algorithm = std::move(algorithm);
}

// ─────────────────────────────────────────────────────────────────
// Observer management (IObservable)
// ─────────────────────────────────────────────────────────────────
void RateLimiter::addObserver(
    std::shared_ptr<IObserver> observer)
{
    if (!observer) return;
    // Prevent duplicate observers by name
    for (auto& obs : observers_) {
        if (obs->getName() == observer->getName()) return;
    }
    observers_.push_back(std::move(observer));
}

void RateLimiter::removeObserver(const std::string& name) {
    observers_.erase(
        std::remove_if(
            observers_.begin(),
            observers_.end(),
            [&name](const std::shared_ptr<IObserver>& obs) {
                return obs->getName() == name;
            }),
        observers_.end()
    );
}

void RateLimiter::notifyObservers(const RateLimitEvent& event) {
    // Called with mutex held — observers must not call back into
    // RateLimiter or deadlock will occur
    for (auto& observer : observers_) {
        try {
            observer->onEvent(event);
        } catch (const std::exception& ex) {
            std::cerr << "[RateLimiter] Observer '"
                      << observer->getName()
                      << "' threw: " << ex.what() << "\n";
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// Admin / Monitoring
// ─────────────────────────────────────────────────────────────────
void RateLimiter::resetClient(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = clients_.find(clientId);
    if (it == clients_.end()) return;

    auto& algo = it->second.algorithm
                     ? it->second.algorithm
                     : defaultAlgorithm_;
    algo->reset(*it->second.token);
}

void RateLimiter::resetAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, entry] : clients_) {
        auto& algo = entry.algorithm
                         ? entry.algorithm
                         : defaultAlgorithm_;
        algo->reset(*entry.token);
    }
}

int RateLimiter::getAvailableTokens(
    const std::string& clientId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = clients_.find(clientId);
    if (it == clients_.end()) return -1;
    return it->second.token->getAvailable();
}

bool RateLimiter::isClientRegistered(
    const std::string& clientId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return clients_.count(clientId) > 0;
}

// ─────────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────────
void RateLimiter::autoRegister(const std::string& clientId) {
    // Called with mutex already held
    ClientEntry entry;
    entry.token = factory_->createTokenForTier(
        clientId, Tier::FREE, Algorithm::TOKEN_BUCKET);
    entry.algorithm = nullptr;
    clients_.emplace(clientId, std::move(entry));
}

RateLimitResult RateLimiter::handleError(
    const std::string& clientId)
{
    if (config_.failOpen) {
        // Fail-open: allow request through when system errors
        return RateLimitResult{
            Status::ALLOWED, 0, 0, 0, clientId,
            defaultAlgorithm_->getType()
        };
    }
    // Fail-closed: deny request when system errors
    return RateLimitResult{
        Status::ERROR, 0, 0, 0, clientId,
        defaultAlgorithm_->getType()
    };
}

} // namespace RateLimiterNS
