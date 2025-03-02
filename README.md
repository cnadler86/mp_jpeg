# MicroPython JPEG

This is a work in progress, so documentation might be incorrec or incomplete. The code and its API might also change.

A very fast and memory-efficient micropython jpeg decoder and encoder. At the moment only the esp port is supported.
If you are not familiar with building custom firmware, visit the releases page to download firmware that suits your board.

## Decoder

### API Reference

- `Decoder(format="RGB888", rotation=0, block=False)`: Creates a new decoder object.
  - `format`: Pixel format for output (`RGB565_BE`, `RGB565_LE`, `CbYCrY`, `RGB888`).
  - `rotation`: Rotation angle for decoding (0, 90, 180, 270). Default 0.
  - `block`: Enable block decoding (default: `False`).

- `get_block_counts(jpeg_data)`: Returns the number of blocks that the decoder will need to decode the full image. Only needed in case of block==True. 
  - `jpeg_data`: JPEG data to decode.

- `decode(jpeg_data)`: Decodes the JPEG image and returns the decoded image. With block==True, it decodes the next block of the JPEG image and returns the decoded block. The decoder will give you a block of full width and the height will be either 8 or 16 pixels. You can estimate the height by `image_height // decoder.get_block_counts()`
  - `jpeg_data`: JPEG data to decode.

- `decode_block(jpeg_data)`: 
  - `jpeg_data`: JPEG data to decode.

### Example

```python
import jpeg

# Create a JPEG decoder object
decoder = jpeg.Decoder(rotation=180, format="RGB888")

# Prepare the JPEG data for decoding
jpeg_data = open("path/to/jpeg/image.jpg", "rb").read()

# Decode the JPEG image
decoded_image = decoder.decode(jpeg_data)
```

## Encoder

### API Reference

- `Encoder(height=240, width=320, format="RGB888", quality=90, rotation=0)`: Creates a new encoder object.
  - `height`: Height of the input image. Required.
  - `width`: Width of the input image. Required.
  - `format`: Pixel format for input image (RGB888, RGBA, YCbYCr, YCbY2YCrY2, GRAY). Required.
  - `quality`: JPEG quality (1-100). Default 90.
  - `rotation`: Rotation angle for encoding (0, 90, 180, 270). Default 0.


- `encode(img_data)`: Encodes the image data to JPEG format and returns the encoded JPEG data.
  - `img_data`: Raw image data to encode.
  - Returns the bytes of the encoded image.

### Example

```python
import jpeg

# Create a JPEG encoder object
encoder = jpeg.Encoder(format="RGB888", quality=80, rotation=90, width=320, height=240)

# Encode the image data to JPEG format
image_data = open("path/to/raw/image.bin", "rb").read()
encoded_jpeg = encoder.encode(image_data)
```

## Benchmark Results for ESP32S3

The following tables present the results of the JPEG decoding and encoding benchmarks performed on an ESP32S3. The image was 240x320.

### Decoder Benchmark

**Input Image Size:** 26,366 bytes.

| Format    | FPS Normal Decode | FPS Block Decode (15) |
|-----------|-------------------|-----------------------|
| RGB565_BE | 21.80             | 34.73                 |
| RGB565_LE | 21.79             | 34.72                 |
| RGB888    | 17.61             | 32.14                 |
| CbYCrY    | 21.85             | 38.17                 |

**Input Image Size:** 7,941 bytes  

| Format    | FPS Normal Decode | FPS Block Decode (15) |
|-----------|-------------------|-----------------------|
| RGB565_BE | 29.65             | 62.27                 |
| RGB565_LE | 29.66             | 62.27                 |
| RGB888    | 22.45             | 55.74                 |
| CbYCrY    | 29.75             | 74.57                 |

Block decode will be faster, because other RAM-type might be used.

### Encoder Benchmark

| Quality | FPS RGB888 |
|---------|------------|
| 100     | 11.90      |
| 90      | 17.58      |
| 80      | 19.62      |
| 70      | 20.55      |
| 60      | 21.35      |

The FPS might vary, depending on the image.
