import os
from dataclasses import dataclass


@dataclass
class Settings:
    db_path: str
    secure_cookies: bool
    static_dir: str


def get_settings() -> Settings:
    return Settings(
        db_path=os.environ.get("SCRIBBLE_DB", "/data/scribble.db"),
        secure_cookies=os.environ.get("SCRIBBLE_SECURE_COOKIES", "1").lower() not in ("0", "false", "no", ""),
        static_dir=os.environ.get("SCRIBBLE_STATIC", "/app/web"),
    )
