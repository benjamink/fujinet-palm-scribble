import io

import pytest
from PIL import Image

from app.bitmap import to_png, validate_bitmap
from app.constants import NOTE_BYTES
from app.errors import ServiceError


@pytest.mark.parametrize("n", [0, 2399, 2401])
def test_validate_rejects_wrong_lengths(n):
    with pytest.raises(ServiceError) as ei:
        validate_bitmap(b"\x00" * n)
    assert ei.value.reason == "bad sketch"


def test_validate_accepts_exact_length():
    validate_bitmap(b"\x00" * NOTE_BYTES)


def _decode(png: bytes) -> Image.Image:
    img = Image.open(io.BytesIO(png))
    img.load()
    return img


def test_to_png_pixels():
    img = _decode(to_png(b"\x80" + b"\x00" * (NOTE_BYTES - 1)))
    assert img.size == (160, 120)
    assert img.mode == "1"
    assert img.getpixel((0, 0)) == 0
    assert img.getpixel((1, 0)) == 255
    assert img.getpixel((0, 1)) == 255


def test_to_png_all_white_and_all_black():
    white = _decode(to_png(b"\x00" * NOTE_BYTES))
    black = _decode(to_png(b"\xff" * NOTE_BYTES))
    assert white.convert("L").getextrema() == (255, 255)
    assert black.convert("L").getextrema() == (0, 0)
