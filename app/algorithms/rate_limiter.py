import time
import uuid
from app.core.redis import get_redis
from app.core.lua_scripts import FIXED_WINDOW_LUA, SLIDING_WINDOW_LUA, TOKEN_BUCKET_LUA

class RateLimiterAlgorithms:
    # Registers scripts in redis for faster execution
    _fixed_window_sha = None
    _sliding_window_sha = None
    _token_bucket_sha = None

    @classmethod
    async def _load_scripts(cls):
        redis = await get_redis()
        if cls._fixed_window_sha is None:
            cls._fixed_window_sha = await redis.script_load(FIXED_WINDOW_LUA)
            cls._sliding_window_sha = await redis.script_load(SLIDING_WINDOW_LUA)
            cls._token_bucket_sha = await redis.script_load(TOKEN_BUCKET_LUA)

    @classmethod
    async def fixed_window(cls, client_id: str, limit: int, window: int) -> bool:
        await cls._load_scripts()
        redis = await get_redis()
        key = f"rate_limit:fw:{client_id}"
        result = await redis.evalsha(cls._fixed_window_sha, 1, key, limit, window)
        return bool(result)

    @classmethod
    async def sliding_window(cls, client_id: str, limit: int, window: int) -> bool:
        await cls._load_scripts()
        redis = await get_redis()
        key = f"rate_limit:sw:{client_id}"
        now = time.time()
        member = f"{now}-{uuid.uuid4()}"
        result = await redis.evalsha(cls._sliding_window_sha, 1, key, limit, now, window, member)
        return bool(result)

    @classmethod
    async def token_bucket(cls, client_id: str, capacity: int, refill_rate: float) -> bool:
        await cls._load_scripts()
        redis = await get_redis()
        tokens_key = f"rate_limit:tb:{client_id}:tokens"
        timestamp_key = f"rate_limit:tb:{client_id}:ts"
        now = time.time()
        # ARGV: capacity, refill_rate, refill_interval(1s), now, requested(1)
        result = await redis.evalsha(cls._token_bucket_sha, 2, tokens_key, timestamp_key, capacity, refill_rate, 1, now, 1)
        return bool(result)
