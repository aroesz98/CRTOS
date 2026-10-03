# flashtest.sh - checks of the flash file system: sh /sd/crtos/share/flashfs/flashtest.sh
# (writes /flash0/test only; prints "ok N" or "FAIL N")
F=/flash0/test
rm -r -f $F ; mkdir -p $F/sub && echo ok 1 || echo FAIL 1
echo hello flash > $F/a.txt && cat $F/a.txt
test -f $F/a.txt && echo ok 2 || echo FAIL 2
cp /sd/crtos/bin/hello.app $F/hello.app && $F/hello.app one two && echo ok 3 || echo FAIL 3
time cp /sd/crtos/apps/netsurf.app $F/big.app
test -s $F/big.app && echo ok 4 || echo FAIL 4
crtos-app check $F/big.app && echo ok 5 || echo FAIL 5
mv $F/a.txt $F/sub/b.txt && cat $F/sub/b.txt && echo ok 6 || echo FAIL 6
echo second version > $F/sub/b.txt && cat $F/sub/b.txt | grep -c x > /dev/null ; cat $F/sub/b.txt
echo more >> $F/sub/b.txt 2> /dev/null && echo FAIL 7 || echo ok 7 append refused
rm $F/sub && echo FAIL 8 || echo ok 8 non-empty dir kept
ls -l $F $F/sub
df
flashfs info
flashfs ls /test
flashfs check && echo ok 9 || echo FAIL 9
rm $F/big.app && test ! -f $F/big.app && echo ok 10 || echo FAIL 10
echo done
