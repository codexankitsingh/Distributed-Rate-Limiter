# Lua scripts execute atomically in Redis.

FIXED_WINDOW_LUA = """
local key = KEYS[1]
local limit = tonumber(ARGV[1])
local window = tonumber(ARGV[2])

local current = redis.call("GET", key)
if current and tonumber(current) >= limit then
    return 0
end

current = redis.call("INCR", key)
if tonumber(current) == 1 then
    redis.call("EXPIRE", key, window)
end

return 1
"""

SLIDING_WINDOW_LUA = """
local key = KEYS[1]
local limit = tonumber(ARGV[1])
local now = tonumber(ARGV[2])
local window = tonumber(ARGV[3])
local member = ARGV[4]

local window_start = now - window

-- Remove old requests
redis.call('ZREMRANGEBYSCORE', key, 0, window_start)

-- Count current requests
local current_requests = redis.call('ZCARD', key)

if current_requests < limit then
    redis.call('ZADD', key, now, member)
    redis.call('EXPIRE', key, window)
    return 1
else
    return 0
end
"""

TOKEN_BUCKET_LUA = """
local tokens_key = KEYS[1]
local timestamp_key = KEYS[2]

local capacity = tonumber(ARGV[1])
local refill_rate = tonumber(ARGV[2]) -- tokens per second
local refill_interval = tonumber(ARGV[3]) -- usually 1 second
local now = tonumber(ARGV[4])
local requested = tonumber(ARGV[5]) -- Usually 1

local last_tokens = tonumber(redis.call("GET", tokens_key))
if last_tokens == nil then
    last_tokens = capacity
end

local last_refreshed = tonumber(redis.call("GET", timestamp_key))
if last_refreshed == nil then
    last_refreshed = now
end

local delta = math.max(0, now - last_refreshed)
local filled_tokens = math.min(capacity, last_tokens + (delta * refill_rate))

local allowed = filled_tokens >= requested

if allowed then
    local new_tokens = filled_tokens - requested
    redis.call("SET", tokens_key, new_tokens)
    redis.call("SET", timestamp_key, now)
    
    -- Optional: set TTL so keys don't live forever
    redis.call("EXPIRE", tokens_key, capacity / refill_rate * 2)
    redis.call("EXPIRE", timestamp_key, capacity / refill_rate * 2)
    return 1
else
    return 0
end
"""
