# JPEG ESP MicroPython Wrapper
A very fast micropython jpeg decoder and encoder, at the moment just for most of the esp boards.
Take a look at the releases or in the workflows. There you will file precompiled images with the latest 
## Features

- JPEG decoding with various pixel formats and rotations
- JPEG encoding with configurable quality and rotation
- Memory-efficient operations for embedded systems

## Installation

Clone the repository to your local machine:

```bash
git clone https://github.com/cnadler86/mp_jpeg.git
```

## Usage

### Decoder

The `Decoder` class provides methods to decode JPEG images.

#### Example

```python
import jpeg

# Create a JPEG decoder object
decoder = jpeg.Decoder(rotation=0, format="RGB888")

# Prepare the JPEG data for decoding
jpeg_data = open("path/to/jpeg/image.jpg", "rb").read()

# Decode the JPEG image
decoded_image = decoder.decode(jpeg_data)
```

### Encoder

The `Encoder` class provides methods to encode images to JPEG format.

#### Example

```python
import jpeg

# Create a JPEG encoder object
encoder = jpeg.Encoder(format="RGB888", quality=90, rotation=0, width=320, height=240)

# Encode the image data to JPEG format
image_data = open("path/to/raw/image.bin", "rb").read()
encoded_jpeg = encoder.encode(image_data)
```

## API Reference

### Decoder

- `Decoder(rotation=0, format="RGB888", block=False)`: Creates a new decoder object.
  - `rotation`: Rotation angle for decoding (0, 90, 180, 270).
  - `format`: Pixel format for output (`RGB565_BE`, `RGB565_LE`, `CbYCrY`, `RGB888`).
  - `block`: Enable block decoding (default: `False`).

- `get_block_counts(jpeg_data)`: Prepares the JPEG data for decoding and returns the number of blocks. Only needed in case of block=True. 
  - `jpeg_data`: JPEG data to decode.

- `decode(jpeg_data)`: Decodes the JPEG image and returns the decoded image.
  - `jpeg_data`: JPEG data to decode.

- `decode_block(jpeg_data)`: Decodes the next block of the JPEG image and returns the decoded block. The decoder will give you a block of full width and the height will be either 8 or 16 pixels. You can estimate it by `image_height // decoder.get_block_counts()`
  - `jpeg_data`: JPEG data to decode.

### Encoder

- `Encoder(format="RGB888", quality=90, rotation=0, width=320, height=240)`: Creates a new encoder object.
  - `format`: Pixel format for input image (`RGB565_BE`, `RGB565_LE`, `CbYCrY`, `RGB888`).
  - `quality`: JPEG quality (1-100).
  - `rotation`: Rotation angle for encoding (0, 90, 180, 270).
  - `width`: Width of the input image.
  - `height`: Height of the input image.

- `encode(img_data)`: Encodes the image data to JPEG format and returns the encoded JPEG data.
  - `img_data`: Raw image data to encode.

- `__del__()`: Cleans up resources used by the encoder.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
```

You may need to update the example file paths and ensure the correct imports as per your project setup.
