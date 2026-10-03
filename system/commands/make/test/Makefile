# Makefile of maketest.sh (runs in a copy under /ram/maketest)
.POSIX:

all: joined.txt

joined.txt: a.out b.out
	cat a.out b.out > $@
	echo made $@ from $^

.SUFFIXES: .in .out
.in.out:
	cp $< $@
	echo converted $<

clean:
	rm -f a.out b.out joined.txt
