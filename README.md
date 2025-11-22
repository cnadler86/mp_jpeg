# MicroPython JPEG

A very fast and memory-efficient micropython jpeg decoder and encoder. At the moment only the esp port is supported.

If you are not familiar with building custom firmware, visit the [releases](https://github.com/cnadler86/mp_jpeg/releases) page to download firmware that suits your board!

To get the version of the low level driver, you can use the following code:

```python
import jpeg
print("JPEG Driver Version:", jpeg.version())
```

## Decoder

### API Reference

- `Decoder(format="RGB888", rotation=0, block=False)`: Creates a new decoder object.
  - `pixel_format`: Pixel format for output (`RGB565_BE`, `RGB565_LE`, `CbYCrY`, `RGB888`).
  - `rotation`: Rotation angle for decoding (0, 90, 180, 270). Default 0.
  - `block`: Enable block decoding (default: `False`). Each time decode is called, it outputs 8 or 16 line data depending on the image and output format (see `get_block_counts`). If enabled, scale, clipper and rotation are not supported.
  - `scale_width` and `scale_height`: Resize the output image to the prvided scale. Note: the scale needs to be consistent with the input image and be a multiple of 8.
  - `clipper_width` and `clipper_height`: This will cut the output image to the specified width and/or height. The clipper_height and clipper_width require integer multiples of 8. The resolution of clipper should be less or equal than scale.
  - `return_bytes`: if true, the decoder return a bytes-object, otherwise a memoryview, default is false.

- `get_img_info(jpeg_data)`: Returns a list containing the width and height. If the decoder was constructed with `block` enabled, then it will also return the number of blocks that the decoder will need to decode the full image and the heigh of each block (eihter 8 or 16). 
  - `jpeg_data`: JPEG data to decode.

- `decode(jpeg_data)`: Decodes the JPEG image and returns the decoded image. With block==True, it decodes the next block of the JPEG image and returns the decoded block. The decoder will give you a block of full width and the height will be either 8 or 16 pixels. You can get the block height by `get_img_info`
  - `jpeg_data`: JPEG data to decode.

### Example

```python
import jpeg

# Create a JPEG decoder object
decoder = jpeg.Decoder(rotation=180, pixel_format="RGB888")

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
  - `pixel_format`: Pixel format for input image (RGB888, RGB565, RGBA, YCbYCr, YCbY2YCrY2, CbYCrY, GRAY). Required.
  - `quality`: JPEG quality (1-100). Default 90.
  - `rotation`: Rotation angle for encoding (0, 90, 180, 270). Default 0.

- `encode(img_data)`: Encodes the image data to JPEG format and returns the encoded JPEG data.
  - `img_data`: Raw image data to encode.
  - Returns the bytes of the encoded image.

### Example

```python
import jpeg

# Create a JPEG encoder object
encoder = jpeg.Encoder(pixel_format="RGB888", quality=80, rotation=90, width=320, height=240)

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

### Build the project

### Setting up the build environment

To build the project, follow these instructions:

- ESP-IDF: I tested it on version 5.2, 5.3 and 5.4, but it might work with other versions.
- Clone the micropython repo and this repo in a folder, e.g. "MyJPEG". I used MicroPython version 1.24 but might work also with older versions.
- You will have to add the ESP JPEG library (I used v0.6.1). To do this, add the following to the dependencies in the respective idf_component.yml file (e.g. in micropython/ports/esp32/main_esp32s3/idf_component.yml):

```yaml
espressif/esp_new_jpeg: "^1.0.0"
```

Alternatively, you can [download the library from the espressif component registry](https://components.espressif.com/components/espressif/esp_new_jpeg/versions/0.6.1?language=en) and unzip the data inside the esp-idf/components folder instead of altering the idf_component.yml file. In this case you might need to rename the folder to "esp_new_jpeg".

### Build the user module

To build the project, you could do it the following way:

```bash
. <path2esp-idf>/esp-idf/export.sh
cd MyJPEG/micropython/ports/esp32
make USER_C_MODULES=../../../../mp_jpeg/micropython.cmake BOARD=<Your-Board> clean
make USER_C_MODULES=../../../../mp_jpeg/micropython.cmake BOARD=<Your-Board> submodules
make USER_C_MODULES=../../../../mp_jpeg/micropython.cmake BOARD=<Your-Board> all
```

You can also pass the MP_JPEG_DIR variable to point to the esp_new_jpeg component folder. in ths case you have to build usinf the idf directly.

Micropython and mp_jpeg folders are at the same level. Note that you need those extra "/../"s while been inside the esp32 port folder. If you experience problems, visit [MicroPython external C modules](https://docs.micropython.org/en/latest/develop/cmodules.html).
