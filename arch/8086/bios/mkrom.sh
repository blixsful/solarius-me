#!/bin/sh
set -e
src="$1"
out="$2"
tmp=$(mktemp -d)
as --32 -o "$tmp/bios.o" "$src"
ld -m elf_i386 -Ttext=0xE000 --oformat binary -o "$tmp/bios.bin" "$tmp/bios.o"
size=$(wc -c < "$tmp/bios.bin")
{
    echo "#pragma once"
    echo "#include <cstddef>"
    echo "#include <cstdint>"
    echo "namespace solme {"
    echo "constexpr size_t kSolBiosRomSize = $size;"
    echo "constexpr uint8_t kSolBiosRom[] = {"
    od -An -v -tu1 "$tmp/bios.bin" | awk '{ line=""; for (i=1;i<=NF;i++) line = line $i ","; print line }'
    echo "};"
    echo "}"
} > "$out"
rm -rf "$tmp"
