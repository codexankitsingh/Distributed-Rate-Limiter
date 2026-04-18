from pydantic_settings import BaseSettings

class Settings(BaseSettings):
    PROJECT_NAME: str = "Distributed Rate Limiter"
    REDIS_URL: str = "redis://redis:6379/0"
    KAFKA_BROKER_URL: str = "kafka:9092"
    KAFKA_TOPIC_ALERTS: str = "rate_limit_alerts"

    class Config:
        env_file = ".env"

settings = Settings()
