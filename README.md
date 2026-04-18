# Distributed Rate Limiter

A production-grade, distributed rate limiting service supporting **Token Bucket**, **Sliding Window**, and **Fixed Window** algorithms via **Redis atomic operations** (Lua scripts) for thread-safe, low-latency enforcement at scale.

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                        Client Request                            │
│                     (X-Client-ID header)                         │
└──────────────────────┬───────────────────────────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────────────────────────┐
│              FastAPI Rate Limit Middleware                        │
│  ┌─────────────┐  ┌─────────────────┐  ┌──────────────────┐     │
│  │ Resolve      │→│ Enforce Limit    │→│ Allow / Deny     │     │
│  │ Client + Tier│  │ (Lua on Redis)  │  │ (HTTP 200 / 429) │     │
│  └─────────────┘  └─────────────────┘  └──────────────────┘     │
└──────────────┬───────────────┬───────────────────────────────────┘
               │               │
       ┌───────▼───────┐  ┌───▼────────────────────────────┐
       │    Redis       │  │    Kafka Producer               │
       │  (State Store) │  │  (Breach Alerting)             │
       │  Lua Scripts   │  │  Topic: rate_limit_alerts      │
       └───────────────┘  └───┬────────────────────────────┘
                              │
                     ┌────────▼────────────────────────┐
                     │    Downstream Consumers          │
                     │  (Dashboards, Auto-blockers,     │
                     │   Webhook Notifications)         │
                     └─────────────────────────────────┘
```

## Features

- **3 Rate-Limiting Algorithms** — Token Bucket, Sliding Window, Fixed Window — all implemented as Redis Lua scripts for O(1) atomic execution
- **Per-Client Tier Policies** — FREE (10 req/min), BASIC (100 req/min), PREMIUM (1K req/min), ENTERPRISE (10K req/min)
- **Configurable FastAPI Middleware** — inspects `X-Client-ID` headers, auto-registers unknown clients, supports algorithm overrides per client
- **Kafka Event Hooks** — real-time breach alerting via structured JSON events on a dedicated Kafka topic; non-blocking, fire-and-forget
- **Fail-Open Design** — if Redis/Kafka is temporarily unavailable, traffic is allowed through (configurable)
- **Dockerized** — single `docker-compose up` spins up Redis, Zookeeper, Kafka, and the FastAPI app
- **k6 Load Testing** — included script validates throughput limits with staged ramp-up, sustained load, and spike scenarios

## Tech Stack

| Component     | Technology                     |
|---------------|--------------------------------|
| API Framework | FastAPI + Uvicorn              |
| State Store   | Redis 7 (Lua scripts)         |
| Event System  | Apache Kafka (Confluent)       |
| Containerization | Docker + Docker Compose     |
| Load Testing  | Grafana k6                     |
| Language      | Python 3.11                    |

## Quick Start

```bash
# Clone the repo
git clone https://github.com/codexankitsingh/Distributed-Rate-Limiter.git
cd Distributed-Rate-Limiter

# Start all services
docker-compose up --build

# The API is available at http://localhost:8000
# Swagger docs at http://localhost:8000/docs
```

## API Endpoints

| Method | Path        | Description                          |
|--------|-------------|--------------------------------------|
| GET    | `/health`   | Liveness probe (Redis + Kafka status)|
| GET    | `/tiers`    | List all tier policies               |
| POST   | `/register` | Register a client with tier + algo   |
| GET    | `/resource` | Rate-limited demo endpoint           |
| GET    | `/heavy`    | Another rate-limited endpoint        |

### Register a Client

```bash
curl -X POST http://localhost:8000/register \
  -H "Content-Type: application/json" \
  -d '{"client_id": "my_service", "tier": "premium", "algorithm": "token_bucket"}'
```

### Hit the Rate-Limited Endpoint

```bash
curl http://localhost:8000/resource -H "X-Client-ID: my_service"
```

### Response Headers

Every response includes:
- `X-RateLimit-Limit` — max requests per window
- `X-RateLimit-Policy` — active algorithm (token_bucket / sliding_window / fixed_window)
- `Retry-After` — seconds until the window resets (on 429 only)

## Load Testing with k6

```bash
# Install k6 (macOS)
brew install k6

# Run the load test (services must be running)
k6 run k6_test.js

# Or via Docker (no install needed)
docker run --rm -i --network host grafana/k6 run - < k6_test.js
```

The k6 test simulates:
- **Ramp-up**: 0 → 50 VUs over 30s
- **Sustained**: 50 VUs for 60s
- **Spike**: 50 → 100 VUs for 30s
- **Cool-down**: 100 → 0 VUs over 10s

## Kafka Breach Alerts

When a client is rate-limited, a structured event is published to `rate_limit_alerts`:

```json
{
  "event_type": "RATE_LIMIT_BREACH",
  "client_id": "my_service",
  "tier": "free",
  "algorithm": "token_bucket",
  "endpoint": "/resource",
  "ip_address": "172.18.0.1",
  "timestamp": 1713500000.123,
  "timestamp_iso": "2025-04-19T06:00:00Z"
}
```

View live alerts:
```bash
docker-compose logs -f kafka-consumer
```

## Project Structure

```
├── app/
│   ├── main.py                    # FastAPI application & lifespan
│   ├── models.py                  # Pydantic models & tier policies
│   ├── algorithms/
│   │   └── rate_limiter.py        # Algorithm dispatch (Redis Lua)
│   ├── core/
│   │   ├── config.py              # Pydantic settings
│   │   ├── redis.py               # Redis connection management
│   │   ├── kafka.py               # Kafka alert producer
│   │   └── lua_scripts.py         # Lua scripts for atomic ops
│   └── middleware/
│       └── rate_limit.py          # FastAPI middleware
├── legacy_cpp_prototype/          # Original C++ design-pattern demo
├── docker-compose.yml             # Full stack orchestration
├── Dockerfile                     # Python 3.11 slim image
├── k6_test.js                     # Load test script
├── requirements.txt               # Python dependencies
└── README.md
```

## Algorithm Details

### Token Bucket
Tokens refill at a fixed rate. Allows bursts up to bucket capacity. Industry standard (Stripe, AWS API Gateway). Implemented as a Lua script tracking `tokens` and `last_refill_timestamp`.

### Sliding Window
Uses Redis Sorted Sets to track request timestamps. Removes expired entries and counts active ones atomically. No boundary-burst problem — most accurate algorithm.

### Fixed Window
Simplest approach — increments a counter that expires at the window boundary. Fast but susceptible to boundary bursts (2× traffic at window edges).

## License

MIT
