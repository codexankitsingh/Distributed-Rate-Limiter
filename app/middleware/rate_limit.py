"""
Configurable FastAPI middleware for per-client rate limiting.

Intercepts every request, resolves the client identity from the
X-Client-ID header (falling back to IP), looks up the client's tier
policy, and enforces the corresponding algorithm (Token Bucket,
Sliding Window, or Fixed Window) via Redis atomic operations.

When a limit is breached the middleware:
  • Returns HTTP 429 with a structured JSON body and Retry-After header.
  • Fires a non-blocking Kafka alert for downstream event-driven reactions.
"""
import logging
import time

from starlette.middleware.base import BaseHTTPMiddleware
from starlette.requests import Request
from starlette.responses import JSONResponse

from app.algorithms.rate_limiter import RateLimiterAlgorithms
from app.core.kafka import kafka_producer
from app.core.redis import get_redis
from app.models import Algorithm, Tier, TierPolicy, TIER_POLICIES

logger = logging.getLogger("rate_limiter.middleware")

# ── Paths exempt from rate limiting ────────────────────────────────────
EXEMPT_PATHS = {"/health", "/docs", "/openapi.json", "/redoc", "/favicon.ico"}


async def _resolve_client(request: Request) -> tuple[str, str, TierPolicy]:
    """
    Resolve the client identity, tier, and policy.

    1. Look for X-Client-ID header.
    2. Check Redis for a registered tier override.
    3. Fall back to the FREE tier policy.

    Returns (client_id, tier_name, policy).
    """
    client_id = request.headers.get("X-Client-ID", request.client.host if request.client else "unknown")
    redis = await get_redis()

    # Check if client has a registered tier in Redis
    tier_name = await redis.get(f"client:tier:{client_id}")
    if tier_name is None:
        tier_name = Tier.FREE

    # Check if client has an algorithm override
    algo_override = await redis.get(f"client:algo:{client_id}")

    policy = TIER_POLICIES[tier_name].model_copy()
    if algo_override:
        policy.algorithm = algo_override

    return client_id, tier_name, policy


async def _enforce_limit(client_id: str, policy: TierPolicy) -> bool:
    """
    Run the rate-limit check using the algorithm specified by the
    client's tier policy.  Returns True if the request is ALLOWED.
    """
    if policy.algorithm == Algorithm.TOKEN_BUCKET:
        return await RateLimiterAlgorithms.token_bucket(
            client_id,
            capacity=policy.requests_per_window,
            refill_rate=policy.refill_rate,
        )
    elif policy.algorithm == Algorithm.SLIDING_WINDOW:
        return await RateLimiterAlgorithms.sliding_window(
            client_id,
            limit=policy.requests_per_window,
            window=policy.window_seconds,
        )
    elif policy.algorithm == Algorithm.FIXED_WINDOW:
        return await RateLimiterAlgorithms.fixed_window(
            client_id,
            limit=policy.requests_per_window,
            window=policy.window_seconds,
        )
    else:
        # Unknown algorithm — fail open
        logger.warning("Unknown algorithm '%s' for client %s — allowing request.", policy.algorithm, client_id)
        return True


class RateLimitMiddleware(BaseHTTPMiddleware):
    """
    Starlette-compatible middleware that enforces per-client rate policies.

    Usage (in main.py):
        app.add_middleware(RateLimitMiddleware)
    """

    async def dispatch(self, request: Request, call_next):
        # Skip exempt paths
        if request.url.path in EXEMPT_PATHS:
            return await call_next(request)

        try:
            client_id, tier_name, policy = await _resolve_client(request)
        except Exception as exc:
            # Fail-open: if Redis is down we let traffic through
            logger.error("Rate-limit resolution failed (%s) — failing open.", exc)
            return await call_next(request)

        allowed = await _enforce_limit(client_id, policy)

        if allowed:
            response = await call_next(request)
            # Attach informational headers
            response.headers["X-RateLimit-Limit"] = str(policy.requests_per_window)
            response.headers["X-RateLimit-Policy"] = policy.algorithm
            return response

        # ── Request DENIED ─────────────────────────────────────────
        retry_after = policy.window_seconds

        # Fire Kafka alert (non-blocking)
        try:
            await kafka_producer.send_alert(
                client_id=client_id,
                tier=tier_name,
                algorithm=policy.algorithm,
                endpoint=request.url.path,
                ip_address=request.client.host if request.client else "unknown",
            )
        except Exception as exc:
            logger.warning("Kafka alert dispatch failed: %s", exc)

        logger.info(
            "RATE LIMITED client=%s tier=%s algo=%s path=%s",
            client_id, tier_name, policy.algorithm, request.url.path,
        )

        return JSONResponse(
            status_code=429,
            content={
                "error": "rate_limited",
                "message": f"Rate limit exceeded. Retry after {retry_after}s.",
                "client_id": client_id,
                "tier": tier_name,
                "retry_after_seconds": retry_after,
            },
            headers={
                "Retry-After": str(retry_after),
                "X-RateLimit-Limit": str(policy.requests_per_window),
                "X-RateLimit-Policy": policy.algorithm,
            },
        )
