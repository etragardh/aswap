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



## Usage

**Installation (original in python)**

```
gh repo clone etragardh/aswap
chmod +x aswap/aswap
sudo ln -s ${PWD}/aswap/aswap /usr/local/bin
```

**Installation (optimized in c++, multicore)**

This will delete the aswap python version and compile the aswap optimized for your machine.<br />
`aswap_cpp` is a pre compiled binary optimized for my Macbook PRO M2 Ultra.<br />
You should compile aswap.cpp your self.
<br />

```
gh repo clone etragardh/aswap
cd aswap
rm aswap
g++ -std=c++17 -O3 -flto -march=native -DNDEBUG -o aswap_cpp aswap.cpp -pthread
chmod +x aswap
sudo ln -s ${PWD}/aswap /usr/local/bin
```
If you pipe the C++ version to hashcat it will run on the available CPU at the same time hashcat runs on your GPU. This way Hashcat can start cracking before aswap finnishes swapping.

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
> Note that `l0v3` is not present in hashcat or john. That would need _a lot_ more rules and add _a lot_ more complexity to the cracking process.
