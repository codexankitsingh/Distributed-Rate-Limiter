"""
Distributed Rate Limiter Service
=================================
FastAPI application exposing a configurable per-client rate limiting
service backed by Redis atomic operations (Lua scripts) and
Kafka-based real-time breach alerting.

Supported algorithms:
  • Token Bucket   — burst-tolerant, industry standard (Stripe, AWS)
  • Sliding Window — accurate, no boundary-burst problem
  • Fixed Window   — simple counter per time slot
"""
import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse

from app.core.config import settings
from app.core.redis import init_redis, close_redis, get_redis
from app.core.kafka import kafka_producer
from app.middleware.rate_limit import RateLimitMiddleware
from app.models import (
    Algorithm,
    ClientInfo,
    HealthResponse,
    Tier,
    TIER_POLICIES,
)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s | %(name)-30s | %(levelname)-7s | %(message)s",
)
logger = logging.getLogger("rate_limiter.app")


# ── Lifespan: startup / shutdown ──────────────────────────────────────
@asynccontextmanager
async def lifespan(app: FastAPI):
    """Initialise Redis and Kafka on startup; tear down on shutdown."""
    logger.info("Starting %s …", settings.PROJECT_NAME)

    # Redis
    await init_redis()
    logger.info("Redis connected at %s", settings.REDIS_URL)

    # Kafka
    await kafka_producer.start()

    yield  # ← application is running

    # Shutdown
    await kafka_producer.stop()
    await close_redis()
    logger.info("Shutdown complete.")


# ── App creation ──────────────────────────────────────────────────────
app = FastAPI(
    title=settings.PROJECT_NAME,
    description=(
        "Distributed rate limiting service supporting Token Bucket, "
        "Sliding Window, and Fixed Window algorithms via Redis atomic "
        "operations.  Kafka-based event hooks for real-time breach alerting."
    ),
    version="1.0.0",
    lifespan=lifespan,
)

# Attach rate-limiting middleware
app.add_middleware(RateLimitMiddleware)


# ══════════════════════════════════════════════════════════════════════
# Endpoints
# ══════════════════════════════════════════════════════════════════════

@app.get("/health", response_model=HealthResponse, tags=["System"])
async def health_check():
    """
    Liveness probe.  Reports connectivity to Redis and Kafka.
    """
    redis_status = "disconnected"
    kafka_status = "disconnected"

    try:
        redis = await get_redis()
        await redis.ping()
        redis_status = "connected"
    except Exception:
        pass

    if kafka_producer._producer is not None:
        kafka_status = "connected"

    return HealthResponse(
        status="healthy" if redis_status == "connected" else "degraded",
        redis=redis_status,
        kafka=kafka_status,
    )


@app.post("/register", tags=["Client Management"])
async def register_client(info: ClientInfo):
    """
    Register (or update) a client with a specific tier and optional
    algorithm override.  The mapping is stored in Redis so every
    node in the cluster sees the same configuration.
    """
    redis = await get_redis()
    await redis.set(f"client:tier:{info.client_id}", info.tier.value)

    if info.algorithm:
        await redis.set(f"client:algo:{info.client_id}", info.algorithm.value)

    policy = TIER_POLICIES[info.tier]
    logger.info(
        "Registered client=%s tier=%s algo=%s",
        info.client_id, info.tier.value,
        info.algorithm.value if info.algorithm else policy.algorithm,
    )

    return {
        "message": "Client registered successfully.",
        "client_id": info.client_id,
        "tier": info.tier.value,
        "algorithm": info.algorithm.value if info.algorithm else policy.algorithm,
        "policy": {
            "requests_per_window": policy.requests_per_window,
            "window_seconds": policy.window_seconds,
        },
    }


@app.get("/tiers", tags=["Configuration"])
async def list_tiers():
    """
    Return all available tiers and their rate-limiting policies.
    Useful for documentation and client self-service.
    """
    return {
        tier: {
            "requests_per_window": p.requests_per_window,
            "window_seconds": p.window_seconds,
            "refill_rate_per_sec": p.refill_rate,
            "algorithm": p.algorithm,
        }
        for tier, p in TIER_POLICIES.items()
    }


@app.get("/", tags=["Demo"])
async def root():
    """
    A simple demo endpoint to test rate limiting against.
    """
    return {
        "message": "Welcome to the Distributed Rate Limiter!",
        "docs": "/docs",
        "health": "/health",
    }


@app.get("/resource", tags=["Demo"])
async def protected_resource(request: Request):
    """
    Simulated protected resource.  The middleware will enforce rate
    limits before this handler is reached.
    """
    client_id = request.headers.get("X-Client-ID", "anonymous")
    return {
        "message": "Access granted.",
        "client_id": client_id,
        "data": "⚡ This is a rate-limited resource.",
    }


@app.get("/heavy", tags=["Demo"])
async def heavy_resource(request: Request):
    """
    Higher-cost endpoint — behaves identically but is useful for
    demonstrating per-route differentiation in load tests.
    """
    client_id = request.headers.get("X-Client-ID", "anonymous")
    return {
        "message": "Heavy resource accessed.",
        "client_id": client_id,
        "data": "🔥 Expensive operation completed.",
    }
