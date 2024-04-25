# aswap
Advanced swapper<br />
For advanced rule based password cracking when swapping characters is a must.<br />

## Syntax
aswap takes input from stdin.<br />
`cat dict.txt | aswap <int:levels deep><void:character to swap><void:replacement>`<br />
ie<br />
`echo "tester" | aswap 1e3`

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
t3ster
t3st3r
```


>[!CAUTION]
> Be careful when using aswap.<br />
> You could accidently gain access to some one elses password ;-) 

## Usage

**Installation**

```
gh repo clone etragardh/aswap
chmod +x aswap/aswap
sudo ln -s ${PWD}/aswap/aswap /usr/local/bin
```

**Test it out**
```
echo "kitties" | aswap 2i! 2e3
```

should output _all_ possible combinations:
```
kitties
kitti3s
k!tties
k!tti3s
kitt!es
kitt!3s
k!tt!es
k!tt!3s
```

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
> Note that `l0v3` is not present in hashcat or john. That would need more complex rules.
