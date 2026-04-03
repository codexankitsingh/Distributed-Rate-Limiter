#pragma once
#include "IObserver.hpp"
#include <fstream>
#include <unordered_map>
#include <atomic>
#include <mutex>

namespace RateLimiterNS {

// ─── Observer 1: Logger ───────────────────────────────────────────
// Writes every event to console and/or log file
// Format: [2026-04-03 12:00:01] [DENIED] client=user_123 algo=TOKEN_BUCKET
class LoggerObserver : public IObserver {
public:
    explicit LoggerObserver(bool logToFile = false,
                            const std::string& filename = "rate_limiter.log");
    ~LoggerObserver() override;

    void        onEvent(const RateLimitEvent& event) override;
    std::string getName()                      const override;

private:
    bool          logToFile_;
    std::ofstream fileStream_;
    std::mutex    mutex_;

    std::string formatEvent(const RateLimitEvent& event) const;
    std::string getCurrentTimestamp()                    const;
};

// ─── Observer 2: MetricsCollector ────────────────────────────────
// Tracks counters: total allowed, denied, per-client, per-algorithm
// Exposes data for Prometheus scraping later
class MetricsObserver : public IObserver {
public:
    void        onEvent(const RateLimitEvent& event) override;
    std::string getName()                      const override;

    // ── Query methods ──
    long long getTotalAllowed()                          const;
    long long getTotalDenied()                           const;
    long long getDeniedForClient(const std::string& id) const;
    long long getAllowedForClient(const std::string& id) const;
    void      printSummary()                             const;
    void      reset();

private:
    std::atomic<long long>                        totalAllowed_{0};
    std::atomic<long long>                        totalDenied_{0};
    std::unordered_map<std::string, long long>    perClientAllowed_;
    std::unordered_map<std::string, long long>    perClientDenied_;
    std::unordered_map<std::string, long long>    perAlgorithmDenied_;
    mutable std::mutex                            mutex_;
};

// ─── Observer 3: AlertObserver ────────────────────────────────────
// Fires alert when denial rate exceeds threshold
// e.g., "user_123 has been denied 50 times in last 60 seconds"
class AlertObserver : public IObserver {
public:
    explicit AlertObserver(int denialThreshold = 10,
                           int windowSeconds   = 60);

    void        onEvent(const RateLimitEvent& event) override;
    std::string getName()                      const override;

private:
    int denialThreshold_;
    int windowSeconds_;

    struct AlertState {
        std::vector<std::chrono::system_clock::time_point> denialTimes;
    };
    std::unordered_map<std::string, AlertState> states_;
    mutable std::mutex                          mutex_;

    void checkAndFireAlert(const std::string& clientId);
    void sendAlert(const std::string& clientId, int count); // extensible
};

} // namespace RateLimiterNS
