# aswap
Advanced swapper<br />
For advanced rule based password cracking when swapping characters is a must.<br />

>[!CAUTION]
> Be careful when using aswap.<br />
> You could accidently gain access to some one elses password ;-)

## Syntax
aswap takes input from stdin.<br />
`cat dict.txt | aswap <int:levels deep><void:find><void:replacement>`<br />
ie<br />
`echo "tester" | aswap 1e3`

A rule is exactly three characters. Levels is how many occurrences of the find
character to branch on, and must be 1-9 — `0e3` is rejected rather than quietly
treated as some other depth.

outputs:
```
tester
t3ster
```

while
`echo "tester" | aswap 2e3`

outputs:
```
tester
test3r
t3ster
t3st3r
```

## Installation

```
gh repo clone etragardh/aswap
cd aswap
make
sudo make install
```

`make` compiles `aswap.cpp` with `-march=native`, so the binary is optimized for
your machine. `sudo make install` symlinks it into `/usr/local/bin`
(override with `make install PREFIX=~/.local`). `make uninstall` removes it.

**Test it out**
```
echo "kitties" | aswap 2i! 2e3
```

should output _all_ possible combinations:
```
kitties
kitti3s
kitt!es
kitt!3s
k!tties
k!tti3s
k!tt!es
k!tt!3s
```

The candidates for one input word always come out in that order. Across a whole
wordlist the words are spread over every core, so which word's block lands first
varies between runs — the set is identical, the order is not. Pipe through
`sort` if you need a stable file.

## Piping to a cracker

By default aswap writes each candidate as soon as it is produced, so the thing
on the other end of the pipe starts working immediately instead of waiting for
aswap to finish:

```
cat rockyou.txt | aswap 2o0 2a@ 2e3 | hashcat -a 0 -m 0 hashes.txt
```

hashcat cracks on the GPU while aswap keeps swapping on the CPU. Memory stays
flat no matter how long the run is — aswap never holds more than the candidates
for a single word.

## `--unique`

Two *different* input words can produce the same candidate, but only if the
wordlist already contains leet variants: with `1o0`, both `love` and `l0ve`
produce `l0ve`. `--unique` removes those.

It is not the default because it is rarely worth what it costs. On
`/usr/share/dict/words` (234k words) expanded to 11.3M candidates:

| | time | peak RSS | duplicates removed |
|---|---|---|---|
| default | 0.07s | 8 MB | — |
| `--unique` | 5.43s | 1222 MB | **0** |

Nothing to remove, because that wordlist holds no leet variants. `--unique` has
to buffer every candidate and read all input before it can write anything, so
reach for it only when your wordlist really does mix `love` and `l0ve` — there
it removes up to half the output.

## aswap vs Hashcat and John The Ripper

**impossible combination in hashcat and john**
```
echo "kitties" | hashcat -a 0 -j 'si! se3' --stdout
```
Outputs only:
`k!tt!3s`

Try to swap only the first occurance of "i".<br />
Can you get `kitties` to output `k!tties` in hashcat or john?

**Complexity = cracking time**

Try this:
```
echo ':' >> test.rule
echo 'si!' >> test.rule
echo 'si1' >> test.rule
echo 'so0' >> test.rule
echo 'sa@' >> test.rule
echo 'sa4' >> test.rule
echo 'se3' >> test.rule
echo 'st7' >> test.rule
echo 'love' | hashcat -a 0 -r test.rule --stdout
```

Output:
```
love <- original
love <- wtf
love <- wtf
l0ve <- swapped o for 0
love <- wtf
love <- wtf
lov3 <- swapped e for 3
love <- wtf
```
Compared with this:

```
echo "love" | aswap 2i! 2i1 2o0 2a@ 2a4 2e3 2t7
```
Output:
```
love <- original
lov3 <- e for 3
l0ve <- o for 0
l0v3 <- combination of the 2 previuous swaps
```

>[!caution]
> Note that `l0v3` is not present in hashcat or john. That would need _a lot_ more rules and add _a lot_ more complexity to the cracking process.

## Repo layout

```
aswap.cpp             the tool
reference/aswap.py    the original Python implementation
tests/equivalence.sh  checks the C++ build against the reference
```

`reference/aswap.py` is the implementation aswap started as, kept because it
defines the semantics: it is short enough to read in one sitting, and the test
suite asserts that the C++ build produces the same set of candidates and rejects
the same rules. Run it directly if you want aswap without a compiler.

```
make test
```

It is the original apart from rule validation, which was added to both at the
same time so the two cannot drift on what counts as a valid rule.

Two differences from the reference are deliberate and are asserted by the tests:
the C++ build drops blank input lines, and it dedups (per input word by default,
across the whole run with `--unique`) where the reference never does.

## History

aswap started as the Python implementation in `reference/aswap.py` and was
rewritten in C++ to get it fast enough to feed a GPU. On 200k words with
`3o0 2a@ 2e3` (1M candidates) the original takes 1.3s and aswap 0.01s, and it
uses every core.

Two Python experiments along the way are no longer in the repo but are worth
recording. Streaming stdin line by line instead of reading it all up front cut
memory from 118 MB to 16 MB — that idea is what the default mode is built on.
Fanning lines out over `multiprocessing.Pool` gave about 2.7x wall clock, which
is what convinced me the work was worth parallelizing properly.

## License

GPL-3.0. See [LICENSE](LICENSE).
