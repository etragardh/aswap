# aswap
Advanced swapper - password cracking

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
```
echo 'si!' >> test.rule
echo 'si1' >> test.rule
echo 'so0' >> test.rule
echo 'sa@' >> test.rule
echo 'sa4' >> test.rule
echo 'se3' >> test.rule
echo 'st7' >> test.rule
echo 'love' | hashcat -a 0 -r test.rule --stdout
```
