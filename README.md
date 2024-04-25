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

## Compared to hashcat or John The Ripper
```
echo "kitties" | hashcat -a 0 -j 'si! se3' --stdout
```
Outputs only:
`k!tt!3s`

**impossible combination in hashcat and john**

Try to swap only the first occurance of "i".
Can you get `kitties` to output `k!tties` ?
