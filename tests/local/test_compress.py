#!/usr/bin/env python3
"""Test zlib compression / decompression standalone."""
import zlib

# Simulate what our module does: compress "hello-tunnel" with raw deflate + Z_SYNC_FLUSH
data = b"hello-tunnel"
print(f"Input: {len(data)} bytes: {data!r}")

# Compress like the module does: raw deflate, Z_SYNC_FLUSH, strip 0x0000FFFF
compressor = zlib.compressobj(zlib.Z_DEFAULT_COMPRESSION, zlib.DEFLATED, -zlib.MAX_WBITS)
result = compressor.compress(data)
result += compressor.flush(zlib.Z_SYNC_FLUSH)
print(f"Raw deflate output: {len(result)} bytes: {result.hex()}")

# Strip SYNC_FLUSH marker per RFC 7692 (00 00 FF FF)
if len(result) >= 4 and result[-4:] == b'\x00\x00\xff\xff':
    stripped = result[:-4]
    print(f"After stripping sync marker: {len(stripped)} bytes: {stripped.hex()}")
else:
    stripped = result
    print(f"No sync marker found, using full output")

# Now decompress the stripped data
decompressor = zlib.decompressobj(-zlib.MAX_WBITS)
try:
    decompressed = decompressor.decompress(stripped)
    decompressed += decompressor.flush()
    print(f"Decompressed: {decompressed}")
    assert decompressed == data, "Mismatch!"
    print("SUCCESS: roundtrip works!")
except Exception as e:
    print(f"Decompress FAILED: {e}")
    
    # Try with trailing 00 00
    for suffix in [b'\x00\x00\xff\xff', b'\x00\x00', b'\x00']:
        try:
            d2 = zlib.decompressobj(-zlib.MAX_WBITS)
            r2 = d2.decompress(stripped + suffix)
            r2 += d2.flush()
            print(f"With suffix {suffix.hex()}: {r2}")
            break
        except Exception as e2:
            print(f"Suffix {suffix.hex()} also failed: {e2}")
