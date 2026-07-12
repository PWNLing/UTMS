#!/usr/bin/env python3
"""Send complete radar snapshot frames over UDP for development and demos."""

from __future__ import annotations

import argparse
import json
import math
import random
import socket
import time
from dataclasses import dataclass


DEFAULT_LATITUDE = 25.311724
DEFAULT_LONGITUDE = 110.416819
TARGET_TYPES = ("CAR", "TRUCK", "PEDESTRIAN", "BICYCLE", "UNKNOWN")


@dataclass
class Target:
    track_id: int
    target_type: str
    phase: float
    radius: float
    speed: float

    def snapshot(self, elapsed_seconds: float) -> dict[str, object]:
        angle = self.phase + elapsed_seconds * self.speed
        latitude = DEFAULT_LATITUDE + math.sin(angle) * self.radius
        longitude = DEFAULT_LONGITUDE + math.cos(angle) * self.radius
        return {
            "track_id": self.track_id,
            "type": self.target_type,
            "position": {"latitude": latitude, "longitude": longitude},
            "distance": round(math.hypot(latitude - DEFAULT_LATITUDE,
                                         longitude - DEFAULT_LONGITUDE) * 100_000, 2),
            "velocity": round(1.0 + abs(self.speed) * 18.0, 2),
        }


def parse_target_range(value: str) -> tuple[int, int]:
    try:
        if "-" in value:
            minimum, maximum = (int(part) for part in value.split("-", maxsplit=1))
        else:
            minimum = maximum = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("targets must be COUNT or MIN-MAX") from error
    if minimum < 0 or maximum < minimum:
        raise argparse.ArgumentTypeError("target range must satisfy 0 <= MIN <= MAX")
    return minimum, maximum


def create_target(track_id: int) -> Target:
    return Target(
        track_id=track_id,
        target_type=random.choice(TARGET_TYPES),
        phase=random.uniform(0.0, math.tau),
        radius=random.uniform(0.0002, 0.004),
        speed=random.uniform(-0.16, 0.16),
    )


def build_frame(sequence: int, targets: list[Target], elapsed_seconds: float) -> dict[str, object]:
    tracks = [target.snapshot(elapsed_seconds) for target in targets]
    now = time.time()
    return {
        "type": "fusion_geodetic_data",
        "version": "1.0",
        "timestamp": now,
        "source": "utms_udp_simulator",
        "header": {"frame_id": "radar_1", "stamp": {"sec": int(now)}, "sequence": sequence},
        "ego_position": {"latitude": DEFAULT_LATITUDE, "longitude": DEFAULT_LONGITUDE},
        "target_count": len(tracks),
        "tracks": tracks,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1", help="destination IPv4 address")
    parser.add_argument("--port", type=int, default=10_000, help="destination UDP port")
    parser.add_argument("--targets", type=parse_target_range, default=(10, 20),
                        metavar="COUNT|MIN-MAX", help="targets per frame (default: 10-20)")
    parser.add_argument("--fps", type=float, default=11.0, help="send frequency (default: 11)")
    args = parser.parse_args()

    if not 1 <= args.port <= 65_535:
        parser.error("port must be in the range 1-65535")
    if args.fps <= 0.0:
        parser.error("fps must be greater than zero")

    minimum_targets, maximum_targets = args.targets
    targets = [create_target(2_000 + index) for index in range(maximum_targets)]
    interval_seconds = 1.0 / args.fps
    started_at = time.monotonic()
    next_send_at = started_at
    sequence = 1

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_socket:
        try:
            while True:
                target_count = random.randint(minimum_targets, maximum_targets)
                frame = build_frame(sequence, targets[:target_count], time.monotonic() - started_at)
                payload = json.dumps(frame, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
                udp_socket.sendto(payload, (args.host, args.port))
                sequence += 1
                next_send_at += interval_seconds
                time.sleep(max(0.0, next_send_at - time.monotonic()))
        except KeyboardInterrupt:
            print("\nSimulator stopped.")


if __name__ == "__main__":
    main()
