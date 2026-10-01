# Reading TIFF

`src/core/TiffImage.{h,cpp}` is this repository's TIFF reader, next to the
OpenEXR one (`docs/notes/exr.md`) and for the same reasons: libtiff would bring
its own zlib/jpeg/zstd/webp dependency tree for a format whose photographic
subset is small. Inflate is the vendored miniz.

TIFF matters for HDR work (issue #127): a raw developer's 16-bit export keeps
the highlight and shadow detail an 8-bit JPEG quantizes away, and a merged HDR
is often a 32-bit float TIFF.

## What it reads

- **Containers** -- classic TIFF and BigTIFF, either byte order. The first
  image in the file is the one read; later pages and SubIFDs are ignored.
- **Layout** -- strips and tiles, chunky and planar (`PlanarConfiguration` 2).
- **Samples** -- 8- and 16-bit unsigned, and 16-, 32- and 64-bit IEEE float.
  `decode()` hands back 8- and 16-bit as stored and every float width as
  float32, which is what the trainer's `UINT8` / `UINT16` / `FLOAT32` buffers
  take.
- **Compression** -- none, LZW, Deflate (both tag values) and PackBits, with
  the horizontal predictor (2) and Adobe's floating-point predictor (3).
- **Photometric** -- BlackIsZero and WhiteIsZero greyscale, and RGB. An extra
  sample is alpha when `ExtraSamples` says so (associated alpha is divided
  out); an unspecified extra channel is dropped.

Chunks decode on a worker pool, as EXR's do. `src/core/tests/tiff_decode.cpp`
checks the reader against files written by two independent encoders --
tifffile/imagecodecs and libtiff (through Pillow) -- across that matrix:

```bash
pip install tifffile imagecodecs pillow
python tools/gen_tiff_cases.py /tmp/tiff_cases
./build_vulkan/tiff_decode /tmp/tiff_cases
```

## What it refuses

Each fails with a sentence naming the problem: JPEG-, CCITT-, ZSTD-, LZMA-,
WebP-, JPEG 2000- and JPEG XL-compressed data (re-save with LZW or ZIP);
palette, CMYK, YCbCr and Lab images; signed or 32-bit integer and 24-bit float
samples; bit-reversed `FillOrder`; old-style (pre-1991) LZW; and DNG, which is
camera raw data and has to be developed first. A header that claims more pixels
than its file could hold is refused before anything is allocated.

## Colour space and metadata

A TIFF's colour space lives in an ICC profile, which is not read. Every reader
therefore treats a TIFF like a PNG: display-encoded Rec.709 unless the run says
otherwise (`--image-color-is-linear`, `--image-color-gamut`, and the SfM / SAM /
geometry equivalents). A float TIFF from a linear HDR merge needs
`--image-color-is-linear true`; values above 1 reach the trainer unclipped
either way.

EXIF comes from the file's own first directory (`sfm::readExif`), so the
exposure the `hdr` preset seeds PPISP with, the focal prior and GPS all work.
The `Orientation` tag is not honoured: no reader turns a TIFF's pixels, so the
trainer, SfM and the GUI agree on them as stored -- photo exporters write
`Orientation` 1 after rotating anyway.
