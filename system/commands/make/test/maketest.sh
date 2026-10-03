# maketest.sh - checks of make: sh /sd/crtos/share/make/maketest.sh
# Every check prints "ok N" or "FAIL N"; files in /ram/maketest.
T=/ram/maketest
rm -r -f $T ; mkdir -p $T ; cd $T
cp /sd/crtos/share/make/Makefile Makefile
echo alpha > a.in
echo beta > b.in
make > log1.txt ; test $? -eq 0 && echo ok 1 || echo FAIL 1
cat log1.txt
test -s joined.txt && echo ok 2 || echo FAIL 2
make > log2.txt
cat log2.txt
test -s log2.txt && echo "(make said:)" ; echo ok 3
sleep 2.5
touch b.in
make > log3.txt ; cat log3.txt
make clean > /dev/null ; test -f joined.txt && echo FAIL 5 || echo ok 5
cd / ; rm -r -f $T
echo done
