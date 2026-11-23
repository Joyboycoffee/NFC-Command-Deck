import gzip

with open('src/nfc_portal.h', 'r', encoding='utf-8') as f:
    content = f.read()

start_tag = 'R"rawliteral('
end_tag = ')rawliteral"'

start = content.find(start_tag) + len(start_tag)
end = content.find(end_tag)
html = content[start:end]

gz = gzip.compress(html.encode('utf-8'))
print(f"Original size: {len(html)} bytes | Gzipped size: {len(gz)} bytes")

with open('src/nfc_portal_gz.h', 'w') as out:
    out.write('#pragma once\n#include <Arduino.h>\n\n')
    out.write(f'const uint32_t NFC_PORTAL_GZ_LEN = {len(gz)};\n')
    out.write('const uint8_t NFC_PORTAL_GZ[] PROGMEM = {\n')
    for i, b in enumerate(gz):
        out.write(f'0x{b:02x},')
        if (i + 1) % 16 == 0:
            out.write('\n')
    out.write('\n};\n')

print("Created src/nfc_portal_gz.h successfully!")
