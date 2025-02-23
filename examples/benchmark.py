from time import ticks_ms
import jpeg
import gc

Nr = 100
Height = 240
Width = 320
with open(f'bigbuckbunny-320x240.jpg', mode="rb") as f:
    Img = f.read()

def decoder():
    gc.collect()
    print("Decoder Benchmark")
    print(f"Input Image Size: {len(Img)} bytes")
    for format in ('RGB565_BE', 'RGB565_LE', 'RGB888', 'CbYCrY'):
        print(f"\nFormat: {format}")
        Decoder = jpeg.decoder(format=format, rotation=0)
        start = ticks_ms()
        for _ in range(Nr):
            ImgDec = Decoder.decode(Img)
        print(f"FPS normal decode: {Nr * 1000 / (ticks_ms() - start):.2f}")
        
        Decoder = jpeg.decoder(format=format, rotation=0, block=True)
        start = ticks_ms()
        blocks = Decoder.get_block_counts(Img)
        for _ in range(Nr):
            for i in range(blocks):
                ImgDec = Decoder.decode(Img)
        print(f"FPS block decode ({blocks}): {Nr * 1000 / (ticks_ms() - start):.2f}")

def encoder():
    print("\nEncoder Benchmark")
    for quality in (100, 90, 80, 70, 60):
        print(f"\nQuality: {quality}")
        Decoder = jpeg.decoder(format='RGB888', rotation=0)
        ImgDec = bytes(Decoder.decode(Img))
        del Decoder
        gc.collect()
        Encoder = jpeg.encoder(format='RGB888', quality=quality, height=Height, width=Width)
        start = ticks_ms()
        for _ in range(Nr):
            ImgEnc = Encoder.encode(ImgDec)
        print(f"FPS encode quality {quality}: {Nr * 1000 / (ticks_ms() - start):.2f}")
        gc.collect()

decoder()
encoder()
