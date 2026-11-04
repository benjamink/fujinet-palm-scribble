import io

from PIL import Image

from .constants import NOTE_BYTES, NOTE_H, NOTE_W
from .errors import ServiceError


def validate_bitmap(b: bytes) -> None:
    if len(b) != NOTE_BYTES:
        raise ServiceError("bad sketch")


def to_png(b: bytes) -> bytes:
    validate_bitmap(b)
    # Wire format: 1 = black. Pillow mode "1" treats set bits as white, so invert.
    inverted = bytes(x ^ 0xFF for x in b)
    img = Image.frombytes("1", (NOTE_W, NOTE_H), inverted)
    out = io.BytesIO()
    img.save(out, format="PNG")
    return out.getvalue()
