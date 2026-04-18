"""
Kafka-based event hooks for real-time rate-breach alerting.

Produces structured JSON events to the 'rate_limit_alerts' topic whenever a
client is rate-limited, enabling event-driven downstream reactions (e.g.,
dashboards, auto-blocking, webhook notifications) and horizontal scalability
across distributed nodes.
"""
import json
import logging
import time

from aiokafka import AIOKafkaProducer
from app.core.config import settings

logger = logging.getLogger("rate_limiter.kafka")


class KafkaAlertProducer:
    """
    Asynchronous Kafka producer for rate-limit breach events.

    Lifecycle is tied to the FastAPI app startup/shutdown hooks so the
    underlying librdkafka connection is properly cleaned up.
    """

    def __init__(self):
        self._producer: AIOKafkaProducer | None = None

    async def start(self):
        """Open the connection to the Kafka broker."""
        try:
            self._producer = AIOKafkaProducer(
                bootstrap_servers=settings.KAFKA_BROKER_URL,
                value_serializer=lambda v: json.dumps(v).encode("utf-8"),
                # Idempotent producer avoids duplicates on retries
                enable_idempotence=True,
            )
            await self._producer.start()
            logger.info("Kafka alert producer connected to %s", settings.KAFKA_BROKER_URL)
        except Exception as exc:
            logger.warning("Kafka unavailable (%s) — alerting disabled, service continues.", exc)
            self._producer = None

    async def stop(self):
        """Flush pending messages and close the connection."""
        if self._producer:
            await self._producer.stop()
            logger.info("Kafka alert producer stopped.")

    async def send_alert(
        self,
        client_id: str,
        tier: str,
        algorithm: str,
        endpoint: str,
        ip_address: str,
    ):
        """
        Emit a rate-limit breach event.

        The event is fire-and-forget so it never blocks the HTTP response path.
        """
        if self._producer is None:
            logger.debug("Kafka producer not available — alert skipped for %s", client_id)
            return

        event = {
            "event_type": "RATE_LIMIT_BREACH",
            "client_id": client_id,
            "tier": tier,
            "algorithm": algorithm,
            "endpoint": endpoint,
            "ip_address": ip_address,
            "timestamp": time.time(),
            "timestamp_iso": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        }

        try:
            await self._producer.send_and_wait(
                settings.KAFKA_TOPIC_ALERTS,
                value=event,
                key=client_id.encode("utf-8"),
            )
            logger.info("Kafka alert sent for client=%s", client_id)
        except Exception as exc:
            # Non-blocking: log and move on — never fail the request
            logger.warning("Failed to send Kafka alert for %s: %s", client_id, exc)


# Module-level singleton — initialised once at app startup
kafka_producer = KafkaAlertProducer()
