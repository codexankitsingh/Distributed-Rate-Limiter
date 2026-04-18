import redis.asyncio as redis
from app.core.config import settings

redis_client = None

async def init_redis():
    global redis_client
    redis_client = redis.from_url(
        settings.REDIS_URL,
        encoding="utf-8",
        decode_responses=True
    )
    # Ping to check connection
    await redis_client.ping()

async def close_redis():
    if redis_client:
        await redis_client.close()

async def get_redis():
    return redis_client
