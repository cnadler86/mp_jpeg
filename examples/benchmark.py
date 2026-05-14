import sys
import gc

try:
    from time import ticks_ms, ticks_diff
except ImportError:
    import time

    def ticks_ms():
        return int(time.perf_counter() * 1000)

    def ticks_diff(a, b):
        return a - b

try:
    import jpeg
except ImportError:
    jpeg = None

DEFAULT_IMAGE_PATH = "000.jpeg"


def _bpp(pixel_format):
    if pixel_format in ("RGB565_BE", "RGB565_LE", "CbYCrY", "YCbYCr", "YCbY2YCrY2"):
        return 2
    if pixel_format in ("RGB888", "RGBA"):
        return 3 if pixel_format == "RGB888" else 4
    if pixel_format == "GRAY":
        return 1
    return 2


def _make_decoder(pixel_format, rotation, block):
    try:
        return jpeg.Decoder(pixel_format=pixel_format, rotation=rotation, block=block)
    except TypeError:
        return jpeg.Decoder(format=pixel_format, rotation=rotation, block=block)


def _make_encoder(height, width, pixel_format, quality, rotation):
    try:
        return jpeg.Encoder(height=height, width=width, pixel_format=pixel_format, quality=quality, rotation=rotation)
    except TypeError:
        return jpeg.Encoder(height=height, width=width, format=pixel_format, quality=quality, rotation=rotation)


def _read_file(path):
    with open(path, "rb") as f:
        return f.read()


def _pick_image_path(argv):
    if argv:
        for a in argv:
            if a and not a.startswith("-"):
                return a
    for p in ("image.jpg", "test.jpg", "bigbuckbunny-320x240.jpg", "bigbuckbunny-160x160.jpg"):
        try:
            open(p, "rb").close()
            return p
        except OSError:
            pass
    return None


def _parse_int_arg(argv, name, default):
    prefix = "--%s=" % name
    for a in argv:
        if a.startswith(prefix):
            try:
                return int(a[len(prefix) :])
            except ValueError:
                return default
    return default


def _parse_str_arg(argv, name, default):
    prefix = "--%s=" % name
    for a in argv:
        if a.startswith(prefix):
            return a[len(prefix) :]
    return default


def _has_flag(argv, name):
    return ("--%s" % name) in argv


def _fps(frames, start_ms, end_ms):
    dt = ticks_diff(end_ms, start_ms)
    if dt <= 0:
        return 0.0
    return frames * 1000.0 / dt


def print_image_info(jpeg_data, formats, rotation):
    fmt0 = formats[0] if formats else "RGB565_LE"

    gc.collect()
    dec = _make_decoder(fmt0, rotation, False)
    info = dec.get_img_info(jpeg_data)
    w, h = info[0], info[1]

    blocks = 0
    block_h = 0
    try:
        gc.collect()
        decb = _make_decoder(fmt0, 0, True)
        info_b = decb.get_img_info(jpeg_data)
        if len(info_b) >= 4:
            blocks = info_b[2]
            block_h = info_b[3]
    except Exception:
        pass

    print("Image Width: %d" % w)
    print("Image Height: %d" % h)
    print("Rotation: %d" % rotation)
    if blocks and block_h:
        print("Blocks: %d" % blocks)
        print("Block Height: %d" % block_h)

    if hasattr(gc, "mem_free"):
        try:
            print("GC mem_free: %d" % gc.mem_free())
            print("GC mem_alloc: %d" % gc.mem_alloc())
        except Exception:
            pass

    for fmt in formats:
        bpp = _bpp(fmt)
        frame_bytes = w * h * bpp
        if block_h:
            block_bytes = w * block_h * bpp
            print("Format %s: bpp=%d framebuffer=%d bytes block=%d bytes" % (fmt, bpp, frame_bytes, block_bytes))
        else:
            print("Format %s: bpp=%d framebuffer=%d bytes" % (fmt, bpp, frame_bytes))


def bench_decoder(jpeg_data, nr, formats, rotation):
    print("Decoder Benchmark")
    print("Input Image Size: %d bytes" % len(jpeg_data))

    for fmt in formats:
        print("\nFormat: %s" % fmt)
        bpp = _bpp(fmt)

        gc.collect()
        dec = _make_decoder(fmt, rotation, False)
        start = ticks_ms()
        for _ in range(nr):
            _ = dec.decode(jpeg_data)
        end = ticks_ms()
        print("FPS normal decode: %.2f" % _fps(nr, start, end))

        gc.collect()
        dec = _make_decoder(fmt, 0, True)
        info = dec.get_img_info(jpeg_data)
        w, h, blocks = info[0], info[1], info[2]

        start = ticks_ms()
        for _ in range(nr):
            for _i in range(blocks):
                _ = dec.decode(jpeg_data)
        end = ticks_ms()
        print("FPS block decode (%d): %.2f" % (blocks, _fps(nr, start, end)))

        gc.collect()
        dec = _make_decoder(fmt, 0, True)
        info = dec.get_img_info(jpeg_data)
        w, h, blocks = info[0], info[1], info[2]
        fb = bytearray(w * h * bpp)
        start = ticks_ms()
        for _ in range(nr):
            for i in range(blocks):
                block = dec.decode(jpeg_data)
                off = i * len(block)
                fb[off : off + len(block)] = block
        end = ticks_ms()
        print("FPS block decode + write (slice) (%d): %.2f" % (blocks, _fps(nr, start, end)))

        gc.collect()
        dec = _make_decoder(fmt, 0, True)
        info = dec.get_img_info(jpeg_data)
        w, h, blocks = info[0], info[1], info[2]
        fb = bytearray(w * h * bpp)
        start = ticks_ms()
        for _ in range(nr):
            for _i in range(blocks):
                _ = dec.decode_into(jpeg_data, fb, blocks=1)
        end = ticks_ms()
        print("FPS decode_into step (blocks=1): %.2f" % _fps(nr, start, end))

        gc.collect()
        dec = _make_decoder(fmt, 0, True)
        info = dec.get_img_info(jpeg_data)
        w, h = info[0], info[1]
        fb = bytearray(w * h * bpp)
        start = ticks_ms()
        for _ in range(nr):
            _ = dec.decode_into(jpeg_data, fb)
        end = ticks_ms()
        print("FPS decode_into full (blocks=0): %.2f" % _fps(nr, start, end))


def bench_encoder(jpeg_data, nr, quality_list, rotation):
    print("\nEncoder Benchmark")

    gc.collect()
    dec = _make_decoder("RGB888", rotation, False)
    info = dec.get_img_info(jpeg_data)
    w, h = info[0], info[1]
    img_raw = bytes(dec.decode(jpeg_data))

    for q in quality_list:
        gc.collect()
        enc = _make_encoder(h, w, "RGB888", q, rotation)
        start = ticks_ms()
        for _ in range(nr):
            _ = enc.encode(img_raw)
        end = ticks_ms()
        print("FPS encode quality %d: %.2f" % (q, _fps(nr, start, end)))


def main():
    argv = sys.argv[1:]

    if _has_flag(argv, "help") or _has_flag(argv, "h"):
        print(
            "Usage: benchmark.py [image.jpg] [--image=path] [--nr=N] [--rotation=0|90|180|270] "
            "[--formats=CSV] [--qualities=CSV] [--no-encoder]"
        )
        return 0

    if jpeg is None:
        print("jpeg module not found (this benchmark is for MicroPython build with mp_jpeg).")
        return 2

    img_path = _parse_str_arg(argv, "image", "")
    if not img_path:
        img_path = _pick_image_path(argv)
    if not img_path:
        img_path = DEFAULT_IMAGE_PATH
    try:
        open(img_path, "rb").close()
    except OSError:
        print("No JPEG file found. Edit DEFAULT_IMAGE_PATH in benchmark.py or pass: benchmark.py image.jpg")
        return 2

    nr = _parse_int_arg(argv, "nr", 100)
    rotation = _parse_int_arg(argv, "rotation", 0)

    fmt_csv = _parse_str_arg(argv, "formats", "RGB565_BE,RGB565_LE,RGB888,CbYCrY")
    formats = [s.strip() for s in fmt_csv.split(",") if s.strip()]
    if not formats:
        formats = ["RGB565_LE"]

    q_csv = _parse_str_arg(argv, "qualities", "100,90,80,70,60")
    qualities = []
    for s in q_csv.split(","):
        s = s.strip()
        if not s:
            continue
        try:
            qualities.append(int(s))
        except ValueError:
            pass
    if not qualities:
        qualities = [90]

    jpeg_data = _read_file(img_path)

    print("JPEG Driver Version: %s" % jpeg.version())
    print("Image: %s" % img_path)
    print("NR: %d" % nr)
    print_image_info(jpeg_data, formats, rotation)

    bench_decoder(jpeg_data, nr, formats, rotation)

    if not _has_flag(argv, "no-encoder"):
        bench_encoder(jpeg_data, nr, qualities, rotation)

    return 0


raise SystemExit(main())
