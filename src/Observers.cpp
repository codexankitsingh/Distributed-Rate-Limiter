#include "Observers.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace RateLimiterNS {

// ═════════════════════════════════════════════════════════════════
//  LoggerObserver
// ═════════════════════════════════════════════════════════════════
LoggerObserver::LoggerObserver(bool logToFile,
                               const std::string& filename)
    : logToFile_(logToFile)
{
    if (logToFile_) {
        fileStream_.open(filename, std::ios::app);
        if (!fileStream_.is_open()) {
            std::cerr << "[LoggerObserver] WARNING: "
                      << "Could not open log file: "
                      << filename << "\n";
            logToFile_ = false;
        }
    }
}

LoggerObserver::~LoggerObserver() {
    if (fileStream_.is_open())
        fileStream_.close();
}

void LoggerObserver::onEvent(const RateLimitEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string msg = formatEvent(event);
    std::cout << msg;
    if (logToFile_ && fileStream_.is_open())
        fileStream_ << msg;
}

std::string LoggerObserver::getName() const {
    return "LoggerObserver";
}

std::string LoggerObserver::formatEvent(
    const RateLimitEvent& event) const
{
    std::ostringstream oss;
    std::string icon = (event.status == Status::ALLOWED) ? "✅" : "❌";

    oss << "[" << getCurrentTimestamp() << "] "
        << icon << " "
        << "[" << toString(event.status)         << "] "
        << "client="    << event.clientId         << " "
        << "algo="      << toString(event.algorithm) << " "
        << "tier="      << toString(event.tier)   << " "
        << "remaining=" << event.remaining
        << "/"          << event.limit;

    if (event.status == Status::DENIED && event.retryAfterMs > 0) {
        oss << " retry_after=" << event.retryAfterMs << "ms";
    }

    oss << "\n";
    return oss.str();
}

std::string LoggerObserver::getCurrentTimestamp() const {
    // Get current time as time_t from system_clock
    auto now      = std::chrono::system_clock::now();
    auto now_t    = std::chrono::system_clock::to_time_t(now);

    // Format: YYYY-MM-DD HH:MM:SS
    std::ostringstream oss;
    // Use localtime (thread-safe version where available)
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &now_t);
#else
    localtime_r(&now_t, &tm_buf);
#endif
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// ═════════════════════════════════════════════════════════════════
//  MetricsObserver
// ═════════════════════════════════════════════════════════════════
void MetricsObserver::onEvent(const RateLimitEvent& event) {
    // Update atomic global counters (lock-free)
    if (event.status == Status::ALLOWED) {
        ++totalAllowed_;
    } else if (event.status == Status::DENIED) {
        ++totalDenied_;
    }

    // Update per-client and per-algorithm maps (needs lock)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (event.status == Status::ALLOWED) {
            perClientAllowed_[event.clientId]++;
        } else if (event.status == Status::DENIED) {
            perClientDenied_[event.clientId]++;
            perAlgorithmDenied_[toString(event.algorithm)]++;
        }
    }
}

std::string MetricsObserver::getName() const {
    return "MetricsObserver";
}

long long MetricsObserver::getTotalAllowed() const {
    return totalAllowed_.load();
}

long long MetricsObserver::getTotalDenied() const {
    return totalDenied_.load();
}

long long MetricsObserver::getDeniedForClient(
    const std::string& id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = perClientDenied_.find(id);
    return (it != perClientDenied_.end()) ? it->second : 0LL;
}

long long MetricsObserver::getAllowedForClient(
    const std::string& id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = perClientAllowed_.find(id);
    return (it != perClientAllowed_.end()) ? it->second : 0LL;
}

void MetricsObserver::printSummary() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════╗\n";
    std::cout << "║         RATE LIMITER METRICS             ║\n";
    std::cout << "╠══════════════════════════════════════════╣\n";
    std::cout << "║  Total Allowed : "
              << std::setw(24) << std::left
              << totalAllowed_.load() << "║\n";
    std::cout << "║  Total Denied  : "
              << std::setw(24) << std::left
              << totalDenied_.load()  << "║\n";
    std::cout << "╠══════════════════════════════════════════╣\n";
    std::cout << "║  Per-Client Breakdown                    ║\n";
    std::cout << "╠══════════════════════════════════════════╣\n";

    // Collect all unique client IDs
    std::vector<std::string> allClients;
    for (auto& [id, _] : perClientAllowed_) allClients.push_back(id);
    for (auto& [id, _] : perClientDenied_) {
        if (std::find(allClients.begin(),
                      allClients.end(), id) == allClients.end())
            allClients.push_back(id);
    }

    for (const auto& id : allClients) {
        long long allowed = 0, denied = 0;
        auto itA = perClientAllowed_.find(id);
        auto itD = perClientDenied_.find(id);
        if (itA != perClientAllowed_.end()) allowed = itA->second;
        if (itD != perClientDenied_.end())  denied  = itD->second;

        std::ostringstream row;
        row << "  " << id
            << " → ✅" << allowed
            << " ❌"   << denied;
        std::cout << "║  " << std::setw(40) << std::left
                  << row.str() << "║\n";
    }

    std::cout << "╠══════════════════════════════════════════╣\n";
    std::cout << "║  Per-Algorithm Denials                   ║\n";
    std::cout << "╠══════════════════════════════════════════╣\n";

    for (auto& [algo, count] : perAlgorithmDenied_) {
        std::ostringstream row;
        row << "  " << algo << " → ❌" << count;
        std::cout << "║  " << std::setw(40) << std::left
                  << row.str() << "║\n";
    }

    std::cout << "╚══════════════════════════════════════════╝\n";
    std::cout << "\n";
}

void MetricsObserver::reset() {
    totalAllowed_.store(0);
    totalDenied_.store(0);
    std::lock_guard<std::mutex> lock(mutex_);
    perClientAllowed_.clear();
    perClientDenied_.clear();
    perAlgorithmDenied_.clear();
}

// ═════════════════════════════════════════════════════════════════
//  AlertObserver
// ═════════════════════════════════════════════════════════════════
AlertObserver::AlertObserver(int denialThreshold, int windowSeconds)
    : denialThreshold_(denialThreshold)
    , windowSeconds_(windowSeconds)
{}

void AlertObserver::onEvent(const RateLimitEvent& event) {
    // Only track DENIED events
    if (event.status != Status::DENIED) return;

    std::lock_guard<std::mutex> lock(mutex_);

    auto& state = states_[event.clientId];
    auto  now   = std::chrono::system_clock::now();

    // Record this denial timestamp
    state.denialTimes.push_back(now);

    // Evict timestamps outside the sliding window
    auto cutoff = now - std::chrono::seconds(windowSeconds_);
    state.denialTimes.erase(
        std::remove_if(
            state.denialTimes.begin(),
            state.denialTimes.end(),
            [&cutoff](const std::chrono::system_clock::time_point& t) {
                return t < cutoff;
            }),
        state.denialTimes.end()
    );

    // Check if threshold exceeded
    checkAndFireAlert(event.clientId);
}

std::string AlertObserver::getName() const {
    return "AlertObserver";
}

void AlertObserver::checkAndFireAlert(const std::string& clientId) {
    // Called with mutex already held
    auto& state = states_[clientId];
    int   count = static_cast<int>(state.denialTimes.size());

    if (count >= denialThreshold_) {
        sendAlert(clientId, count);
        // Clear after firing to avoid repeated alerts
        state.denialTimes.clear();
    }
}

void AlertObserver::sendAlert(const std::string& clientId, int count) {
    // In production: send to PagerDuty / Slack / SNS
    // For now: print a prominent console alert
    std::cout << "\n";
    std::cout << "🚨 ╔══════════════════════════════════════╗\n";
    std::cout << "🚨 ║          RATE LIMIT ALERT            ║\n";
    std::cout << "🚨 ╠══════════════════════════════════════╣\n";
    std::cout << "🚨 ║  Client   : "
              << std::setw(27) << std::left << clientId << "║\n";
    std::cout << "🚨 ║  Denials  : "
              << std::setw(27) << std::left << count    << "║\n";
    std::cout << "🚨 ║  Window   : "
              << std::setw(24) << std::left
              << (std::to_string(windowSeconds_) + "s") << "║\n";
    std::cout << "🚨 ║  Action   : "
              << std::setw(27) << std::left
              << "Investigate / Block"                  << "║\n";
    std::cout << "🚨 ╚══════════════════════════════════════╝\n";
    std::cout << "\n";
}

} // namespace RateLimiterNS
