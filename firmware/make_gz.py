import gzip

with open('src/main.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

start_marker = 'INDEX_HTML[] PROGMEM = R"rawhtml('
start = content.find(start_marker) + len(start_marker)
end = content.find(')rawhtml";', start)
html = content[start:end]
print(f"Original length: {len(html)} bytes")

gz = gzip.compress(html.encode('utf-8'), compresslevel=9)
print(f"Gzipped length: {len(gz)} bytes")

# Also format as C byte array
hex_bytes = [f"0x{b:02x}" for b in gz]
chunks = [", ".join(hex_bytes[i:i+16]) for i in range(0, len(hex_bytes), 16)]
c_array = "const uint8_t INDEX_HTML_GZ[] PROGMEM = {\n  " + ",\n  ".join(chunks) + "\n};\n"
c_array += f"const size_t INDEX_HTML_GZ_LEN = {len(gz)};\n"

with open('src/html_gz.h', 'w', encoding='utf-8') as f:
    f.write(c_array)

print("Saved src/html_gz.h!")
