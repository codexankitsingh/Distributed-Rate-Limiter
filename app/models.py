"""
Pydantic models for tier-based rate-limiting policies, API responses,
and client configuration.
"""
from enum import Enum
from pydantic import BaseModel


class Algorithm(str, Enum):
    TOKEN_BUCKET = "token_bucket"
    SLIDING_WINDOW = "sliding_window"
    FIXED_WINDOW = "fixed_window"


class Tier(str, Enum):
    FREE = "free"
    BASIC = "basic"
    PREMIUM = "premium"
    ENTERPRISE = "enterprise"


class TierPolicy(BaseModel):
    """Rate-limiting parameters bound to a specific tier."""
    requests_per_window: int
    window_seconds: int
    refill_rate: float  # tokens/sec — only used by token_bucket
    algorithm: Algorithm

    class Config:
        use_enum_values = True


# ── Default per-tier policies ──────────────────────────────────────────
TIER_POLICIES: dict[str, TierPolicy] = {
    Tier.FREE: TierPolicy(
        requests_per_window=10,
        window_seconds=60,
        refill_rate=0.167,           # ~10 tokens / 60s
        algorithm=Algorithm.TOKEN_BUCKET,
    ),
    Tier.BASIC: TierPolicy(
        requests_per_window=100,
        window_seconds=60,
        refill_rate=1.667,           # ~100 tokens / 60s
        algorithm=Algorithm.SLIDING_WINDOW,
    ),
    Tier.PREMIUM: TierPolicy(
        requests_per_window=1000,
        window_seconds=60,
        refill_rate=16.667,          # ~1000 tokens / 60s
        algorithm=Algorithm.TOKEN_BUCKET,
    ),
    Tier.ENTERPRISE: TierPolicy(
        requests_per_window=10000,
        window_seconds=60,
        refill_rate=166.667,         # ~10000 tokens / 60s
        algorithm=Algorithm.TOKEN_BUCKET,
    ),
}


class RateLimitResponse(BaseModel):
    """JSON body returned when a request is rate-limited (HTTP 429)."""
    error: str = "rate_limited"
    message: str
    client_id: str
    tier: str
    retry_after_seconds: int


class HealthResponse(BaseModel):
    status: str
    redis: str
    kafka: str


class ClientInfo(BaseModel):
    """Payload for the /register endpoint."""
    client_id: str
    tier: Tier = Tier.FREE
    algorithm: Algorithm | None = None
