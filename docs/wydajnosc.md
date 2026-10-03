# Wydajność

## Pomiar: `crtos bench`

```
.\crtos bench                         # pomiar i tabela wyników (około minuty)
.\crtos bench --save przed            # zapisuje wyniki w build/bench/przed.json
.\crtos bench --compare przed         # porównuje z zapisanymi: zmiana w %, "better" / "WORSE"
.\crtos bench --no-gfx                # bez testu grafiki (on otwiera na chwilę dwa okna)
```

Pomiary zbiera program `bench` na płytce (`crtos run bench [TEST...]` uruchamia same
wybrane), testy jądra z monitora (`crtos kmon "test sem"` itd.), log startu i `gfxinfo -b`.

| Test | Co mierzy |
|---|---|
| `syscall` | `getpid()`: wywołanie systemowe obsłużone od razu w obsłudze SVC |
| `sysfull` | `close(-1)`: pełna droga wywołania, którą idą wszystkie pozostałe (wątek jądra, może blokować) |
| `thread` | dwa wątki programu podają sobie futex: 2 przełączenia i 4 wywołania na obrót |
| `pipe` | dwa procesy, 1 bajt tam i z powrotem przez potoki |
| `port` | dwa procesy, wywołanie IPC z odpowiedzią (4 bajty) |
| `memcpy`, `memset` | 16 KB (mieści się w pamięci podręcznej danych) i 256 KB (SDRAM) |
| `read` | czytanie dużego pliku z karty SD kawałkami po 32 KB |
| `spawn` | uruchomienie `hello.app`: wczytanie z karty, start, koniec, `wait` |
| `k-switch` | przełączenie między wątkami jądra (z operacjami na semaforze), w cyklach |
| `irq-avg`, `irq-max` | opóźnienie przerwania: od zgłoszenia do obsługi, w cyklach |
| `kmalloc`, `kfree` | sterta jądra: mieszanka wszystkich pul i wyrównań (także niebuforowanej SDRAM) |
| `boot`, `init` | od startu jądra do wczytania sterowników i do pierwszego procesu |
| `fps`, `compose` | animacja w oknie (sama i z rysowaniem w drugim oknie) i czas składania klatki przez `gfxd` |

Procesor ma 600 MHz: 1 cykl to 1,67 ns.

## Wyniki (etap M9, wrzesień 2026)

| Test | Przed | Po | Zmiana |
|---|---|---|---|
| `syscall` (`getpid`) | 1193 ns | 146 ns | 8× szybciej |
| `sysfull` | ok. 1190 ns | 548 ns | 2× szybciej |
| `thread` | 6,3 µs | 4,5 µs | −29% |
| `pipe` | 17,7 µs | 9,2 µs | −48% |
| `port` | 13,7 µs | 7,9 µs | −42% |
| `memcpy` 16 KB | 182 MB/s | 1526 MB/s | 8× szybciej |
| `memcpy` 256 KB | 49 MB/s | 71 MB/s | +45% |
| `memset` 256 KB | 184 MB/s | 304 MB/s | +65% |
| `read` | 30,6 MB/s | 35,1 MB/s | +15% |
| `spawn` | 7,7 ms | 7,2 ms | −6% |
| `k-switch` | 416 cykli | 436 cykli | +5% (dwa dodatkowe rejestry MPU) |
| przerwanie | 45 / 195 cykli | 45 / 195 cykli | bez zmian |
| start (`init`) | 451 ms | 443 ms | −2% |
| grafika | 58 fps, klatka 1,7 ms | bez zmian | ogranicza odświeżanie ekranu (58,7 Hz) |

`memset` 16 KB waha się między pomiarami od kilkuset MB/s do ponad 2 GB/s. Cortex-M7 przy
ciągłym zapisie całych linii potrafi przestać umieszczać je w pamięci podręcznej.

## Pamięć podręczna i SDRAM

Program wykonywany z SDRAM (arena procesu) jest szybki tylko wtedy, gdy jego gorący kod
mieści się w pamięci podręcznej instrukcji. Program testowy wywołujący po kolei N funkcji
po ok. 340 B (ten sam kod, różna ilość):

| Kod w pętli | Czas na wywołanie |
|---|---|
| 5 KB | 211 ns |
| 21 KB | 596 ns |
| 42 KB | 2785 ns |
| 85 KB i więcej | 3620–3830 ns |

Kod ponad kilkanaście KB działa 18 razy wolniej: każda linia 32 B spoza pamięci podręcznej
kosztuje ok. 350 ns (ok. 200 cykli). Długość burstu SDRAM nie ma na to wpływu: próba z
burstem 8 zamiast 1 (`SDRAMCR0`, rejestr trybu kości w DCD) dała te same czasy, więc
konfiguracja z DCD została. Tak samo działają chybienia danych, więc duże bufory
przeglądane wiele razy na klatkę (np. emulatora) kosztują więcej niż wynika z samych
obliczeń.

Dwie pułapki danych:

- **Zapisy seryjne nie trafiają do pamięci podręcznej.** Cortex-M7, widząc ciąg zapisów
  całych linii (`memset`, `memcpy`), przestaje przydzielać im linie w pamięci podręcznej i
  pisze prosto do SDRAM. Bufor wyczyszczony przez `memset` i zaraz potem czytany daje więc
  chybienie na każdej linii. Lepszy jest mały bufor roboczy używany wielokrotnie: zostaje w
  pamięci podręcznej. Z tej samej cechy korzysta wypisywanie gotowego wyniku: kopia całych
  linii do SDRAM (np. do okna) nie wypycha danych roboczych. Renderer emulatora SNES rysuje
  pas 8 linii we własnych buforach i dopiero gotowy pas wypisuje do obrazu i do okna, co dało
  9–13% krótszą klatkę.
- **Zbiory pamięci podręcznej.** Pamięć podręczna danych ma 4 drogi po 8 KB: adresy
  odległe o wielokrotność 8 KB trafiają do tego samego zbioru. Kilka dużych buforów
  przeglądanych razem (np. obraz, podobraz i ich bufory głębokości) warto przesunąć względem
  siebie tak, aby ich wiersze nie zajmowały tych samych zbiorów.

Trzy wnioski ze zdalnego pulpitu (`vncd`, porównanie dwóch kopii ekranu 800×480 po 750 KB
i kodowanie zlib):

- **Czytanie przez procesor pamięci bez pamięci podręcznej jest bardzo wolne.** Porównanie
  całej kopii ekranu leżącej w pamięci bez cache trwało 121 ms, a w pamięci z cache 16,5 ms.
  Bufor, który pisze akcelerator (PXP), a czyta program, lepiej trzymać w pamięci z cache:
  sterownik PXP unieważnia obszar docelowy przed operacją i po niej (D03).
- **Wczytywanie z wyprzedzeniem pomaga przy przeglądaniu dużych buforów po kolei.**
  `__builtin_prefetch` (instrukcja PLD) kilka linii przed miejscem pracy sprawia, że
  chybienia się nakładają zamiast czekać po kolei: porównanie kopii ekranu przyspieszyło
  2,4 raza (z ok. 21 do ok. 9 ms przy oknie bez zmian).
- **Tablice pomocnicze muszą się mieścić w pamięci podręcznej danych (32 KB) razem
  z danymi.** Tablica skrótów kodera zlib 32 KB była wolniejsza od tablicy 8 KB
  (`uint16_t`, kandydaci najwyżej 8 KB wstecz): każde chybienie kosztowało więcej niż reszta
  pracy nad pikselem.

## Szybki kod programu w ITCM

Program może umieścić swoje najgorętsze funkcje w ITCM (pamięć bez oczekiwania, bez pamięci
podręcznej):

```cmake
crtos_app(gra SOURCES ... FAST gra_fast.txt FAST_KB 62)
```

- `gra_fast.txt` wymienia funkcje po jednej w wierszu (nazwy symboli jak w `nm`, C++
  z dekoracją), `#` zaczyna komentarz. Kolejność to priorytet: `tools/appfast.py` bierze je
  po kolei, aż do `FAST_KB` (domyślnie 48). Program musi być budowany z
  `-ffunction-sections` (domyślnie tak jest).
- Przy starcie jądro przydziela obszar szybkiego kodu na szczycie ITCM (`crtos kmon mem`,
  pula `fastcode`, ok. 72 KB) i umieszcza tam te funkcje. Wywołania między ITCM a SDRAM
  idą przez veneery. Komunikat `gra: 64 KB of fast code at 0x00010000` w logu potwierdza, że
  się udało.
- Obszar ma jednego właściciela naraz. Drugi program z `FAST` albo program, któremu obszar
  nie wystarczy, ładuje się normalnie (cały kod w SDRAM) i działa tak samo, tylko wolniej.
- Blok szybkiego kodu zajmuje jedno z trzech okien pamięci współdzielonej procesu.
- Listę najlepiej ułożyć z profilu (próbkowanie PC przez sondę, jak w
  [debugowaniu](debugowanie.md)): funkcje w kolejności udziału w czasie na KB kodu.
- Emulator SNES (`apps/snes/snes_fast.txt`, z profilu pięciu gier): 47 funkcji, 60 KB,
  obejmujące ok. 80% czasu emulacji; skraca klatkę o 10–17% (Zelda 14,1 → 12,5 ms, Yoshi
  22,5 → 18,8 ms przy pierwszej liście).

## Co zmieniono

- **`memcpy`, `memmove`, `memset`** (`kernel/lib/memops.c`) dla jądra i każdego programu
  (obiekt `crtos_memops`, zawsze linkowany). Wersje z newlib-nano kopiują po bajcie, bo są
  zbudowane na rozmiar. Nowe kopiują słowami po 32 bajty na obrót i pobierają dane z
  wyprzedzeniem 128 B (PLD), co w SDRAM daje +25%. Źródło może być niewyrównane, bo M7 czyta
  niewyrównane słowa ze zwykłej pamięci. Z tego powodu nie wolno ich używać na pamięci
  urządzeń z niewyrównanym źródłem.
- **Gorący kod jądra w ITCM** (`crtos.ld`): scheduler, przełączanie kontekstu,
  synchronizacja, wywołania systemowe, procesy, uchwyty, IPC, VFS, potoki, `poll`, pamięć
  współdzielona, sterta, przerwania, MPU, `memcpy`. To ok. 49 KB ze 128 KB. Wcześniej całe
  jądro wykonywało się z HyperFlash przez pamięć podręczną instrukcji, dzieloną z
  programami. Każde wyrzucenie kodu jądra z tej pamięci kosztowało odczyt z flash. Kod,
  który czeka na urządzenia (gniazda, konsola, log, grafika, wejście), wrócił do flash, a
  reszta ITCM to [szybki kod programu](#szybki-kod-programu-w-itcm).
- **Wywołanie systemowe**:
  - Obie strefy ochronne stosu wątku programu (stos użytkownika w regionie MPU 14, stos jądra
    w regionie 15) są włączone cały czas, więc wejście i wyjście z wywołania nie
    przeprogramowuje MPU. Jądro odrzuca wskaźniki do strefy ochronnej własnego stosu wątku
    (`uaccess_ok`).
  - Obsługa SVC rozpoznaje wywołanie po uprawnieniach wywołującego, bez czytania instrukcji
    z pamięci programu.
  - `getpid`, `gettid` i `time_us` są obsługiwane od razu w obsłudze SVC.
- **Zegar systemowy** liczy tyknięcia, których przerwanie nie zdążyło obsłużyć (przerwania
  zablokowane dłużej niż 1 ms), z licznika cykli. `crtos kmon uptime` pokazuje, czy się
  zdarzyły. Obciążenie CPU liczone jest poprawnie, także po rozłączeniu debugera.

## Sieć (październik 2026)

Pomiar: `crtos netbench` (program `nettest` na płytce, klient w `tools/netbench.py`). Komputer
łączył się przez Wi-Fi (czas obiegu ok. 1,8 ms), płytka kablem 100 Mbit/s.

| Test | Przedtem | Teraz | Teraz, przy animacji na ekranie |
|---|---|---|---|
| TCP do płytki | 38–52 Mbit/s | 93,7 Mbit/s | 94,8 Mbit/s |
| TCP z płytki | 59–65 Mbit/s (`nettest` 96% CPU) | 90–95 Mbit/s (37% CPU) | 94,7 Mbit/s |
| UDP do płytki / z płytki | 95 / 94 Mbit/s | 95 / 96 Mbit/s | 95 / 96 Mbit/s |

Sufit TCP na łączu 100 Mbit/s to ok. 94,9 Mbit/s (ramka 1538 B na 1460 B danych).
`crtos scp` pliku 8 MB: 9,4–9,7 MB/s na `/ram`, 7,4–8,5 MB/s na kartę.

Co zmieniono (D05, S05):

- **Deskryptory DMA w DTCM.** Kontroler czyta deskryptor przed każdą ramką. W SDRAM ten
  odczyt czekał za ekranem i akceleratorem 2D, a w tym czasie przepełniało się FIFO
  odbiornika: przy animacji 0,4% uszkodzonych ramek i 66 Mbit/s. W DTCM (bez pamięci
  podręcznej, kontroler sięga przez port AHBS rdzenia) jest zero uszkodzonych ramek. Bufory
  odbiorcze są w OCRAM z pamięcią podręczną, unieważniane przed odczytem (odczyt pamięci bez
  cache był najwolniejszą częścią ramki), i stos kopiuje ramkę wprost z nich.
- **Okna TCP 91 KB ze skalowaniem (RFC 7323).** Przy 100 Mbit/s okno musi pokryć czas
  obiegu, a przez Wi-Fi to 2–4 ms. Przy oknie 23 KB nadawca przez większość czasu czekał
  (50 Mbit/s przy prawie bezczynnym procesorze). Bez skalowania okno ma najwyżej 64 KB.
- **Sumy kontrolne w sprzęcie.** Kontroler ENET wstawia i sprawdza sumy IPv4/TCP/UDP/ICMP.
  Dodaje przy tym wartość pola, więc sterownik je zeruje: inaczej `ping` z płytki (gniazdo
  surowe z sumą programu) nie działał.
- **Mniej budzenia.** lwIP zgłaszał jądru zdarzenie gniazda przy każdym segmencie, a nadawca
  budził się, żeby dopisać kilka KB. Teraz zgłasza tylko zmiany, które mogą kogoś odblokować.
  Ścieżka danych gniazd jądra przeszła z flasha do ITCM. Profil wysyłania przed zmianą:
  zdarzenia ok. 25%, kod z flasha 13%, dzielenie 64-bitowe przy limicie czasu 4%. Po zmianie
  procesor jest w 60% bezczynny, a resztę zajmują głównie dwie kopie danych (`memcpy` 29%).

## Znane ograniczenia

- **Sieć przy wielu gniazdach naraz.** Pula buforów stosu mieści okno TCP jednego gniazda
  (91 KB). Kilka gniazd odbierających naraz z pełną prędkością gubi ramki (`crtos kmon
  net`: `dropped`), a TCP wysyła je ponownie. Sterownik włącza sterowanie przepływem (ramki
  PAUSE, 802.3x), gdy przełącznik je obsługuje, co log pokazuje jako `flow control on`.
- **`kmalloc`/`kfree` ok. 700 cykli.** Test celowo miesza pule, wyrównania i rozmiary do
  16 KB, w tym pulę bez pamięci podręcznej. Typowe małe alokacje są znacznie tańsze.
- **SDRAM.** Kopiowanie 256 KB (71 MB/s) ogranicza sama pamięć: odczyt źródła, wczytanie
  linii celu i zapis z powrotem.
