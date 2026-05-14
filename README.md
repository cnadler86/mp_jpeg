# MicroPython JPEG

A fast and memory-efficient JPEG decoder/encoder module for MicroPython (ESP port).

## Features
- JPEG **Decoder**: normal decode (full frame) and **block decode** (tile/strip decode)
- JPEG **Encoder**
- `decode_into()` supports **zero-copy writing into a user-provided framebuffer**
- Designed for embedded UI/animation workloads (cooperative scheduling)

---

# Getting started

```python
import jpeg
print("JPEG Driver Version:", jpeg.version())
```

---

# Decoder

## Create a decoder

```python
decoder = jpeg.Decoder(
    pixel_format="RGB565_LE",
    rotation=0,
    block=False,
    scale_width=0, scale_height=0,
    clipper_width=0, clipper_height=0,
    return_bytes=False,
)
```

### Parameters
- `pixel_format`: Output pixel format.
  - Supported: `RGB565_BE`, `RGB565_LE`, `CbYCrY`, `RGB888`
- `rotation`: `0`, `90`, `180`, `270`
- `block`:
  - `False` (default): normal decode (full image per decode)
  - `True`: block decode mode (each decode produces one block; usually 8 or 16 lines per block)
  - If `block=True`: scaling/clipper/rotation are not supported (library limitation)
- `scale_width`, `scale_height`:
  - Optional output scaling (must match JPEG constraints and be multiple of 8)
- `clipper_width`, `clipper_height`:
  - Optional crop (must be multiple of 8; must be <= scale)
- `return_bytes`:
  - `False` (default): `decode()` returns `memoryview`
  - `True`: `decode()` returns `bytes`

---

## get_img_info(jpeg_data)

```python
info = decoder.get_img_info(jpeg_data)
```

Returns:
- normal mode: `[width, height]`
- block mode: `[width, height, blocks, block_height]`

Where:
- `blocks` = number of blocks to decode full image
- `block_height` is usually `8` or `16`

---

## decode(jpeg_data)

```python
img_or_block = decoder.decode(jpeg_data)
```

- If `block=False`: returns the full decoded frame
- If `block=True`: returns the next decoded block (full width, height = 8 or 16 lines)
- When block decoding is finished:
  - returns `None`

This API is useful when you want the raw block bytes/memoryview and handle placement/output yourself.

---

## decode_into(jpeg_data, out_buffer, *, blocks=0)  ✅ NEW API

```python
done = decoder.decode_into(jpeg_data, framebuffer)            # blocks defaults to 0 (FULL)
done = decoder.decode_into(jpeg_data, framebuffer, blocks=1)  # step
```

### Goal
Decode and write directly into a user-provided buffer (framebuffer).  
This avoids Python slice copies and reduces GC pressure.

### Return value
- Returns **bool**
  - `True`: this call completed one full decode round (framebuffer is ready)
  - `False`: not finished yet (only possible in `block=True` with `blocks>0`)

### blocks parameter (important)
- `blocks=0` (**default**): **FULL mode**
  - Continue decoding from the current progress until the frame is complete
  - Returns `True`
- `blocks>0`: **STEP mode**
  - Decode at most `blocks` blocks from current progress
  - Returns:
    - `False` if not finished yet
    - `True` if finished within this call
- `blocks<0`: raises `ValueError`

### Auto-rewind behavior (by design)
After a full round is completed, if you call `decode_into()` again with the **same** `jpeg_data`,
the decoder will automatically restart (rewind) and decode again.

This simplifies animation loops (you control whether to call again or switch to the next frame at a higher level).

### Buffer requirements
- If `block=False`:
  - `out_buffer` must be at least the full decoded frame size
- If `block=True`:
  - `out_buffer` must be large enough to hold the **full frame** (because the module writes each block into the correct offset automatically)

### Practical guide
#### 1) Choose pixel format and size the framebuffer
- `RGB565_BE` / `RGB565_LE` / `CbYCrY`: 2 bytes per pixel
- `RGB888`: 3 bytes per pixel

```python
import jpeg

img = open("image.jpg", "rb").read()
dec = jpeg.Decoder(pixel_format="RGB565_LE", rotation=0, block=True)
info = dec.get_img_info(img)
w, h = info[0], info[1]

bytes_per_pixel = 2
fb = bytearray(w * h * bytes_per_pixel)
```

#### 2) Full decode (simple, highest throughput per call)
Use this when you can afford decoding the whole frame in one call.

```python
done = dec.decode_into(img, fb)  # blocks defaults to 0
if done:
    pass
```

#### 3) STEP decode (cooperative scheduling, UI-friendly)
Use this when you want to spread decoding across multiple iterations to keep the UI responsive.

```python
done = dec.decode_into(img, fb, blocks=1)
if done:
    pass
```

#### 4) Correct loop pattern for STEP mode
`decode_into(..., blocks>0)` can return `False` until the image is complete.

```python
while True:
    done = dec.decode_into(img, fb, blocks=1)
    if done:
        break
    # do other work here (render UI, poll input, etc.)
```

### When to use `decode_into` vs `decode`
- Prefer `decode_into` when your goal is a full framebuffer for display (avoids Python slice assembly and reduces GC pressure).
- Use `decode(block=True)` when you need raw block bytes and want to handle placement/output yourself.

### Notes and caveats
- STEP mode is meaningful when using `block=True` and `blocks>0`.
- `block=True` has limitations: rotation must be 0, and scaling/clipping are not supported.
- The decoder auto-rewinds after completing a full round when you call `decode_into()` again with the same `jpeg_data`, which is convenient for animation loops.

---

# Encoder

## Create an encoder

```python
enc = jpeg.Encoder(
    height=240,
    width=320,
    pixel_format="RGB888",
    quality=90,
    rotation=0,
)
```

### Parameters
- `height`, `width`: required
- `pixel_format` (input format): supported by driver (e.g. `RGB888`, `RGB565`, `RGBA`, `YCbYCr`, `CbYCrY`, `GRAY`, etc.)
- `quality`: 1..100
- `rotation`: `0`, `90`, `180`, `270`

## encode(img_data)

```python
jpeg_bytes = enc.encode(raw_image_bytes)
```

Returns `bytes`.

---

# Benchmark

## Benchmark script
Use the provided `benchmark.py` (updated for the new bool-return `decode_into` API).

## Test image
- Resolution: **240×240**
- JPEG size: **32627 bytes**
- Blocks: **30** (block height: **8**)
- NR = 100

## Decoder results

| Format     | FPS normal decode (`decode`, block=False) | FPS block decode (`decode`, block=True) | FPS block decode + write (python slice) | FPS decode_into step (blocks=1) | FPS decode_into full (blocks=0) |
|-----------|--------------------------------------------|------------------------------------------|------------------------------------------|----------------------------------|----------------------------------|
| RGB565_BE | 18.47 | 24.65 | 4.82 | 21.38 | 21.52 |
| RGB565_LE | 18.46 | 24.65 | 4.82 | 21.40 | 21.52 |
| RGB888    | 16.49 | 23.75 | 3.43 | 20.19 | 20.34 |
| CbYCrY    | 18.92 | 26.03 | 4.85 | 21.99 | 22.13 |

### Notes
- `block decode` is fastest when you only need block output (no full-frame assembly).
- `python slice` assembly is slow due to Python-level copying.
- `decode_into` provides a practical middle ground: good speed + direct framebuffer output.

## Encoder results

| Quality | FPS (RGB888) |
|--------:|--------------:|
| 100     | 10.95 |
| 90      | 16.88 |
| 80      | 19.18 |
| 70      | 20.23 |
| 60      | 22.46 |

---

# Build (ESP-IDF / MicroPython external C module)

## Requirements
- ESP-IDF: tested on 5.2 / 5.3 / 5.4
- MicroPython: tested around v1.24
- ESP JPEG library: `espressif/esp_new_jpeg`

Add dependency in `idf_component.yml` (example):
```yaml
dependencies:
  espressif/esp_new_jpeg: "^1.0.0"
```

## Build
```sh
. <path-to-esp-idf>/export.sh
cd micropython/ports/esp32

make USER_C_MODULES=../../../../mp_jpeg/micropython.cmake BOARD=<Your-Board> clean
make USER_C_MODULES=../../../../mp_jpeg/micropython.cmake BOARD=<Your-Board> submodules
make USER_C_MODULES=../../../../mp_jpeg/micropython.cmake BOARD=<Your-Board> all
```

---

# Example usage

## Full decode into framebuffer (fast, simple)
```python
import jpeg

img = open("image.jpg","rb").read()

dec = jpeg.Decoder(pixel_format="RGB565_LE", rotation=0, block=True)
info = dec.get_img_info(img)
w, h = info[0], info[1]

fb = bytearray(w * h * 2)

done = dec.decode_into(img, fb)   # default blocks=0 FULL
# done == True
# fb is ready
```

## Cooperative decode (UI-friendly)
```python
# each frame decode 1 block to avoid blocking UI
done = dec.decode_into(img, fb, blocks=1)
if done:
    # completed this image
    pass
```
