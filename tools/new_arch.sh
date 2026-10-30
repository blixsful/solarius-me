#!/bin/sh
set -e
if [ $# -ne 1 ]; then
    echo "usage: tools/new_arch.sh NAME   (letters/digits, starts with a letter)" >&2
    exit 2
fi
name="$1"
case "$name" in
    [a-z]*) ;;
    *) echo "name must start with a lowercase letter" >&2; exit 2 ;;
esac
root=$(cd "$(dirname "$0")/.." && pwd)
dest="$root/arch/$name"
if [ -e "$dest" ]; then
    echo "$dest already exists" >&2
    exit 1
fi
class=$(printf '%s' "$name" | sed 's/^./\U&/')
mkdir -p "$dest"
tpl="$root/tools/arch_template"
render() {
    sed -e "s/__NAME__/$name/g" -e "s/__CLASS__/$class/g" "$1" > "$2"
}
render "$tpl/cpu.hpp.in" "$dest/cpu_$name.hpp"
render "$tpl/cpu.cpp.in" "$dest/cpu_$name.cpp"
render "$tpl/machine.cpp.in" "$dest/machine_$name.cpp"
render "$tpl/NOTES.md.in" "$dest/NOTES.md"
echo "created $dest"
echo "next: make && ./solme-$name --list-machines"
