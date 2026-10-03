# shtest.sh - checks of the shell: sh /sd/crtos/share/sh/shtest.sh
# Every check prints "ok N" or "FAIL N ..."; the last line counts the failures.
T=/ram/shtest
rm $T/* 2>/dev/null ; mkdir $T
cd $T

# 1 variables and quoting
A=hello
B="two words"
test "$A" = hello && echo ok 1 || echo FAIL 1
test "$B" = "two words" && echo ok 2 || echo FAIL 2
test '$A' = '$A' && echo ok 3 || echo FAIL 3
test "${A}x" = hellox && echo ok 4 || echo FAIL 4

# 2 exit codes, && ||
false && echo FAIL 5 || echo ok 5
true || echo FAIL 6 ; echo ok 6
false ; test $? -eq 1 && echo ok 7 || echo FAIL 7
false && echo FAIL 8a || echo ok 8

# 3 redirections
echo first > out.txt
echo second >> out.txt
cat out.txt > copy.txt
test -s copy.txt && echo ok 9 || echo FAIL 9
/sd/crtos/bin/apptest.app echo "program to file" > prog.txt
cat prog.txt | /sd/crtos/bin/apptest.app exit 0 && echo ok 10 || echo FAIL 10
/sd/crtos/bin/apptest.app echo both > both.txt 2>&1 && test -s both.txt && echo ok 11 || echo FAIL 11
nosuchprogram 2> err.txt ; test $? -eq 127 && echo ok 12 || echo FAIL 12

# 4 pipes
echo through | cat > piped.txt
test -s piped.txt && echo ok 13 || echo FAIL 13
/sd/crtos/bin/apptest.app echo pipe-from-program | cat > p2.txt
test -s p2.txt && echo ok 14 || echo FAIL 14

# 5 file name patterns
touch a1.c a2.c b.h
echo *.c > glob.txt
cat glob.txt
echo [ab]*.? > glob2.txt
cat glob2.txt
echo nomatch*.zz

# 6 scripts with arguments, sourcing
echo 'echo args $# first $1 all $@' > s.sh
sh s.sh one two three
echo 'X=from-source' > src.sh
. ./src.sh
test "$X" = from-source && echo ok 15 || echo FAIL 15
echo 'exit 3' > e.sh
sh e.sh ; test $? -eq 3 && echo ok 16 || echo FAIL 16

# 7 set -e in a script
echo 'set -e' > se.sh
echo 'echo before' >> se.sh
echo 'false' >> se.sh
echo 'echo FAIL 17 not stopped' >> se.sh
sh se.sh ; echo ok 17 status $?

# 8 environment of one command, export, unset
VAR=only-here /sd/crtos/bin/apptest.app exit 0
test -z "$VAR" && echo ok 18 || echo FAIL 18
export E1=exported
unset A
test -z "$A" && echo ok 19 || echo FAIL 19
which apptest

# 9 background and wait
/sd/crtos/bin/apptest.app exit 4 &
wait ; echo ok 20 wait $?

cd /
rm $T/*
rm $T
echo done
