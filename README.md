# Python `pymtpng`

Python bindings for the [MTPNG library](https://github.com/bvibber/mtpng), a parallelized PNG encoder in Rust by Brion Vibber.

## Build prerequisites

- Python `3.12+`
- Rust toolchain (`cargo`)
- CMake `>=3.24`
- Ninja
- C++20 compiler

## Build with dependencies from lockfile

```shell
uv sync --no-install-project
uv build --wheel --no-build-isolation
```

## Develop with auto rebuild

```shell
uv sync
uv run ruff check .
uv run pytest
```

## Usage

```python
import pymtpng

# Encode numpy uint8 array to PNG
with open("image_uint8.png", "wb") as fh:
    pymtpng.encode_png(image_uint8, fh)

# Encode numpy uint16 array to PNG
with open("image_uint16.png", "wb") as fh:
    pymtpng.encode_u16_png(image_uint16, fh)

# Customize encoding options
pymtpng.encode_png(
    image,
    fh,
    filter=pymtpng.Filter.Adaptive,
    compression_level=pymtpng.CompressionLevel.Fast,
    strategy=pymtpng.Strategy.Huffman,
)

# Store key-value pairs as iTXt chunks
pymtpng.encode_png(image, fh, info={"Hello": "World"})
```
