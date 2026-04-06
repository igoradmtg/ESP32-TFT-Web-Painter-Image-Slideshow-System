# save_as: gz_to_h.py
# Использование: python gz_to_h.py [input_file.h] [output_file_gz.h]
# По умолчанию: 
#   input_file = "index_html.h"
#   output_file = "index_html_gz.h"

import sys
import gzip
import os

def compress_and_convert(input_path, output_path):
    # Читаем исходный файл в бинарном режиме
    with open(input_path, 'rb') as f:
        html_data = f.read()
    
    # Сжимаем данные с помощью gzip
    gz_data = gzip.compress(html_data, compresslevel=9)
    
    # Имя массива и переменной
    array_name = os.path.splitext(os.path.basename(output_path))[0]
    size_name = f"{array_name}_size"

    # Формируем C-код
    lines = []
    lines.append("// Auto-generated from gzipped content")
    lines.append("// DO NOT EDIT MANUALLY")
    lines.append(f"#pragma once")
    lines.append(f"#include <Arduino.h>")
    lines.append(f"const uint8_t {array_name}[] PROGMEM = {{")
    
    # Разбиваем на строки по 16 байт
    for i in range(0, len(gz_data), 16):
        chunk = gz_data[i:i+16]
        hex_bytes = ', '.join(f'0x{b:02X}' for b in chunk)
        lines.append("  " + hex_bytes + (',' if i + 16 < len(gz_data) else ''))
    
    lines.append("};")
    lines.append(f"const unsigned int {size_name} = {len(gz_data)};")
    lines.append("")

    # Записываем результат
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))

    print(f"Успешно создано: {output_path}")
    print(f"  Исходный размер: {len(html_data)} байт")
    print(f"  Сжатый размер: {len(gz_data)} байт ({len(gz_data)/len(html_data)*100:.1f}%)")
    print(f"  Включайте в код: #include \"{output_path}\"")

if __name__ == "__main__":
    # Устанавливаем значения по умолчанию
    input_file = "index_html.html"
    output_file = "index_html_gz.h"
    
    # Обрабатываем аргументы командной строки
    if len(sys.argv) > 1:
        input_file = sys.argv[1]
        if len(sys.argv) > 2:
            output_file = sys.argv[2]
    
    # Проверяем существование входного файла
    if not os.path.exists(input_file):
        print(f"Ошибка: файл '{input_file}' не найден!", file=sys.stderr)
        print("Использование: python gz_to_h.py [input_file.h] [output_file_gz.h]", file=sys.stderr)
        sys.exit(1)
    
    compress_and_convert(input_file, output_file)