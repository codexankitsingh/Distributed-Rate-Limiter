/**
 * k6 Load Test — Distributed Rate Limiter
 *
 * Validates throughput limits under high concurrency:
 *   • Stage 1: Ramp-up to 50 VUs over 30s
 *   • Stage 2: Sustained 50 VUs for 1 minute
 *   • Stage 3: Spike to 100 VUs for 30s
 *   • Stage 4: Cool-down over 10s
 *
 * Each VU sends requests with a unique X-Client-ID header so the
 * rate limiter tracks them independently.
 *
 * Run:
 *   k6 run k6_test.js
 *   (or from Docker: docker run --rm -i --network host grafana/k6 run - < k6_test.js)
 */

import http from "k6/http";
import { check, sleep } from "k6";
import { Counter, Rate, Trend } from "k6/metrics";

// ── Custom metrics ──────────────────────────────────────────────────
const rateLimited = new Counter("rate_limited_requests");
const allowedRate = new Rate("allowed_rate");
const latency = new Trend("request_latency", true);

// ── Test configuration ──────────────────────────────────────────────
export const options = {
  stages: [
    { duration: "30s", target: 50 },   // ramp-up
    { duration: "1m", target: 50 },    // sustained load
    { duration: "30s", target: 100 },  // spike
    { duration: "10s", target: 0 },    // cool-down
  ],
  thresholds: {
    http_req_duration: ["p(95)<200", "p(99)<500"],
    rate_limited_requests: ["count>0"],  // we WANT to see 429s
  },
};

const BASE_URL = __ENV.BASE_URL || "http://localhost:8000";

// ── Scenario 1: Per-client rate limiting ─────────────────────────────
export default function () {
  const clientId = `k6_user_${__VU}`;

  // Hit the protected resource
  const res = http.get(`${BASE_URL}/resource`, {
    headers: {
      "X-Client-ID": clientId,
      "Content-Type": "application/json",
    },
    tags: { endpoint: "resource" },
  });

  latency.add(res.timings.duration);

  const isAllowed = res.status === 200;
  const isRateLimited = res.status === 429;

  check(res, {
    "status is 200 or 429": (r) => r.status === 200 || r.status === 429,
    "has rate-limit header": (r) => r.headers["X-Ratelimit-Limit"] !== undefined,
  });

  if (isRateLimited) {
    rateLimited.add(1);
    allowedRate.add(0);

    // Verify 429 body structure
    check(res, {
      "429 body has error field": (r) => {
        const body = JSON.parse(r.body);
        return body.error === "rate_limited";
      },
      "429 body has retry_after": (r) => {
        const body = JSON.parse(r.body);
        return body.retry_after_seconds > 0;
      },
    });
  } else {
    allowedRate.add(1);
  }

  sleep(0.1); // 100ms between requests per VU
}

// ── Scenario 2: Burst test (separate function) ──────────────────────
export function burstTest() {
  const clientId = "k6_burst_client";

  // Fire 20 requests as fast as possible
  for (let i = 0; i < 20; i++) {
    const res = http.get(`${BASE_URL}/resource`, {
      headers: {
        "X-Client-ID": clientId,
        "Content-Type": "application/json",
      },
      tags: { endpoint: "burst" },
    });

    check(res, {
      "burst: valid status": (r) => r.status === 200 || r.status === 429,
    });
  }
}
