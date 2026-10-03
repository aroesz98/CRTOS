# Powłoka, edytor i make

Na płytce działa powłoka `sh`: na konsoli szeregowej, w oknie **Terminal** (`term`) i przez
USB (`getty`). Do pracy z plikami tekstowymi jest edytor `edit`, a do budowania według
`Makefile` jest `make`. Gdzie leży program, mówi `where`.

## Powłoka sh

### Polecenia i programy

`help` wypisuje polecenia wbudowane:
- pliki: `ls [-l]`, `cat`, `cp`, `mv`, `rm [-f] [-r]`, `mkdir [-p]`, `touch`, `cd`, `pwd`;
- procesy: `ps`, `kill`, `jobs`, `wait`, `time`;
- system: `free`, `uptime`, `date`, `insmod`, `rmmod`, `reboot`;
- zmienne i skrypty: `export`, `unset`, `env`, `set`, `test`/`[`, `true`, `false`, `which`,
  `.`/`source`, `exit`;
- terminal: `clear` (czyści ekran i przewijaną historię okna), `history` (wpisane polecenia,
  `history -c` je zapomina).

`which nazwa` mówi, co uruchomi powłoka: polecenie wbudowane („built into the shell”) albo
pierwszy pasujący program z `$PATH`. Wszystkie pasujące programy pokazuje `where` (niżej).

Każda inna nazwa uruchamia program. Powłoka szuka go w katalogach z `$PATH` najpierw jako
`nazwa.app`, potem jako `nazwa`. Nazwa ze znakiem `/` to ścieżka. Plik, który nie jest
programem, wykonuje się jako skrypt powłoki.

`ls -l` pokazuje datę ostatniego zapisu pliku. Datę zna system, gdy ustawi ją sieć (SNTP),
zegar RTC albo `date RRRR-MM-DD GG:MM`.

### Edycja wiersza poleceń

Na terminalu (konsola szeregowa, okno **Terminal**, USB) wiersz poleceń edytuje sama
powłoka, tak samo w każdym z nich:

| Klawisze | Działanie |
|---|---|
| strzałki w lewo i w prawo (Ctrl-B, Ctrl-F) | ruch kursora w wierszu |
| Ctrl + strzałka w lewo / w prawo | o słowo |
| Home, End (Ctrl-A, Ctrl-E) | początek, koniec wiersza |
| Backspace, Delete (Ctrl-D w niepustym wierszu) | kasowanie przed kursorem, pod kursorem |
| Insert | wstawianie / nadpisywanie (w nadpisywaniu kursor jest blokiem) |
| strzałki w górę i w dół (Ctrl-P, Ctrl-N) | wcześniejsze polecenia |
| PgUp, PgDn | najstarsze polecenie, nowy wiersz |
| Ctrl-K, Ctrl-U | wycina do końca, do początku wiersza |
| Ctrl-W | wycina słowo przed kursorem |
| Ctrl-L | czyści ekran |
| Ctrl-C | porzuca wiersz (nie wykonuje go) |

Historia (200 poleceń) trafia do pliku `$HOME/.sh_history` (inny plik: zmienna `HISTFILE`)
i jest wspólna dla powłok na wszystkich terminalach; każda nowa powłoka ją wczytuje. Wiersz
dłuższy niż szerokość terminala przewija się w bok.

Na czas edycji wiersza terminal jest w trybie surowym (bez echa, każdy klawisz od razu), a na
czas programu wraca do trybu kanonicznego. Programy czytają więc linie jak dotąd, a Ctrl-C
kończy program na pierwszym planie.

Ctrl-C przerywa też polecenia wbudowane powłoki (`cat`, `cp`, `ls`, `rm -r`, `sleep`, `wait`,
`.` skrypt), np. przypadkowe `cat /dev/mtd1`. Po Ctrl-C powłoka pomija resztę wiersza
(`a; b`, `a && b`) i resztę skryptu, a `$?` ma wartość 130. `cp` przerwane w połowie usuwa
niedokończoną kopię, a `wait` przestaje czekać, ale programy w tle działają dalej. Klawisze
wpisane w czasie polecenia wbudowanego pojawią się w następnym wierszu poleceń.

### Składnia

| Zapis | Znaczenie |
|---|---|
| `a \| b \| c` | potok: wyjście `a` jest wejściem `b` |
| `a ; b` | po kolei |
| `a && b` | `b` tylko po sukcesie `a` (kod 0) |
| `a \|\| b` | `b` tylko po błędzie `a` |
| `a &` | w tle (`jobs`, `wait`) |
| `< plik`, `> plik`, `>> plik` | wejście z pliku, wyjście do pliku (dopisywanie) |
| `2> plik`, `2>&1`, `>&2` | błędy do pliku, błędy tam, gdzie wyjście, wyjście na błędy |
| `NAZWA=wartość` | zmienna (wszystkie zmienne są w środowisku programów) |
| `NAZWA=wartość program` | zmienna tylko dla tego programu |
| `$NAZWA`, `${NAZWA}` | wartość zmiennej; bez cudzysłowu dzielona na słowa |
| `$?`, `$$` | kod wyjścia ostatniego polecenia, numer procesu powłoki |
| `$0`…`$9`, `$#`, `$@` | argumenty skryptu |
| `'…'` | tekst dosłownie |
| `"…"` | tekst z `$…`; `\"`, `\\`, `\$` |
| `*`, `?`, `[a-z]` | nazwy pasujących plików, posortowane (wzorzec w ostatniej części ścieżki) |
| `# …` | komentarz |

Przykłady:

```
ls /sd/crtos/bin | cat > lista.txt
gcc -c *.c && gcc *.o -o prog.app || echo "nie wyszło: $?"
CFLAGS="-O2 -Wall" make
```

### Skrypty

```
sh skrypt.sh arg1 arg2      # w nowej powłoce
. skrypt.sh                 # w tej powłoce (zmienne zostają)
./skrypt.sh                 # plik nie-ELF: też w nowej powłoce
```

W skrypcie działają:
- `set -e`: koniec na pierwszym poleceniu z błędem;
- `set -x`: wypisuje każde polecenie przed wykonaniem;
- `exit KOD`.

Linia zakończona `\` ciągnie się dalej. Na końcu pliku powłoka kończy się z kodem ostatniego
polecenia.

### Zmienne na starcie

Gdy powłoka dostaje puste środowisko, ustawia:

| Zmienna | Wartość |
|---|---|
| `PATH` | `/flash0/bin:/sd/crtos/bin:/sd/crtos/apps` |
| `HOME` | `/sd/crtos` |
| `TMPDIR` | `/ram` (pliki tymczasowe) |
| `SHELL` | `/sd/crtos/bin/sh.app` |

### Czego nie ma

- instrukcji sterujących `if`, `for`, `while` i `case`;
- funkcji;
- podstawiania poleceń `$(…)`.

Zamiast `if` użyj `test … && … || …`.

## Okno Terminal

- **Zaznaczanie**: przeciągnięcie myszą zaznacza tekst (także w przewiniętej historii);
  dwuklik zaznacza słowo, a kliknięcie usuwa zaznaczenie.
- **Kopiowanie**: Ctrl+C, gdy coś jest zaznaczone (bez zaznaczenia Ctrl+C przerywa program jak
  dotąd), albo Ctrl+Shift+C lub Ctrl+Insert. Tekst trafia do schowka systemu (`gfxd`), więc
  można go wkleić w innym programie albo przez zdalny pulpit na komputerze.
- **Wklejanie**: Ctrl+V, Ctrl+Shift+V albo Shift+Insert. Tekst trafia do programu tak, jakby
  był wpisany; koniec linii działa jak Enter.
- **Przewijanie**: kółko myszy (3 wiersze na ząbek) albo przeciągnięcie palcem. Liczba w prawym
  górnym rogu mówi, o ile wierszy okno jest przewinięte; pisanie wraca na dół.
- Dotknięcie tekstu palcem (albo kliknięcie bez zaznaczenia) chowa i pokazuje klawiaturę
  ekranową.

## where

`where` pokazuje każde miejsce w `$PATH`, z którego może się uruchomić program o danej nazwie,
w kolejności, w jakiej szuka powłoka (`nazwa.app`, potem `nazwa`):

```
where gcc              /flash0/bin/gcc.app
where 'ed*'            wzorzec: * ? [...] (w cudzysłowie, żeby nie rozwinęła go powłoka)
where -t make          z rozmiarem i datą pliku
where -r /sd/crtos/etc '*.cfg'    każdy plik o tej nazwie w katalogu i niżej
where -q prog && echo jest       tylko kod wyjścia: 0 znaleziony, 1 nie
```

Wzorzec pasuje do nazwy z `.app` i bez niej. Polecenia wbudowane nie są plikami: pokazuje je
`which`.

## Edytor edit

```
edit plik.c          # otwiera plik albo tworzy nowy
edit plik.c +120     # od linii 120
```

| Klawisze | Działanie |
|---|---|
| strzałki, Home/End (Ctrl-A/Ctrl-E), PgUp/PgDn | ruch |
| Backspace, Del (Ctrl-D), Enter, Tab | pisanie (Enter zachowuje wcięcie) |
| Ctrl-S | zapis |
| Ctrl-Q (albo Ctrl-X) | wyjście; z niezapisanymi zmianami trzeba nacisnąć drugi raz |
| Ctrl-F | szukanie: pisz tekst, strzałki – następny/poprzedni, Enter – zostań, Esc – wróć |
| Ctrl-G | skok do linii |
| Ctrl-K | wycina linię; kolejne Ctrl-K dokładają następne |
| Ctrl-U | wkleja wycięte linie |
| Ctrl-L | rysuje ekran od nowa (np. po zmianie rozmiaru okna) |

Edytor działa w każdym terminalu:
- w oknie **Terminal** (z klawiatury USB albo ekranowej, która ma klawisz Ctrl);
- na konsoli szeregowej;
- przez USB w programie typu Tera Term albo PuTTY.

Zapis przebiega przez `plik.tmp`, więc nieudany zapis nie psuje pliku. Końce linii `\r\n`
zostają takie, jakie były.

## make

`make` to POSIX make ([pdpmake](https://github.com/rmyorston/pdpmake)). Przykład `Makefile`
dla programu z dwóch plików:

```make
CFLAGS = -O2 -Wall

prog.app: main.o util.o
	gcc main.o util.o -o $@

main.o util.o: util.h

clean:
	rm -f *.o prog.app
```

Wiersze poleceń zaczynają się od tabulatora. Polecenia wykonuje `sh -c`, więc działa w nich
wszystko z tabeli wyżej.

Domyślne reguły:
- `.c.o` → `$(CC) $(CFLAGS) -c $<`, gdzie `CC=gcc` (natywny kompilator płytki).

Opcje:

| Opcja | Działanie |
|---|---|
| `-n` | pokazuje polecenia, nie wykonując ich |
| `-k` | działa dalej po błędzie |
| `-f plik` | inny `Makefile` |
| `-C katalog` | zmienia katalog przed pracą |

`make` porównuje czasy plików. FAT zapisuje je co 2 sekundy, więc cel ma zwykle ten sam czas
co jego źródła. W CRTOS równe czasy oznaczają, że cel jest aktualny. Zmiana źródła w ciągu
około 2 sekund po zbudowaniu może zostać przeoczona. Pomaga wtedy `touch plik.c`.

## Sprawdzenia

```
sh /sd/crtos/share/sh/shtest.sh        # 20 sprawdzeń powłoki
sh /sd/crtos/share/make/maketest.sh    # make: budowanie, "nothing to be done", przebudowa
```
