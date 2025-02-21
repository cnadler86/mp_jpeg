from time import ticks_ms
import jpeg
import gc

Nr = 10
with open(f'MyImage-320x240.jpg', mode="rb") as f:
        Img = f.read()
        
def decoder():
    gc.collect()
    for format in ('RGB565_BE','RGB565_LE','RGB888','CbYCrY'):
        print(f"Format: {format}")
        Decoder=jpeg.decoder(format=format,rotation=0)
        start = ticks_ms()
        for _ in range(Nr):
            ImgDec = Decoder.decode(Img)
        print(f"FPS normal decode: {Nr*1000/(ticks_ms()-start)}. Decoded image size: {len(ImgDec)}")
        
        Decoder=jpeg.decoder(format=format,rotation=0,block=True)
        
        start = ticks_ms()
        blocks = Decoder.get_block_counts(Img)
        for _ in range(Nr):
            for i in range(Decoder.get_block_counts(Img)):
                ImgDec = Decoder.decode(Img)
        print(f"FPS block decode ({blocks}): {Nr*1000/(ticks_ms()-start)}")

def encoder():
    Height = 240
    Width = 320
    for format in ('RGB565_BE','RGB565_LE','RGB888','CbYCrY'):
        print(f"Format: {format}")
        Decoder=jpeg.decoder(format=format,rotation=0)
        ImgDec=Decoder.decode(Img)
        Encoder=jpeg.encoder(format=format,quality=80,height=Height,width=Width)
        start = ticks_ms()
        for _ in range(Nr):
            ImgEnc = Ecoder.encode(Img)
        print(f"FPS normal decode: {Nr*1000/(ticks_ms()-start)}")
        gc.collect()
    
decoder()
encoder()

