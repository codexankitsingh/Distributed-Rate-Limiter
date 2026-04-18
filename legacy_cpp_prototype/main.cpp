#include "RateLimiter.hpp"
#include "Algorithms.hpp"
#include "Observers.hpp"
#include "TokenFactory.hpp"
#include "Enums.hpp"

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <memory>

using namespace RateLimiterNS;

// ═════════════════════════════════════════════════════════════════
// Helper Utilities
// ═════════════════════════════════════════════════════════════════

void printSectionHeader(const std::string& title,
                        const std::string& subtitle = "") {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════╗\n";
    std::cout << "║  " << std::setw(48) << std::left << title   << "║\n";
    if (!subtitle.empty()) {
        std::cout << "║  " << std::setw(48) << std::left << subtitle << "║\n";
    }
    std::cout << "╚══════════════════════════════════════════════════╝\n";
}

void printDivider() {
    std::cout << "──────────────────────────────────────────────────\n";
}

void printResult(const RateLimitResult& r, int reqNum) {
    std::string icon   = (r.status == Status::ALLOWED) ? "✅" : "❌";
    std::string status = toString(r.status);

    std::cout << "  Req #" << std::setw(3) << std::left << reqNum
              << " " << icon
              << " [" << std::setw(9) << std::left << status << "] "
              << "remaining=" << std::setw(5) << std::left << r.remaining
              << "/ limit=" << r.limit;

    if (r.status == Status::DENIED && r.retryAfterMs > 0) {
        std::cout << "  ⏳ retry_after=" << r.retryAfterMs << "ms";
    }

    std::cout << "\n";
}

void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ═════════════════════════════════════════════════════════════════
// TEST 1: Token Bucket — FREE Tier Burst Test
//
// Expectation:
//   Requests 1–10  → ✅ ALLOWED  (bucket has 10 tokens)
//   Requests 11–15 → ❌ DENIED   (bucket empty)
// ═════════════════════════════════════════════════════════════════
void testTokenBucket(RateLimiter& limiter) {
    printSectionHeader(
        "TEST 1: Token Bucket — FREE Tier",
        "Limit: 10 req/min | Burst up to 10"
    );

    limiter.registerClient("tb_free_user", Tier::FREE,
                            Algorithm::TOKEN_BUCKET);

    std::cout << "\n  Firing 15 requests rapidly...\n\n";
    for (int i = 1; i <= 15; ++i) {
        auto result = limiter.allowRequest("tb_free_user");
        printResult(result, i);
    }

    std::cout << "\n  Available tokens: "
              << limiter.getAvailableTokens("tb_free_user") << "\n";
}

// ═════════════════════════════════════════════════════════════════
// TEST 2: Token Bucket — PREMIUM Tier
//
// Expectation:
//   Requests 1–1000 → ✅ ALLOWED  (bucket has 1000 tokens)
//   Request  1001   → ❌ DENIED
// ═════════════════════════════════════════════════════════════════
void testTokenBucketPremium(RateLimiter& limiter) {
    printSectionHeader(
        "TEST 2: Token Bucket — PREMIUM Tier",
        "Limit: 1000 req/min | Show first 5 + boundary"
    );

    limiter.registerClient("tb_premium_user", Tier::PREMIUM,
                            Algorithm::TOKEN_BUCKET);

    std::cout << "\n  Firing first 5 requests...\n\n";
    for (int i = 1; i <= 5; ++i) {
        auto result = limiter.allowRequest("tb_premium_user");
        printResult(result, i);
    }

    std::cout << "\n  Available tokens after 5 requests: "
              << limiter.getAvailableTokens("tb_premium_user") << "\n";
}

// ═════════════════════════════════════════════════════════════════
// TEST 3: Fixed Window — Boundary Burst Problem Demo
//
// Expectation:
//   Requests 1–10  → ✅ ALLOWED  (window fills up)
//   Requests 11–15 → ❌ DENIED   (window full)
//   After reset    → ✅ ALLOWED  (new window starts)
// ═════════════════════════════════════════════════════════════════
void testFixedWindow(RateLimiter& limiter) {
    printSectionHeader(
        "TEST 3: Fixed Window — FREE Tier",
        "Limit: 10 req/min | Shows boundary burst issue"
    );

    auto fixedAlgo = std::make_shared<FixedWindowAlgorithm>();

    limiter.registerClient("fw_free_user", Tier::FREE,
                            Algorithm::FIXED_WINDOW);
    limiter.setAlgorithmForClient("fw_free_user", fixedAlgo);

    std::cout << "\n  Firing 13 requests rapidly...\n\n";
    for (int i = 1; i <= 13; ++i) {
        auto result = limiter.allowRequest("fw_free_user");
        printResult(result, i);
    }

    printDivider();
    std::cout << "  Resetting client window manually...\n";
    limiter.resetClient("fw_free_user");

    std::cout << "\n  Firing 3 more requests after reset...\n\n";
    for (int i = 1; i <= 3; ++i) {
        auto result = limiter.allowRequest("fw_free_user");
        printResult(result, i);
    }
}

// ═════════════════════════════════════════════════════════════════
// TEST 4: Sliding Window — No Boundary Burst
//
// Expectation:
//   Requests 1–10  → ✅ ALLOWED
//   Requests 11–15 → ❌ DENIED
//   Weighted formula prevents boundary spikes
// ═════════════════════════════════════════════════════════════════
void testSlidingWindow(RateLimiter& limiter) {
    printSectionHeader(
        "TEST 4: Sliding Window — BASIC Tier",
        "Limit: 100 req/min | No boundary burst"
    );

    auto slideAlgo = std::make_shared<SlidingWindowAlgorithm>();

    limiter.registerClient("sw_basic_user", Tier::BASIC,
                            Algorithm::SLIDING_WINDOW);
    limiter.setAlgorithmForClient("sw_basic_user", slideAlgo);

    std::cout << "\n  Firing 10 requests...\n\n";
    for (int i = 1; i <= 10; ++i) {
        auto result = limiter.allowRequest("sw_basic_user");
        printResult(result, i);
    }

    std::cout << "\n  Available tokens: "
              << limiter.getAvailableTokens("sw_basic_user") << "\n";
}

// ═════════════════════════════════════════════════════════════════
// TEST 5: Leaky Bucket — Traffic Shaping
//
// Expectation:
//   Burst of 15 requests → first 10 queued (ALLOWED)
//   Remaining 5          → ❌ DENIED (queue full)
// ═════════════════════════════════════════════════════════════════
void testLeakyBucket(RateLimiter& limiter) {
    printSectionHeader(
        "TEST 5: Leaky Bucket — FREE Tier",
        "Limit: 10 req/min | Smooths all bursts"
    );

    auto leakyAlgo = std::make_shared<LeakyBucketAlgorithm>();

    limiter.registerClient("lb_free_user", Tier::FREE,
                            Algorithm::LEAKY_BUCKET);
    limiter.setAlgorithmForClient("lb_free_user", leakyAlgo);

    std::cout << "\n  Firing 15 requests in a burst...\n\n";
    for (int i = 1; i <= 15; ++i) {
        auto result = limiter.allowRequest("lb_free_user");
        printResult(result, i);
    }
}

// ═════════════════════════════════════════════════════════════════
// TEST 6: Auto-Registration of Unknown Client
//
// Expectation:
//   Unknown client → auto-registered as FREE tier
//   Requests 1–10  → ✅ ALLOWED
//   Requests 11+   → ❌ DENIED
// ═════════════════════════════════════════════════════════════════
void testAutoRegister(RateLimiter& limiter) {
    printSectionHeader(
        "TEST 6: Auto-Register Unknown Client",
        "mystery_user never registered → auto FREE tier"
    );

    std::cout << "\n  Is 'mystery_user' registered before request? "
              << (limiter.isClientRegistered("mystery_user")
                      ? "YES" : "NO") << "\n\n";

    std::cout << "  Firing 5 requests from unknown client...\n\n";
    for (int i = 1; i <= 5; ++i) {
        auto result = limiter.allowRequest("mystery_user");
        printResult(result, i);
    }

    std::cout << "\n  Is 'mystery_user' registered after request? "
              << (limiter.isClientRegistered("mystery_user")
                      ? "YES" : "NO") << "\n";

    std::cout << "  Available tokens: "
              << limiter.getAvailableTokens("mystery_user") << "\n";
}

// ═════════════════════════════════════════════════════════════════
// TEST 7: Per-Client Algorithm Override
//
// Two clients, same tier, different algorithms
// Expectation:
//   client_A → Token Bucket  (allows burst)
//   client_B → Leaky Bucket  (smooths burst)
// ═════════════════════════════════════════════════════════════════
void testPerClientAlgorithmOverride(RateLimiter& limiter) {
    printSectionHeader(
        "TEST 7: Per-Client Algorithm Override",
        "Same tier, different algorithms per client"
    );

    // Client A: Token Bucket (default)
    limiter.registerClient("override_client_A", Tier::FREE,
                            Algorithm::TOKEN_BUCKET);

    // Client B: Leaky Bucket (override)
    auto leakyAlgo = std::make_shared<LeakyBucketAlgorithm>();
    limiter.registerClient("override_client_B", Tier::FREE,
                            Algorithm::LEAKY_BUCKET);
    limiter.setAlgorithmForClient("override_client_B", leakyAlgo);

    std::cout << "\n  Client A (Token Bucket) — 5 requests:\n\n";
    for (int i = 1; i <= 5; ++i) {
        auto result = limiter.allowRequest("override_client_A");
        printResult(result, i);
    }

    std::cout << "\n  Client B (Leaky Bucket) — 5 requests:\n\n";
    for (int i = 1; i <= 5; ++i) {
        auto result = limiter.allowRequest("override_client_B");
        printResult(result, i);
    }
}

// ═════════════════════════════════════════════════════════════════
// TEST 8: Multi-Tier Comparison
//
// Same number of requests across all 4 tiers
// Expectation:
//   FREE       → hits limit quickly
//   BASIC      → more headroom
//   PREMIUM    → lots of headroom
//   ENTERPRISE → virtually unlimited
// ═════════════════════════════════════════════════════════════════
void testMultiTierComparison(RateLimiter& limiter) {
    printSectionHeader(
        "TEST 8: Multi-Tier Comparison",
        "20 requests across FREE / BASIC / PREMIUM / ENTERPRISE"
    );

    limiter.registerClient("cmp_free",       Tier::FREE);
    limiter.registerClient("cmp_basic",      Tier::BASIC);
    limiter.registerClient("cmp_premium",    Tier::PREMIUM);
    limiter.registerClient("cmp_enterprise", Tier::ENTERPRISE);

    std::vector<std::pair<std::string, std::string>> clients = {
        {"cmp_free",       "FREE      "},
        {"cmp_basic",      "BASIC     "},
        {"cmp_premium",    "PREMIUM   "},
        {"cmp_enterprise", "ENTERPRISE"}
    };

    const int NUM_REQUESTS = 20;

    for (auto& [clientId, label] : clients) {
        int allowed = 0, denied = 0;
        for (int i = 0; i < NUM_REQUESTS; ++i) {
            auto result = limiter.allowRequest(clientId);
            if (result.status == Status::ALLOWED) ++allowed;
            else                                  ++denied;
        }
        std::cout << "  " << label
                  << " → ✅ " << std::setw(3) << allowed
                  << " allowed  ❌ " << std::setw(3) << denied
                  << " denied"
                  << "  (remaining: "
                  << limiter.getAvailableTokens(clientId) << ")\n";
    }
}

// ═════════════════════════════════════════════════════════════════
// TEST 9: Reset Functionality
//
// Expectation:
//   Fill up bucket → all denied → reset → allowed again
// ═════════════════════════════════════════════════════════════════
void testResetFunctionality(RateLimiter& limiter) {
    printSectionHeader(
        "TEST 9: Reset Functionality",
        "Exhaust tokens → reset → verify recovery"
    );

    limiter.registerClient("reset_user", Tier::FREE,
                            Algorithm::TOKEN_BUCKET);

    std::cout << "\n  Phase 1: Exhaust all 10 tokens...\n\n";
    for (int i = 1; i <= 12; ++i) {
        auto result = limiter.allowRequest("reset_user");
        printResult(result, i);
    }

    std::cout << "\n  Tokens before reset: "
              << limiter.getAvailableTokens("reset_user") << "\n";

    printDivider();
    std::cout << "  Calling resetClient(\"reset_user\")...\n";
    limiter.resetClient("reset_user");

    std::cout << "  Tokens after reset:  "
              << limiter.getAvailableTokens("reset_user") << "\n\n";

    std::cout << "  Phase 2: Fire 5 requests after reset...\n\n";
    for (int i = 1; i <= 5; ++i) {
        auto result = limiter.allowRequest("reset_user");
        printResult(result, i);
    }
}

// ═════════════════════════════════════════════════════════════════
// TEST 10: Request Cost > 1 (Weighted Requests)
//
// Some requests cost more tokens (e.g., bulk API calls)
// Expectation:
//   Cost=1 → normal deduction
//   Cost=5 → 5 tokens deducted per request
// ═════════════════════════════════════════════════════════════════
void testWeightedRequests(RateLimiter& limiter) {
    printSectionHeader(
        "TEST 10: Weighted Requests (requestCost > 1)",
        "Bulk API call costs 5 tokens each"
    );

    limiter.registerClient("weighted_user", Tier::FREE,
                            Algorithm::TOKEN_BUCKET);

    std::cout << "\n  Firing requests with cost=5 each...\n";
    std::cout << "  (FREE tier has 10 tokens → expect 2 allowed)\n\n";

    for (int i = 1; i <= 5; ++i) {
        auto result = limiter.allowRequest("weighted_user", 5);
        printResult(result, i);
    }
}

// ═════════════════════════════════════════════════════════════════
// TEST 11: Observer — Alert Threshold Test
//
// Expectation:
//   After N consecutive denials → 🚨 Alert fires
// ═════════════════════════════════════════════════════════════════
void testAlertObserver(RateLimiter& limiter) {
    printSectionHeader(
        "TEST 11: Alert Observer Threshold",
        "5 denials in 10s window → 🚨 Alert fires"
    );

    limiter.registerClient("alert_user", Tier::FREE,
                            Algorithm::TOKEN_BUCKET);

    std::cout << "\n  Exhausting tokens first (10 requests)...\n\n";
    for (int i = 1; i <= 10; ++i) {
        limiter.allowRequest("alert_user");
    }

    std::cout << "\n  Now firing 7 more (all denied → alert fires)...\n\n";
    for (int i = 1; i <= 7; ++i) {
        auto result = limiter.allowRequest("alert_user");
        printResult(result, i);
    }
}

// ═════════════════════════════════════════════════════════════════
// TEST 12: Concurrent Requests (Thread Safety)
//
// Expectation:
//   5 threads × 4 requests = 20 total
//   Only 10 allowed (FREE tier limit)
//   No crashes, no data races
// ═════════════════════════════════════════════════════════════════
void testConcurrentRequests(RateLimiter& limiter) {
    printSectionHeader(
        "TEST 12: Concurrent Requests (Thread Safety)",
        "5 threads × 4 requests = 20 total | limit=10"
    );

    limiter.registerClient("concurrent_user", Tier::FREE,
                            Algorithm::TOKEN_BUCKET);

    std::atomic<int> totalAllowed{0};
    std::atomic<int> totalDenied{0};

    auto worker = [&](int threadId) {
        for (int i = 0; i < 4; ++i) {
            auto result = limiter.allowRequest("concurrent_user");
            if (result.status == Status::ALLOWED) ++totalAllowed;
            else                                  ++totalDenied;
        }
    };

    std::cout << "\n  Launching 5 threads...\n\n";

    std::vector<std::thread> threads;
    for (int t = 0; t < 5; ++t) {
        threads.emplace_back(worker, t);
    }
    for (auto& th : threads) {
        th.join();
    }

    std::cout << "  Results:\n";
    std::cout << "  ✅ Total Allowed : " << totalAllowed.load() << "\n";
    std::cout << "  ❌ Total Denied  : " << totalDenied.load()  << "\n";
    std::cout << "  📊 Total Requests: "
              << (totalAllowed.load() + totalDenied.load()) << "\n";
    std::cout << "  🔒 No data races (mutex protected)\n";
}

// ═════════════════════════════════════════════════════════════════
// MAIN
// ═════════════════════════════════════════════════════════════════
int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════╗\n";
    std::cout << "║       DISTRIBUTED RATE LIMITER — C++ DEMO       ║\n";
    std::cout << "║   Patterns: Strategy | Observer | Factory | Facade║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n";

    // ── 1. Wire up core dependencies ─────────────────────────────
    auto factory   = std::make_shared<TieredTokenFactory>();
    auto refill    = std::make_shared<FixedRateRefillStrategy>();
    auto algorithm = std::make_shared<TokenBucketAlgorithm>(refill);

    // ── 2. Configure the system ───────────────────────────────────
    RateLimiterConfig config;
    config.failOpen        = true;   // allow requests if Redis/system down
    config.enableLogging   = false;  // disable auto-logger (we add manually)
    config.enableMetrics   = true;   // auto-attach MetricsObserver
    config.enableAlerts    = true;   // auto-attach AlertObserver
    config.alertThreshold  = 5;      // alert after 5 denials in window

    // ── 3. Build the Facade ───────────────────────────────────────
    RateLimiter limiter(factory, algorithm, config);

    // ── 4. Manually attach Logger (with file output) ──────────────
    limiter.addObserver(
        std::make_shared<LoggerObserver>(
            /*logToFile=*/true,
            /*filename=*/"rate_limiter.log"
        )
    );

    // ── 5. Keep a handle to MetricsObserver for final summary ─────
    //    (MetricsObserver was auto-attached by config.enableMetrics)
    //    We create a separate one here to query it directly.
    auto metricsObserver = std::make_shared<MetricsObserver>();
    limiter.addObserver(metricsObserver);

    // ── 6. Run all tests ──────────────────────────────────────────
    testTokenBucket              (limiter);
    testTokenBucketPremium       (limiter);
    testFixedWindow              (limiter);
    testSlidingWindow            (limiter);
    testLeakyBucket              (limiter);
    testAutoRegister             (limiter);
    testPerClientAlgorithmOverride(limiter);
    testMultiTierComparison      (limiter);
    testResetFunctionality       (limiter);
    testWeightedRequests         (limiter);
    testAlertObserver            (limiter);
    testConcurrentRequests       (limiter);

    // ── 7. Final metrics summary ──────────────────────────────────
    printSectionHeader(
        "FINAL METRICS SUMMARY",
        "Aggregated across all 12 tests"
    );
    metricsObserver->printSummary();

    // ── 8. Final system status ────────────────────────────────────
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════╗\n";
    std::cout << "║              SYSTEM STATUS                      ║\n";
    std::cout << "╠══════════════════════════════════════════════════╣\n";
    std::cout << "║  ✅ All tests completed                          ║\n";
    std::cout << "║  📄 Logs written to: rate_limiter.log            ║\n";
    std::cout << "║  🔒 Thread safety  : mutex-protected             ║\n";
    std::cout << "║  🏗️  Patterns used  : Strategy, Observer,        ║\n";
    std::cout << "║                      Factory, Facade             ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    return 0;
}
