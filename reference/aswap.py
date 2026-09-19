#!/usr/bin/env python3
import sys, re

# min 1 arg
if len(sys.argv) < 2:
    print("===============================")
    print("=== Advanced swapper")
    print("=== by @etragardh")
    print("= Usage: ~# aswap lfr")
    print("= Usage: l = levels deep (1-9)")
    print("= Usage: f = find this")
    print("= Usage: r = replace with this")
    print("= Usage: Repeat as many times you like")
    print("= Example: ~# echo 'love' | aswap 2i! 1o0 3a@")
    print("===============================")
    exit()

# validate every rule before reading stdin
for argv in sys.argv[1:]:
    if len(argv) != 3 or not argv[0].isdigit():
        print(f"Invalid rule: {argv} (expected <levels><find><replace>, e.g. 2o0)", file=sys.stderr)
        exit(1)
    if argv[0] == "0":
        print(f"Invalid rule: {argv} (levels must be 1-9)", file=sys.stderr)
        exit(1)

def swap(word, index, char):
    return word[:index] + char + word[index+1:]

for i, argv in enumerate(sys.argv):
    if i == 0:
        continue
    elif i == 1:
        lines = sys.stdin.readlines()

    levels = int(argv[0])
    find = argv[1]
    replace = argv[2]

    lines_memory = []
    for line in lines:
        word = line.strip()

        words_memory = []
        words_memory.append(word)

        deep = 0
        for i, c in enumerate(word):
            if c == find:
                deep = deep +1
                tmp = words_memory.copy()
                for w in tmp:
                    words_memory.append(swap(w, i, replace))

                if deep >= levels:
                    break

        for wm in words_memory:
            lines_memory.append(wm)
    lines = lines_memory

print(*lines, sep='\n')
