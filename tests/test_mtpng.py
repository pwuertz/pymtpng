import io

import numpy as np
import pymtpng
import pytest
from numpy.testing import assert_array_equal
from PIL import Image


@pytest.fixture(params=[np.uint8, np.uint16])
def image(request):
    dtype = request.param
    return np.eye(32, dtype=dtype)


def encode_png(
    image: np.ndarray,
    compression_level: pymtpng.CompressionLevel,
    strategy: pymtpng.Strategy,
) -> io.BytesIO:
    encode_fn = pymtpng.encode_png if image.dtype == np.uint8 else pymtpng.encode_u16_png
    buffer = io.BytesIO()
    encode_fn(image, buffer, compression_level=compression_level, strategy=strategy)
    buffer.seek(0)
    return buffer


def decode_png(buffer: io.BytesIO) -> np.ndarray:
    return np.asarray(Image.open(buffer))


def test_encode(image: np.ndarray):
    png = encode_png(image, pymtpng.CompressionLevel.Fast, pymtpng.Strategy.Huffman)
    image_out = decode_png(png)
    assert_array_equal(image_out, image)
