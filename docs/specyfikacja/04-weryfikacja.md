# 04 Weryfikacja

Specyfikacja i raport weryfikacji oprogramowania CRTOS: metody, wyniki, powiązanie
wymagań z weryfikacją, pewność narzędzi (ISO 26262-8, rozdz. 11) i stan komponentów
zewnętrznych (ISO 26262-8, rozdz. 12).

## 1. Cel i zakres

- Pokazać, jak sprawdzane jest każde wymaganie komponentów (`REQ-<ID>-NN`) i jaki jest
  ostatni wynik.
- Poziomy wg ISO 26262-6: weryfikacja jednostek (rozdz. 9), integracja i jej weryfikacja
  (rozdz. 10), testy oprogramowania wbudowanego na docelowym sprzęcie (rozdz. 11).
- Stan: system QM – weryfikacja jest wykonywana w zakresie potrzebnym do rozwoju, bez
  formalnej specyfikacji testów, pomiaru pokrycia ani niezależności

## 2. Metody i poziomy weryfikacji

| Metoda | Narzędzie / polecenie | Poziom (ISO 26262-6) | Co obejmuje | Kiedy |
|---|---|---|---|---|
| przegląd kodu i projektu | ta dokumentacja (odtworzenie wymagań z kodu) | 8–9 | wszystkie komponenty | 26–27.09.2026 |
| ostrzeżenia kompilatora | GCC `-Wall` (jądro, moduły, programy) | 9 (analiza statyczna – częściowo) | cały kod | każde budowanie |
| kontrola symboli i relokacji | `tools/modcheck.py` | 10 (integracja) | każdy `.ko` i `.app` | każde budowanie |
| kontrola ramek stosu jądra | `tools/stackcheck.py` (`-fstack-usage`) | 9 | funkcje jądra > 256 B | budowanie jądra |
| autotesty jądra | `crtos kmon "test all"` (13 testów, `kernel/rtos/tests.cpp`) | 9–10, na płytce | scheduler, mutex z PI, semafory, przerwania, sterta, FPU, izolacja błędów, procesy, wywołania | po zmianie jądra |
| testy interfejsu programów | `crtos run apptest` (12 grup, `tests/apptest`) | 10–11, na płytce | pliki, katalogi, błędy i uprawnienia, sterta, wątki, futex, pthread, IPC, shm, `poll`, ochrona pamięci, cykl życia | po zmianie jądra lub `libcrtos` |
| pomiary wydajności | `crtos bench` (`tests/bench` + `gfxinfo -b`) | 11 | koszt wywołań, przełączeń, IPC, pamięci, SD, startu programu, fps | po zmianach wydajności ([Wydajność](../wydajnosc.md)) |
| testy obciążeniowe karty | `kmon sdstress`, `kmon sd trace` | 11 | sterownik SD (K14) | po zmianie K14 |
| testy na płytce, ręczne | `crtos run ...` (`ping`, `nc`, `spi -l`, `uart -l`, `mtd test`, `evtest`, `gfxtap`), dotyk, klawiatura USB, `crtos shot` | 11 | sterowniki, usługi, aplikacje | po zmianie komponentu |
| test wdrożenia | `crtos deploy` (CRC-32 każdego pliku), `crtos flash --net` | 11 | U06, D07, T01 | przy każdym wgraniu |

## 3. Wyniki

### 3.1 Autotesty jądra – 27.09.2026 (`crtos kmon "test all"`)

| Test | Sprawdza | Wynik | Pomiar |
|---|---|---|---|
| `sched` | round robin i ścisłe priorytety | ok | 484432 / 484470 / 484465 pętli; niski priorytet: 0 pętli; z wywłaszczaniem co tyknięcie 483841 / 483866 pętli (przed poprawką 27.09.2026: 959982 / 0) |
| `mutex` | dziedziczenie priorytetu | ok | wysoki priorytet czekał 30,0 ms, właściciel działał z priorytetem 15 |
| `sem` | semafory, koszt przełączenia | ok | 469 cykli (781 ns) na przełączenie z operacjami semafora |
| `irq` | opóźnienie przerwania | ok | min 45, śr. 45, maks. 197 cykli (śr. 75 ns) |
| `heap` | sterta: obciążenie i spójność | ok | `kmalloc` śr. 758 cykli, `kfree` śr. 736 cykli |
| `fpu` | kontekst FPU wątków jądra | ok | |
| `kstack` | przepełnienie stosu wątku jądra ograniczone do wątku | ok | |
| `null` | NULL w wątku jądra ograniczony do wątku | ok | |
| `user` | proces nieuprzywilejowany startuje i kończy się | ok | |
| `usermem` | dostęp programu do pamięci jądra kończy tylko proces | ok | |
| `userstack` | przepełnienie stosu programu kończy tylko proces | ok | |
| `userregs` | rejestry programu i FPU przetrwają wywołania i przełączenia | ok | |
| `syscall` | koszt wywołania systemowego | ok | 116 ns na `getpid()` z trybu użytkownika |

**13 testów, 0 błędów** (1267 ms).

03.10.2026, po podziale jądra na rdzeń i część OS: obraz OS – `test all` **15 testów, 0 błędów**
(nowe `queue`: 3000 elementów przez kolejkę na 4 elementy, 2,7 µs na element; `timer`:
jednorazowy po 19,995 ms, okresowy 5 ms – 20 wywołań w 102 ms); obraz RTOS z
`examples/rtos/blinky` – **10 testów rdzenia, 0 błędów** (kolejka 2,0 µs na element, maks.
opóźnienie przerwania 137 cykli). Porównanie A/B tego samego pomiaru (`crtos bench`) na
jądrze sprzed podziału i po nim: przełączenie wątków ok. 440 cykli w obu, `kmalloc` 901 i
918–933 cykli, opóźnienie przerwania maks. 195 i 197 cykli – w granicach rozrzutu.

29.09.2026 (jądro z loaderem XIP, limitem sterty 24 MB i wspólnym środowiskiem programów,
pulpit uruchomiony): trzy przebiegi `test all` – dwa z jednym błędem (raz `sched`: „lower
priority ran while a higher one was ready”, raz `irq`: „memory leak: 128 bytes”), trzeci
13/13; `test sched` i `test irq` osobno 3/3. Błędy nie powtarzają się; zmiany jądra nie
dotyczą schedulera ani przerwań, a oba testy są wrażliwe na pracę w tle (inne wątki
w trakcie pomiaru, przydziały pamięci sieci). Zapisane jako luka W-09 (rozdz. 8).

### 3.2 Testy interfejsu programów (`crtos run apptest`)

Ostatni wynik: **1720 sprawdzeń, 0 błędów** (27.09.2026). Czasy grup z przebiegu
z 26.09.2026:

| Grupa | Wynik | Czas | Pomiar |
|---|---|---|---|
| files on /ram | ok | 1 ms | |
| files on /sd | ok | 127 ms | |
| directories | ok | 0 ms | |
| errors and permissions | ok | 16 ms | |
| heap | ok | 5 ms | |
| threads and mutexes | ok | 75 ms | |
| futex | ok | 49 ms | opóźnienie obudzenia 8 µs |
| POSIX threads | ok | 311 ms | |
| IPC in a process | ok | 3 ms | wywołanie i odpowiedź 14,2 µs |
| IPC, shm, poll between processes | ok | 66 ms | |
| memory protection | ok | 45 ms | 3 procesy z błędami → `-EFAULT`, system działa |
| process lifecycle | ok | 231 ms | pamięć procesów po 8 uruchomieniach: +0 B |

### 3.3 Testy na płytce (ręczne), stan na 27.09.2026

| Obszar | Test | Wynik |
|---|---|---|
| ekran, grafika | `gfxinfo -b`, `gfxdemo`, zrzuty `crtos shot` | 58 fps (ekran 58,7 Hz), składanie klatki ok. 1,7 ms |
| wygląd interfejsu (03.10.2026) | `appearance` przez kmon: skale 100–200%, cztery rodziny czcionek, akcent, przezroczystość i krycie, zaokrąglenia, tapety wbudowane, kolor i plik (`crtos wallpaper`); zrzuty `crtos desktop --shot` | działa bez restartu programów; tapeta wbudowana ok. 0,3 s, plik 800×500 0,1 s |
| dotyk, przycisk | dotyk GT911, SW8 (`evtest`) | działa |
| sieć | DHCP, NTP, `ping`, `nc`, `crtos deploy`, `httpd` | działa |
| USB, tryb urządzenia | wyliczenie High Speed, powłoka `getty`, Ctrl-C | działa, ok. 280 KB/s płytka → komputer |
| USB, tryb host | start kontrolera, zasilanie portu | działa; wyliczenie klawiatury i myszy wymaga przejściówki OTG (test ręczny, niewykonany przy tej dokumentacji) |
| SPI | `spi` – komunikaty do 16 KB, 100 kHz–20 MHz, polecenie + odczyt; stan pinów przez SWD | działa |
| UART | `uart -l` (pętla wewnętrzna) | działa |
| dźwięk | `audio tone 440 2000` (96000 ramek), emulator NES przez ok. 110 s | działa, 0 przerw w danych, 0 błędów FIFO |
| flash | `mtd test`, `crtos flash --net` (także odrzucenie złego obrazu) | działa |
| izolacja | apptest „memory protection”, `kmon test user*` | tylko proces z błędem kończy się |

## 4. Śledzenie wymagań

Rodzaje weryfikacji: **A** – test automatyczny (autotesty jądra, `apptest`, `bench`,
`modcheck`), **M** – test na płytce uruchamiany ręcznie albo obserwacja (polecenia,
log, testy ręczne), **P** – przegląd kodu. Tabele są wygenerowane z kolumny
„Weryfikacja” dokumentów komponentów.

### 4.1 Podsumowanie

| Komponent | Wymagania | test automatyczny (A) | test na płytce / ręczny (M) | przegląd kodu (P) | tylko przegląd kodu |
|---|---|---|---|---|---|
| K01 | 6 | 5 | 0 | 1 | 1 |
| K02 | 5 | 3 | 1 | 2 | 2 |
| K03 | 8 | 5 | 2 | 4 | 1 |
| K04 | 6 | 4 | 1 | 3 | 2 |
| K05 | 8 | 7 | 2 | 2 | 1 |
| K06 | 7 | 5 | 0 | 3 | 2 |
| K07 | 9 | 4 | 2 | 7 | 4 |
| K08 | 9 | 9 | 1 | 2 | 0 |
| K09 | 7 | 6 | 0 | 4 | 1 |
| K10 | 7 | 3 | 0 | 4 | 4 |
| K11 | 9 | 5 | 1 | 5 | 3 |
| K12 | 5 | 2 | 1 | 3 | 3 |
| K13 | 9 | 4 | 2 | 3 | 3 |
| K14 | 8 | 4 | 5 | 4 | 1 |
| K15 | 6 | 1 | 5 | 2 | 1 |
| K16 | 14 | 5 | 8 | 11 | 3 |
| K17 | 7 | 0 | 4 | 4 | 3 |
| K18 | 7 | 0 | 5 | 2 | 2 |
| K19 | 6 | 2 | 4 | 1 | 1 |
| K20 | 8 | 6 | 0 | 6 | 2 |
| K21 | 6 | 5 | 1 | 2 | 1 |
| S01 | 5 | 0 | 1 | 4 | 4 |
| S02 | 5 | 0 | 2 | 3 | 3 |
| S03 | 7 | 3 | 5 | 4 | 2 |
| S04 | 6 | 0 | 2 | 5 | 4 |
| S05 | 8 | 1 | 3 | 6 | 5 |
| D01 | 6 | 0 | 2 | 6 | 4 |
| D02 | 6 | 0 | 3 | 3 | 3 |
| D03 | 7 | 1 | 6 | 3 | 1 |
| D04 | 13 | 0 | 10 | 10 | 3 |
| D05 | 9 | 1 | 8 | 3 | 1 |
| D06 | 8 | 0 | 6 | 3 | 2 |
| D07 | 8 | 1 | 6 | 3 | 2 |
| D08 | 5 | 0 | 0 | 5 | 5 |
| D09 | 6 | 0 | 3 | 5 | 3 |
| D10 | 9 | 3 | 6 | 4 | 1 |
| U01 | 4 | 0 | 2 | 2 | 2 |
| U02 | 3 | 0 | 1 | 2 | 2 |
| U03 | 14 | 0 | 12 | 7 | 2 |
| U04 | 9 | 0 | 7 | 7 | 2 |
| U05 | 3 | 0 | 2 | 1 | 1 |
| U06 | 7 | 0 | 5 | 3 | 2 |
| U07 | 4 | 0 | 2 | 2 | 2 |
| U08 | 10 | 0 | 10 | 5 | 0 |
| L01 | 14 | 13 | 2 | 7 | 1 |
| L02 | 13 | 0 | 10 | 6 | 3 |
| A01 | 11 | 0 | 8 | 6 | 3 |
| A02 | 17 | 1 | 16 | 5 | 1 |
| A03 | 13 | 5 | 10 | 5 | 1 |
| T01 | 17 | 2 | 13 | 5 | 3 |
| T02 | 15 | 6 | 8 | 6 | 2 |
| **razem** | **419** | **122** | **216** | **211** | **111** |

111 z 419 wymagań (26%) jest sprawdzonych tylko przeglądem kodu – głównie sterowniki,
frameworki podsystemów i usługi, które nie mają testów automatycznych (luka W-02
w rozdz. 8).

### 4.2 Macierz wymagań

| Wymaganie | Rodzaj | Weryfikacja |
|---|---|---|
| [REQ-K01-01](komponenty/K01-kontekst-i-svc.md#3-wymagania) | A | `kmon test fpu`, `kmon test userregs` |
| [REQ-K01-02](komponenty/K01-kontekst-i-svc.md#3-wymagania) | A | `kmon test kstack`, `kmon test userstack` |
| [REQ-K01-03](komponenty/K01-kontekst-i-svc.md#3-wymagania) | A | `kmon test syscall`, `apptest` |
| [REQ-K01-04](komponenty/K01-kontekst-i-svc.md#3-wymagania) | A | `kmon test userregs` |
| [REQ-K01-05](komponenty/K01-kontekst-i-svc.md#3-wymagania) | A | `apptest` (cykl życia procesu) |
| [REQ-K01-06](komponenty/K01-kontekst-i-svc.md#3-wymagania) | P | przegląd kodu |
| [REQ-K02-01](komponenty/K02-przerwania.md#3-wymagania) | A, M | `kmon test irq`, `kmon irq` |
| [REQ-K02-02](komponenty/K02-przerwania.md#3-wymagania) | P | przegląd kodu |
| [REQ-K02-03](komponenty/K02-przerwania.md#3-wymagania) | A | testy K05/K06 (`kmon test sched`, `mutex`, `sem`) |
| [REQ-K02-04](komponenty/K02-przerwania.md#3-wymagania) | P | przegląd kodu |
| [REQ-K02-05](komponenty/K02-przerwania.md#3-wymagania) | A | `kmon test irq`, `crtos bench` |
| [REQ-K03-01](komponenty/K03-mpu.md#3-wymagania) | A | `kmon test usermem`, `apptest` (ochrona pamięci) |
| [REQ-K03-02](komponenty/K03-mpu.md#3-wymagania) | A | `kmon test null` |
| [REQ-K03-03](komponenty/K03-mpu.md#3-wymagania) | A | `kmon test kstack`, `kmon test userstack` |
| [REQ-K03-04](komponenty/K03-mpu.md#3-wymagania) | A | `apptest` (IPC i shm między procesami), `kmon test usermem` |
| [REQ-K03-05](komponenty/K03-mpu.md#3-wymagania) | P | przegląd kodu |
| [REQ-K03-06](komponenty/K03-mpu.md#3-wymagania) | A, P | przegląd kodu, `kmon test null` |
| [REQ-K03-07](komponenty/K03-mpu.md#3-wymagania) | M, P | przegląd kodu (`kmon mpu`), `xiptest` z `/flash0` |
| [REQ-K03-08](komponenty/K03-mpu.md#3-wymagania) | M, P | przegląd kodu, `snes` z `FAST` (`kmon mem`) |
| [REQ-K04-01](komponenty/K04-wyjatki.md#3-wymagania) | A | `kmon test null`, `kstack`, `usermem`, `userstack`; `apptest` (ochrona pamięci) |
| [REQ-K04-02](komponenty/K04-wyjatki.md#3-wymagania) | P | przegląd kodu |
| [REQ-K04-03](komponenty/K04-wyjatki.md#3-wymagania) | A, M | `apptest` (raporty w logu), `crtos crash`, `xiptest crash` |
| [REQ-K04-04](komponenty/K04-wyjatki.md#3-wymagania) | P | przegląd kodu |
| [REQ-K04-05](komponenty/K04-wyjatki.md#3-wymagania) | A | `apptest` (pamięć wraca po zakończeniu procesów) |
| [REQ-K04-06](komponenty/K04-wyjatki.md#3-wymagania) | A, P | `heaptest` (instrukcje na pamięci emulowanej), przegląd kodu |
| [REQ-K05-01](komponenty/K05-scheduler.md#3-wymagania) | A | `kmon test sched` (część „priority”) |
| [REQ-K05-02](komponenty/K05-scheduler.md#3-wymagania) | A | `kmon test sched` (różnica < 10%) |
| [REQ-K05-03](komponenty/K05-scheduler.md#3-wymagania) | A, P | przegląd kodu, `crtos bench` (`k-switch`) |
| [REQ-K05-04](komponenty/K05-scheduler.md#3-wymagania) | A | `kmon test sem`, `apptest` (futex, IPC z limitem czasu) |
| [REQ-K05-05](komponenty/K05-scheduler.md#3-wymagania) | A | `apptest` (cykl życia), `kmon test user` |
| [REQ-K05-06](komponenty/K05-scheduler.md#3-wymagania) | A, M | `kmon uptime` (statystyka `late/lost`), test ręczny z zapisem flash (D07) |
| [REQ-K05-07](komponenty/K05-scheduler.md#3-wymagania) | A, M | `apptest` (pamięć po zakończeniu procesów), `kmon mem` |
| [REQ-K05-08](komponenty/K05-scheduler.md#3-wymagania) | P | przegląd kodu |
| [REQ-K06-01](komponenty/K06-synchronizacja.md#3-wymagania) | A | `kmon test mutex` (wątek o średnim priorytecie nie wyprzedza, właściciel działa z priorytetem 15) |
| [REQ-K06-02](komponenty/K06-synchronizacja.md#3-wymagania) | A, P | przegląd kodu, `kmon test mutex` |
| [REQ-K06-03](komponenty/K06-synchronizacja.md#3-wymagania) | P | przegląd kodu |
| [REQ-K06-04](komponenty/K06-synchronizacja.md#3-wymagania) | P | przegląd kodu (`mutex_release_all` w `task_exit`) |
| [REQ-K06-05](komponenty/K06-synchronizacja.md#3-wymagania) | A | `kmon test sem`, sterowniki (D02–D06) |
| [REQ-K06-06](komponenty/K06-synchronizacja.md#3-wymagania) | A | `kmon test sem`, `apptest` (futex) |
| [REQ-K06-07](komponenty/K06-synchronizacja.md#3-wymagania) | A | `apptest` (futex, wątki POSIX) |
| [REQ-K07-01](komponenty/K07-pamiec-jadra.md#3-wymagania) | A, P | przegląd kodu, `kmon test heap` (średni koszt) |
| [REQ-K07-02](komponenty/K07-pamiec-jadra.md#3-wymagania) | A | `kmon test heap` (wyrównanie), start procesów (areny) |
| [REQ-K07-03](komponenty/K07-pamiec-jadra.md#3-wymagania) | P | przegląd kodu |
| [REQ-K07-04](komponenty/K07-pamiec-jadra.md#3-wymagania) | P | przegląd kodu |
| [REQ-K07-05](komponenty/K07-pamiec-jadra.md#3-wymagania) | P | przegląd kodu |
| [REQ-K07-06](komponenty/K07-pamiec-jadra.md#3-wymagania) | A, M | `kmon test heap`, `kmon mem` |
| [REQ-K07-07](komponenty/K07-pamiec-jadra.md#3-wymagania) | M, P | przegląd kodu, `kmon mem` (`fastcode`) |
| [REQ-K07-08](komponenty/K07-pamiec-jadra.md#3-wymagania) | P | przegląd kodu |
| [REQ-K07-09](komponenty/K07-pamiec-jadra.md#3-wymagania) | A, P | przegląd kodu, `heaptest` (arena `cc1` na górze, okno sterty 21,5 MB z reszty) |
| [REQ-K08-01](komponenty/K08-procesy.md#3-wymagania) | A, P | przegląd kodu, `apptest` |
| [REQ-K08-02](komponenty/K08-procesy.md#3-wymagania) | A | `apptest` (cykl życia: brak wycieku pamięci) |
| [REQ-K08-03](komponenty/K08-procesy.md#3-wymagania) | A | `apptest` (`kill` z kodem 77) |
| [REQ-K08-04](komponenty/K08-procesy.md#3-wymagania) | A | `apptest` (`ECHILD` po odebraniu) |
| [REQ-K08-05](komponenty/K08-procesy.md#3-wymagania) | A | `apptest` (błędy i uprawnienia, `EFAULT`), `apptest_xip` (napisy we flashu) |
| [REQ-K08-06](komponenty/K08-procesy.md#3-wymagania) | A | `apptest` (błędy) |
| [REQ-K08-07](komponenty/K08-procesy.md#3-wymagania) | A | `apptest` (`noperm`) |
| [REQ-K08-08](komponenty/K08-procesy.md#3-wymagania) | A | `apptest` (IPC, shm między procesami; `killself`: proces zwolniony po `kill` samego siebie) |
| [REQ-K08-09](komponenty/K08-procesy.md#3-wymagania) | A, M, P | `xiptest` (wątek), `apptest_xip`, przegląd kodu |
| [REQ-K09-01](komponenty/K09-wywolania-systemowe.md#3-wymagania) | A | `apptest` (błędy: `write` ze wskaźnikiem jądra → `EFAULT`) |
| [REQ-K09-02](komponenty/K09-wywolania-systemowe.md#3-wymagania) | A, P | przegląd kodu, `apptest` (pliki, katalogi) |
| [REQ-K09-03](komponenty/K09-wywolania-systemowe.md#3-wymagania) | A | `apptest` (proces `noperm`) |
| [REQ-K09-04](komponenty/K09-wywolania-systemowe.md#3-wymagania) | A, P | przegląd kodu, `apptest` |
| [REQ-K09-05](komponenty/K09-wywolania-systemowe.md#3-wymagania) | A, P | przegląd kodu, `apptest` (`killself`) |
| [REQ-K09-06](komponenty/K09-wywolania-systemowe.md#3-wymagania) | P | przegląd kodu |
| [REQ-K09-07](komponenty/K09-wywolania-systemowe.md#3-wymagania) | A | `apptest` |
| [REQ-K10-01](komponenty/K10-ipc.md#3-wymagania) | A | `apptest` (IPC między procesami, przekazanie uchwytu portu i pamięci współdzielonej) |
| [REQ-K10-02](komponenty/K10-ipc.md#3-wymagania) | P | przegląd kodu |
| [REQ-K10-03](komponenty/K10-ipc.md#3-wymagania) | P | przegląd kodu |
| [REQ-K10-04](komponenty/K10-ipc.md#3-wymagania) | P | przegląd kodu |
| [REQ-K10-05](komponenty/K10-ipc.md#3-wymagania) | A | `apptest` (`EPIPE` po zakończeniu procesu serwera) |
| [REQ-K10-06](komponenty/K10-ipc.md#3-wymagania) | P | przegląd kodu |
| [REQ-K10-07](komponenty/K10-ipc.md#3-wymagania) | A | `apptest` (`EEXIST`, `ENOENT` po zamknięciu portu) |
| [REQ-K11-01](komponenty/K11-pamiec-wspoldzielona.md#3-wymagania) | P | przegląd kodu |
| [REQ-K11-02](komponenty/K11-pamiec-wspoldzielona.md#3-wymagania) | A | `apptest` (shm między procesami), K03 |
| [REQ-K11-03](komponenty/K11-pamiec-wspoldzielona.md#3-wymagania) | A | `apptest` (brak wycieku po zakończeniu procesów) |
| [REQ-K11-04](komponenty/K11-pamiec-wspoldzielona.md#3-wymagania) | P | przegląd kodu |
| [REQ-K11-05](komponenty/K11-pamiec-wspoldzielona.md#3-wymagania) | P | przegląd kodu (`mpu_proc_changed`) |
| [REQ-K11-06](komponenty/K11-pamiec-wspoldzielona.md#3-wymagania) | A, P | `heaptest` (okno 16 MB obok działającego pulpitu), `apptest`, przegląd kodu |
| [REQ-K11-07](komponenty/K11-pamiec-wspoldzielona.md#3-wymagania) | A, P | `heaptest` (obiekt 23,5 MB w trzech regionach, `write` przez granice kawałków, `EFAULT` za końcem), przegląd kodu |
| [REQ-K11-08](komponenty/K11-pamiec-wspoldzielona.md#3-wymagania) | A | `heaptest` (`write` do ostatniego bajtu obiektu przyjęty, o bajt dalej `EFAULT`) |
| [REQ-K11-09](komponenty/K11-pamiec-wspoldzielona.md#3-wymagania) | M | `crtos desktop --shot` przy 800×480: kopia ekranu 750 KB poza pulą bez cache, obraz zgodny z `crtos shot` |
| [REQ-K12-01](komponenty/K12-potoki-i-poll.md#3-wymagania) | A, M | `crtos bench` (`pipe`), `term` i `getty` (ręcznie); brak testu automatycznego końca pliku i `-EPIPE` |
| [REQ-K12-02](komponenty/K12-potoki-i-poll.md#3-wymagania) | P | przegląd kodu |
| [REQ-K12-03](komponenty/K12-potoki-i-poll.md#3-wymagania) | A | `apptest` (poll na porcie: limit czasu i odpowiedź) |
| [REQ-K12-04](komponenty/K12-potoki-i-poll.md#3-wymagania) | P | przegląd kodu |
| [REQ-K12-05](komponenty/K12-potoki-i-poll.md#3-wymagania) | P | przegląd kodu |
| [REQ-K13-01](komponenty/K13-vfs.md#3-wymagania) | A | `apptest` (pliki, katalogi) |
| [REQ-K13-02](komponenty/K13-vfs.md#3-wymagania) | P | przegląd kodu |
| [REQ-K13-03](komponenty/K13-vfs.md#3-wymagania) | P | przegląd kodu |
| [REQ-K13-04](komponenty/K13-vfs.md#3-wymagania) | A | `apptest` (`dup`, uchwyty dziedziczone przez procesy) |
| [REQ-K13-05](komponenty/K13-vfs.md#3-wymagania) | M | test ręczny (`rmmod` przy otwartym `/dev/*`) |
| [REQ-K13-06](komponenty/K13-vfs.md#3-wymagania) | P | przegląd kodu, `devmgr` (U02) |
| [REQ-K13-07](komponenty/K13-vfs.md#3-wymagania) | A | apptest (ścieżka z 60 × `x/..`) |
| [REQ-K13-08](komponenty/K13-vfs.md#3-wymagania) | A | apptest (czas pliku w `/ram`) |
| [REQ-K13-09](komponenty/K13-vfs.md#3-wymagania) | M | `xiptest` z `/flash0` i z karty |
| [REQ-K14-01](komponenty/K14-karta-sd-fat.md#3-wymagania) | A, M, P | przegląd kodu, `kmon sdbench` |
| [REQ-K14-02](komponenty/K14-karta-sd-fat.md#3-wymagania) | M | `kmon sdstress`, `kmon sd trace` |
| [REQ-K14-03](komponenty/K14-karta-sd-fat.md#3-wymagania) | M, P | przegląd kodu, `kmon sd` |
| [REQ-K14-04](komponenty/K14-karta-sd-fat.md#3-wymagania) | M | `kmon sdstress` |
| [REQ-K14-05](komponenty/K14-karta-sd-fat.md#3-wymagania) | A, P | przegląd kodu, `apptest` (pliki na /sd) |
| [REQ-K14-06](komponenty/K14-karta-sd-fat.md#3-wymagania) | A, M | `apptest` (pliki na /sd), `crtos deploy` (sumy CRC po zapisie) |
| [REQ-K14-07](komponenty/K14-karta-sd-fat.md#3-wymagania) | P | przegląd kodu |
| [REQ-K14-08](komponenty/K14-karta-sd-fat.md#3-wymagania) | A | apptest (czas pliku na `/sd`) |
| [REQ-K15-01](komponenty/K15-konsola-log.md#3-wymagania) | M, P | przegląd kodu, `kmon uptime` (`log lost`) |
| [REQ-K15-02](komponenty/K15-konsola-log.md#3-wymagania) | P | przegląd kodu |
| [REQ-K15-03](komponenty/K15-konsola-log.md#3-wymagania) | M | test ręczny (`crtos serial`, Ctrl-]) |
| [REQ-K15-04](komponenty/K15-konsola-log.md#3-wymagania) | M | test ręczny (`crtos serial`, `ping`, Ctrl-C) |
| [REQ-K15-05](komponenty/K15-konsola-log.md#3-wymagania) | A, M | `kmon panic` (test ręczny), `crtos crash` |
| [REQ-K15-06](komponenty/K15-konsola-log.md#3-wymagania) | M | `kmon panic` |
| [REQ-K16-01](komponenty/K16-loader.md#3-wymagania) | A, P | przegląd kodu, `tools/modcheck.py` |
| [REQ-K16-02](komponenty/K16-loader.md#3-wymagania) | A, P | `tools/modcheck.py` (przy budowaniu), przegląd kodu |
| [REQ-K16-03](komponenty/K16-loader.md#3-wymagania) | M | test ręczny (`rmmod` przy otwartym `/dev/*`, `rmmod net-lwip`) |
| [REQ-K16-04](komponenty/K16-loader.md#3-wymagania) | P | przegląd kodu |
| [REQ-K16-05](komponenty/K16-loader.md#3-wymagania) | A, P | przegląd kodu, `apptest` |
| [REQ-K16-06](komponenty/K16-loader.md#3-wymagania) | P | przegląd kodu |
| [REQ-K16-07](komponenty/K16-loader.md#3-wymagania) | P | przegląd kodu |
| [REQ-K16-08](komponenty/K16-loader.md#3-wymagania) | M, P | przegląd kodu, program z `FAST` przy zajętym obszarze |
| [REQ-K16-09](komponenty/K16-loader.md#3-wymagania) | M, P | `snes -b` (sumy kontrolne z `FAST` i bez), przegląd kodu |
| [REQ-K16-10](komponenty/K16-loader.md#3-wymagania) | M, P | przegląd kodu, `kmon mem` po końcu programu |
| [REQ-K16-11](komponenty/K16-loader.md#3-wymagania) | M, P | przegląd kodu, `crtos-app check` |
| [REQ-K16-12](komponenty/K16-loader.md#3-wymagania) | A, M | `xiptest` z `/flash0` i z karty (adres kodu), `apptest_xip` |
| [REQ-K16-13](komponenty/K16-loader.md#3-wymagania) | A, M | `xiptest` (wątek), `apptest_xip` (wątki, pthread) |
| [REQ-K16-14](komponenty/K16-loader.md#3-wymagania) | M, P | przegląd kodu, `xiptest crash` |
| [REQ-K17-01](komponenty/K17-drzewo-urzadzen.md#3-wymagania) | P | przegląd kodu |
| [REQ-K17-02](komponenty/K17-drzewo-urzadzen.md#3-wymagania) | M | `kmon devices`, `kmon dt` |
| [REQ-K17-03](komponenty/K17-drzewo-urzadzen.md#3-wymagania) | M, P | przegląd kodu, start płytki |
| [REQ-K17-04](komponenty/K17-drzewo-urzadzen.md#3-wymagania) | M | start płytki (kolejność modułów dowolna) |
| [REQ-K17-05](komponenty/K17-drzewo-urzadzen.md#3-wymagania) | P | przegląd kodu, `rmmod`/`insmod` |
| [REQ-K17-06](komponenty/K17-drzewo-urzadzen.md#3-wymagania) | P | przegląd kodu |
| [REQ-K17-07](komponenty/K17-drzewo-urzadzen.md#3-wymagania) | M | `devmgr` (U02) przy restarcie |
| [REQ-K18-01](komponenty/K18-start.md#3-wymagania) | P | przegląd kodu (`main`) |
| [REQ-K18-02](komponenty/K18-start.md#3-wymagania) | P | przegląd kodu (`kernel_main`) |
| [REQ-K18-03](komponenty/K18-start.md#3-wymagania) | M | test ręczny |
| [REQ-K18-04](komponenty/K18-start.md#3-wymagania) | M | start płytki (`kmon lsmod`, `devices`) |
| [REQ-K18-05](komponenty/K18-start.md#3-wymagania) | M | test ręczny (np. bez karty) |
| [REQ-K18-06](komponenty/K18-start.md#3-wymagania) | M | `kmon procs` |
| [REQ-K18-07](komponenty/K18-start.md#3-wymagania) | M | obraz RTOS z `examples/rtos/blinky` na płytce: `dmesg`, `ps` (03.10.2026) |
| [REQ-K19-01](komponenty/K19-kmon.md#3-wymagania) | M | start bez karty (test ręczny) |
| [REQ-K19-02](komponenty/K19-kmon.md#3-wymagania) | M | `crtos kmon` przy otwartym `crtos serial` |
| [REQ-K19-03](komponenty/K19-kmon.md#3-wymagania) | P | przegląd kodu |
| [REQ-K19-04](komponenty/K19-kmon.md#3-wymagania) | A | `crtos kmon "test all"` |
| [REQ-K19-05](komponenty/K19-kmon.md#3-wymagania) | M | `crtos put`, `crtos deploy --swd` |
| [REQ-K19-06](komponenty/K19-kmon.md#3-wymagania) | A, M | `test all`: OS 15/15, RTOS 10/10; `help` w RTOS 11 poleceń; `nm` obrazu RTOS |
| [REQ-K20-01](komponenty/K20-pamiec-emulowana.md#3-wymagania) | A, P | `heaptest` (instrukcje z C i asemblera, przez granicę stron, bez wyrównania), przegląd kodu |
| [REQ-K20-02](komponenty/K20-pamiec-emulowana.md#3-wymagania) | A, P | przegląd kodu (`span`), `heaptest` (przemiatanie większe niż pamięć podręczna) |
| [REQ-K20-03](komponenty/K20-pamiec-emulowana.md#3-wymagania) | P | przegląd kodu |
| [REQ-K20-04](komponenty/K20-pamiec-emulowana.md#3-wymagania) | A, P | `heaptest` (6 MB przy pamięci podręcznej 512 KB), przegląd kodu |
| [REQ-K20-05](komponenty/K20-pamiec-emulowana.md#3-wymagania) | A, P | przegląd kodu (`syscall_enter_fault`), `heaptest` (dwa wątki) |
| [REQ-K20-06](komponenty/K20-pamiec-emulowana.md#3-wymagania) | A | `heaptest` (`open` ze ścieżką, `write`/`read` 64 KB, `pipe`, `VMEM_INFO`) |
| [REQ-K20-07](komponenty/K20-pamiec-emulowana.md#3-wymagania) | A | `kmon vmem` po `heaptest` (0 regionów, brak pamięci podręcznej) |
| [REQ-K20-08](komponenty/K20-pamiec-emulowana.md#3-wymagania) | P | przegląd kodu |
| [REQ-K21-01](komponenty/K21-api-rtos.md#3-wymagania) | A | `kmon test queue` |
| [REQ-K21-02](komponenty/K21-api-rtos.md#3-wymagania) | A | `kmon test queue` (wysłanie z przerwania) |
| [REQ-K21-03](komponenty/K21-api-rtos.md#3-wymagania) | A, P | `kmon test queue` (usunięcie kolejki), przegląd kodu |
| [REQ-K21-04](komponenty/K21-api-rtos.md#3-wymagania) | A | `kmon test timer` |
| [REQ-K21-05](komponenty/K21-api-rtos.md#3-wymagania) | P | przegląd kodu, `nm` (ITCM/DTCM) |
| [REQ-K21-06](komponenty/K21-api-rtos.md#3-wymagania) | A, M | budowanie `--rtos` (`nm`), obraz RTOS na płytce: blinky, `test all` 10/10 |
| [REQ-S01-01](komponenty/S01-zegary-piny-gpio.md#3-wymagania) | M | start płytki (kolejność modułów) |
| [REQ-S01-02](komponenty/S01-zegary-piny-gpio.md#3-wymagania) | P | przegląd kodu |
| [REQ-S01-03](komponenty/S01-zegary-piny-gpio.md#3-wymagania) | P | przegląd kodu, `rmmod`/`insmod` sterownika |
| [REQ-S01-04](komponenty/S01-zegary-piny-gpio.md#3-wymagania) | P | przegląd kodu |
| [REQ-S01-05](komponenty/S01-zegary-piny-gpio.md#3-wymagania) | P | przegląd kodu, działanie ekranu i PHY |
| [REQ-S02-01](komponenty/S02-i2c-spi.md#3-wymagania) | P | przegląd kodu |
| [REQ-S02-02](komponenty/S02-i2c-spi.md#3-wymagania) | P | przegląd kodu, `spi` (A03) |
| [REQ-S02-03](komponenty/S02-i2c-spi.md#3-wymagania) | M | `crtos run spi` (A03) |
| [REQ-S02-04](komponenty/S02-i2c-spi.md#3-wymagania) | P | przegląd kodu |
| [REQ-S02-05](komponenty/S02-i2c-spi.md#3-wymagania) | M | `crtos run spi -s ...` |
| [REQ-S03-01](komponenty/S03-fb-gpu2d.md#3-wymagania) | A, M | `kmon fbtest`, obserwacja ekranu |
| [REQ-S03-02](komponenty/S03-fb-gpu2d.md#3-wymagania) | M | `gfxinfo` (U03, 58 fps) |
| [REQ-S03-03](komponenty/S03-fb-gpu2d.md#3-wymagania) | A, M, P | przegląd kodu, `kmon gpu2dtest` |
| [REQ-S03-04](komponenty/S03-fb-gpu2d.md#3-wymagania) | P | przegląd kodu |
| [REQ-S03-05](komponenty/S03-fb-gpu2d.md#3-wymagania) | P | przegląd kodu |
| [REQ-S03-06](komponenty/S03-fb-gpu2d.md#3-wymagania) | A, M | `kmon gpu2dtest` |
| [REQ-S03-07](komponenty/S03-fb-gpu2d.md#3-wymagania) | M, P | przegląd kodu; `kmon fb` przy działającym `gfxd`: odmowa, po jego zakończeniu: zmiana |
| [REQ-S04-01](komponenty/S04-wejscie.md#3-wymagania) | P | przegląd kodu |
| [REQ-S04-02](komponenty/S04-wejscie.md#3-wymagania) | P | przegląd kodu |
| [REQ-S04-03](komponenty/S04-wejscie.md#3-wymagania) | M | `crtos run evtest` |
| [REQ-S04-04](komponenty/S04-wejscie.md#3-wymagania) | P | przegląd kodu, odłączenie myszy USB |
| [REQ-S04-05](komponenty/S04-wejscie.md#3-wymagania) | P | przegląd kodu |
| [REQ-S04-06](komponenty/S04-wejscie.md#3-wymagania) | M, P | przegląd kodu, `inputd` („event1 (…), gamepad”) |
| [REQ-S05-01](komponenty/S05-siec.md#3-wymagania) | A, M, P | przegląd kodu, `apptest`/`nc` (zabicie w trakcie) |
| [REQ-S05-02](komponenty/S05-siec.md#3-wymagania) | P | przegląd kodu |
| [REQ-S05-03](komponenty/S05-siec.md#3-wymagania) | P | przegląd kodu |
| [REQ-S05-04](komponenty/S05-siec.md#3-wymagania) | P | przegląd kodu |
| [REQ-S05-05](komponenty/S05-siec.md#3-wymagania) | P | przegląd kodu |
| [REQ-S05-06](komponenty/S05-siec.md#3-wymagania) | P | przegląd kodu |
| [REQ-S05-07](komponenty/S05-siec.md#3-wymagania) | M | `crtos netbench`, ping i echo UDP (D05) |
| [REQ-S05-08](komponenty/S05-siec.md#3-wymagania) | M | profil PC przez DWT_PCSR: kod z flasha 11% → 0,1% próbek |
| [REQ-D01-01](komponenty/D01-platforma.md#3-wymagania) | P | przegląd kodu |
| [REQ-D01-02](komponenty/D01-platforma.md#3-wymagania) | P | przegląd kodu |
| [REQ-D01-03](komponenty/D01-platforma.md#3-wymagania) | P | przegląd kodu |
| [REQ-D01-04](komponenty/D01-platforma.md#3-wymagania) | M, P | przegląd kodu, SW8 (`evtest`) |
| [REQ-D01-05](komponenty/D01-platforma.md#3-wymagania) | P | przegląd kodu |
| [REQ-D01-06](komponenty/D01-platforma.md#3-wymagania) | M, P | przegląd kodu; `fbtest` po 10 zmianach trybu: 58,7 i 59,1 Hz (przed poprawką 117 Hz) |
| [REQ-D02-01](komponenty/D02-magistrale.md#3-wymagania) | P | przegląd kodu |
| [REQ-D02-02](komponenty/D02-magistrale.md#3-wymagania) | M | `kmon i2cdetect 1` |
| [REQ-D02-03](komponenty/D02-magistrale.md#3-wymagania) | M | `crtos run spi -c -r 10000 9f` |
| [REQ-D02-04](komponenty/D02-magistrale.md#3-wymagania) | M | `CFGR1.OUTCFG` = 1 (odczyt przez SWD, 27.09.2026) |
| [REQ-D02-05](komponenty/D02-magistrale.md#3-wymagania) | P | przegląd kodu, start |
| [REQ-D02-06](komponenty/D02-magistrale.md#3-wymagania) | P | przegląd kodu |
| [REQ-D03-01](komponenty/D03-ekran.md#3-wymagania) | M | obserwacja (brak rozrywania), `gfxinfo` |
| [REQ-D03-02](komponenty/D03-ekran.md#3-wymagania) | M | `gfxinfo` (58 fps) |
| [REQ-D03-03](komponenty/D03-ekran.md#3-wymagania) | P | przegląd kodu |
| [REQ-D03-04](komponenty/D03-ekran.md#3-wymagania) | A, M, P | przegląd kodu, `kmon gpu2dtest`, kopia ekranu `vncd` w pamięci z cache = `crtos shot` |
| [REQ-D03-05](komponenty/D03-ekran.md#3-wymagania) | M | start płytki |
| [REQ-D03-06](komponenty/D03-ekran.md#3-wymagania) | M | `kmon fb` + `fbtest`: 10 zmian 480×272 ↔ 800×480 (58,7 i 59,1 Hz), brak trybu 1024×600, pula `ncache` bez ubytku |
| [REQ-D03-07](komponenty/D03-ekran.md#3-wymagania) | M, P | przegląd kodu; 10 zmian trybu bez zawieszenia (przed poprawką zawieszenie w `ELCDIF_Reset`, ramki zatrzymane w 4 z 5 zmian) |
| [REQ-D04-01](komponenty/D04-wejscie.md#3-wymagania) | P | przegląd kodu |
| [REQ-D04-02](komponenty/D04-wejscie.md#3-wymagania) | M | `evtest` (ręcznie) |
| [REQ-D04-03](komponenty/D04-wejscie.md#3-wymagania) | M | `evtest` |
| [REQ-D04-04](komponenty/D04-wejscie.md#3-wymagania) | M | `evtest` z SW8 |
| [REQ-D04-05](komponenty/D04-wejscie.md#3-wymagania) | M, P | przegląd kodu, `dmesg` („5 points”) |
| [REQ-D04-06](komponenty/D04-wejscie.md#3-wymagania) | M, P | przegląd kodu, `dmesg` („pad link up (…, 0 bad)”) |
| [REQ-D04-07](komponenty/D04-wejscie.md#3-wymagania) | P | przegląd kodu |
| [REQ-D04-08](komponenty/D04-wejscie.md#3-wymagania) | M, P | przegląd kodu, `evtest /dev/event1` |
| [REQ-D04-09](komponenty/D04-wejscie.md#3-wymagania) | M, P | przegląd kodu, `evtest /dev/event1` |
| [REQ-D04-10](komponenty/D04-wejscie.md#3-wymagania) | M, P | przegląd kodu, `kmon evtest event1` (z padem) |
| [REQ-D04-11](komponenty/D04-wejscie.md#3-wymagania) | P | przegląd kodu |
| [REQ-D04-12](komponenty/D04-wejscie.md#3-wymagania) | M, P | przegląd kodu, `evtest` (ręcznie, dwa palce) |
| [REQ-D04-13](komponenty/D04-wejscie.md#3-wymagania) | M, P | przegląd kodu; `dmesg`: `touchscreen@14` (GT911, 800×480) przypisany; zmiana trybu przez `fb_suggest_size` sprawdzona `kmon fb` |
| [REQ-D05-01](komponenty/D05-siec.md#3-wymagania) | M, P | przegląd kodu, `crtos netbench` |
| [REQ-D05-02](komponenty/D05-siec.md#3-wymagania) | M | `kmon net` (`errors`) |
| [REQ-D05-03](komponenty/D05-siec.md#3-wymagania) | M, P | przegląd kodu, `crtos netbench` (TCP z płytki 94,7 Mbit/s, 0 odrzuconych) |
| [REQ-D05-04](komponenty/D05-siec.md#3-wymagania) | P | przegląd kodu |
| [REQ-D05-05](komponenty/D05-siec.md#3-wymagania) | M | `kmon lsmod` (USED 1) |
| [REQ-D05-06](komponenty/D05-siec.md#3-wymagania) | M | ping 32–8000 B w obie strony, echo UDP 32–2047 B (fragmenty) |
| [REQ-D05-07](komponenty/D05-siec.md#3-wymagania) | M | `crtos netbench` z `gfxdemo`: 94,8 Mbit/s, 0 uszkodzonych ramek |
| [REQ-D05-08](komponenty/D05-siec.md#3-wymagania) | M | `crtos netbench --time 10`: TCP 89,9–94,7 Mbit/s, UDP 94,5–95,7 Mbit/s |
| [REQ-D05-09](komponenty/D05-siec.md#3-wymagania) | A, M | `apptest` (gniazda, `poll`), `crtos netbench` |
| [REQ-D06-01](komponenty/D06-usb.md#3-wymagania) | P | przegląd kodu |
| [REQ-D06-02](komponenty/D06-usb.md#3-wymagania) | M | test z komputerem (getty, U07) |
| [REQ-D06-03](komponenty/D06-usb.md#3-wymagania) | M | test ręczny (`usb: cable unplugged`) |
| [REQ-D06-04](komponenty/D06-usb.md#3-wymagania) | P | przegląd kodu (`tusb_breakpoint`) |
| [REQ-D06-05](komponenty/D06-usb.md#3-wymagania) | M | test ręczny (tryb host) |
| [REQ-D06-06](komponenty/D06-usb.md#3-wymagania) | M, P | `dmesg` po starcie (USB1 urządzenie, USB2 host), przegląd kodu |
| [REQ-D06-07](komponenty/D06-usb.md#3-wymagania) | M | parser na komputerze (dwa deskryptory), odbiornik 046d:c534 na J10 („wheel: report protocol”) |
| [REQ-D06-08](komponenty/D06-usb.md#3-wymagania) | M | burza przerwań po restarcie sondą zatrzymana zapisem rejestrów przez SWD; po poprawce restarty bez burzy |
| [REQ-D07-01](komponenty/D07-flash-mtd.md#3-wymagania) | P | `mtd info`, przegląd kodu |
| [REQ-D07-02](komponenty/D07-flash-mtd.md#3-wymagania) | M, P | przegląd kodu, `mtd test` |
| [REQ-D07-03](komponenty/D07-flash-mtd.md#3-wymagania) | M | `mtd test` (0 różnic), start po aktualizacji |
| [REQ-D07-04](komponenty/D07-flash-mtd.md#3-wymagania) | M | `crtos flash --net` ze złym plikiem (27.09.2026: `ERR Invalid argument`) |
| [REQ-D07-05](komponenty/D07-flash-mtd.md#3-wymagania) | M | `crtos flash --net` (27.09.2026: nowe jądro wystartowało) |
| [REQ-D07-06](komponenty/D07-flash-mtd.md#3-wymagania) | P | przegląd kodu |
| [REQ-D07-07](komponenty/D07-flash-mtd.md#3-wymagania) | M | `mtd write mtd1` przy zamontowanym `/flash0` → `EBUSY` |
| [REQ-D07-08](komponenty/D07-flash-mtd.md#3-wymagania) | A, M | `flashtest.sh`, programy XIP (K16) |
| [REQ-D08-01](komponenty/D08-trng-rtc.md#3-wymagania) | P | przegląd kodu |
| [REQ-D08-02](komponenty/D08-trng-rtc.md#3-wymagania) | P | przegląd kodu |
| [REQ-D08-03](komponenty/D08-trng-rtc.md#3-wymagania) | P | przegląd kodu |
| [REQ-D08-04](komponenty/D08-trng-rtc.md#3-wymagania) | P | przegląd kodu |
| [REQ-D08-05](komponenty/D08-trng-rtc.md#3-wymagania) | P | przegląd kodu, start po wyjęciu baterii |
| [REQ-D09-01](komponenty/D09-dzwiek.md#3-wymagania) | P | przegląd kodu |
| [REQ-D09-02](komponenty/D09-dzwiek.md#3-wymagania) | M, P | przegląd kodu, `audio` (licznik „FIFO errors”) |
| [REQ-D09-03](komponenty/D09-dzwiek.md#3-wymagania) | M | `audio` w trakcie gry (27.09.2026) |
| [REQ-D09-04](komponenty/D09-dzwiek.md#3-wymagania) | P | przegląd kodu |
| [REQ-D09-05](komponenty/D09-dzwiek.md#3-wymagania) | M, P | przegląd kodu, `audio tone 440 2000` (96000 ramek w 2 s) |
| [REQ-D09-06](komponenty/D09-dzwiek.md#3-wymagania) | P | przegląd kodu |
| [REQ-D10-01](komponenty/D10-flashfs.md#3-wymagania) | M | reset w trakcie kopiowania 4,4 MB |
| [REQ-D10-02](komponenty/D10-flashfs.md#3-wymagania) | M, P | przegląd kodu, reset w trakcie zapisu |
| [REQ-D10-03](komponenty/D10-flashfs.md#3-wymagania) | P | przegląd kodu; program XIP z `/flash0` (K16) |
| [REQ-D10-04](komponenty/D10-flashfs.md#3-wymagania) | A, M | `flashtest.sh`, zapis po przerwanym pliku |
| [REQ-D10-05](komponenty/D10-flashfs.md#3-wymagania) | M | `mtd write mtd1` → `EBUSY` |
| [REQ-D10-06](komponenty/D10-flashfs.md#3-wymagania) | M, P | przegląd kodu, `flashfs format yes` |
| [REQ-D10-07](komponenty/D10-flashfs.md#3-wymagania) | A | `flashtest.sh` (`flashfs check`) |
| [REQ-D10-08](komponenty/D10-flashfs.md#3-wymagania) | A | `flashtest.sh` (dopisanie odrzucone) |
| [REQ-D10-09](komponenty/D10-flashfs.md#3-wymagania) | M, P | `crtos toolchain install` (`cc1`, `cc1plus` obok siebie), przegląd kodu |
| [REQ-U01-01](komponenty/U01-init.md#3-wymagania) | M | `crtos kmon procs` |
| [REQ-U01-02](komponenty/U01-init.md#3-wymagania) | M | test ręczny (`kill -p`), log `restarting later` |
| [REQ-U01-03](komponenty/U01-init.md#3-wymagania) | P | przegląd kodu |
| [REQ-U01-04](komponenty/U01-init.md#3-wymagania) | P | przegląd kodu |
| [REQ-U02-01](komponenty/U02-devmgr.md#3-wymagania) | M | `inputd` po restarcie (ręcznie) |
| [REQ-U02-02](komponenty/U02-devmgr.md#3-wymagania) | P | przegląd kodu |
| [REQ-U02-03](komponenty/U02-devmgr.md#3-wymagania) | P | przegląd kodu |
| [REQ-U03-01](komponenty/U03-gfxd.md#3-wymagania) | M | obserwacja, `gfxinfo` |
| [REQ-U03-02](komponenty/U03-gfxd.md#3-wymagania) | M | `gfxdemo` (płynna animacja 58 fps) |
| [REQ-U03-03](komponenty/U03-gfxd.md#3-wymagania) | P | przegląd kodu |
| [REQ-U03-04](komponenty/U03-gfxd.md#3-wymagania) | M | zabicie programu z oknem (`sysmon`) |
| [REQ-U03-05](komponenty/U03-gfxd.md#3-wymagania) | M, P | przegląd kodu, restart `wm` |
| [REQ-U03-06](komponenty/U03-gfxd.md#3-wymagania) | P | przegląd kodu |
| [REQ-U03-07](komponenty/U03-gfxd.md#3-wymagania) | M, P | przegląd kodu, `gfxtap stick`, gra `voxel` |
| [REQ-U03-08](komponenty/U03-gfxd.md#3-wymagania) | M, P | przegląd kodu, `gfxtap hold` z oknem `snes` (zrzut: dwa przyciski wciśnięte) |
| [REQ-U03-09](komponenty/U03-gfxd.md#3-wymagania) | M | `gfxtap key 125` (menu, zrzut), strzałki i Enter w menu uruchamiają `term`, Super_L i Esc przez VNC |
| [REQ-U03-10](komponenty/U03-gfxd.md#3-wymagania) | M | `gfxtap wheel` nad menu, listą `sysmon` i terminalem, `gfxtap mdrag` w terminalu (zrzuty) |
| [REQ-U03-11](komponenty/U03-gfxd.md#3-wymagania) | M | `term` Ctrl+C/Ctrl+V, schowek przez VNC w obie strony (`cliptest`) |
| [REQ-U03-12](komponenty/U03-gfxd.md#3-wymagania) | M, P | `crtos desktop --shot` = `crtos shot` (poza kursorem), przegląd kodu |
| [REQ-U03-13](komponenty/U03-gfxd.md#3-wymagania) | M, P | `gfxtap hover` i ruch przez VNC nad paskiem zadań i menu; przegląd kodu |
| [REQ-U03-14](komponenty/U03-gfxd.md#3-wymagania) | M | `appearance wallpaper`: tapety wbudowane, kolor, plik z `crtos wallpaper`; zmiana skali przestawia `wm` i programy |
| [REQ-U04-01](komponenty/U04-inputd-osk.md#3-wymagania) | M, P | przegląd kodu, `gfxtap`/dotyk |
| [REQ-U04-02](komponenty/U04-inputd-osk.md#3-wymagania) | P | przegląd kodu |
| [REQ-U04-03](komponenty/U04-inputd-osk.md#3-wymagania) | P | przegląd kodu |
| [REQ-U04-04](komponenty/U04-inputd-osk.md#3-wymagania) | M | test ręczny z klawiaturą USB |
| [REQ-U04-05](komponenty/U04-inputd-osk.md#3-wymagania) | M | test ręczny (`term`) |
| [REQ-U04-06](komponenty/U04-inputd-osk.md#3-wymagania) | M, P | przegląd kodu, `kmon evtest event1` i gra `voxel` z padem |
| [REQ-U04-07](komponenty/U04-inputd-osk.md#3-wymagania) | M, P | przegląd kodu, dotyk dwoma palcami (ręcznie) |
| [REQ-U04-08](komponenty/U04-inputd-osk.md#3-wymagania) | M, P | przegląd kodu; kółko myszy USB (ręcznie) |
| [REQ-U04-09](komponenty/U04-inputd-osk.md#3-wymagania) | M, P | klawiatura ekranowa przy 100 i 150% (zrzuty); przegląd kodu |
| [REQ-U05-01](komponenty/U05-netmgr.md#3-wymagania) | P | przegląd kodu |
| [REQ-U05-02](komponenty/U05-netmgr.md#3-wymagania) | M | log startu (`netmgr: eth0: asking DHCP`) |
| [REQ-U05-03](komponenty/U05-netmgr.md#3-wymagania) | M | log `clock set by netmgr` |
| [REQ-U06-01](komponenty/U06-httpd-deployd.md#3-wymagania) | P | przegląd kodu |
| [REQ-U06-02](komponenty/U06-httpd-deployd.md#3-wymagania) | P | przegląd kodu |
| [REQ-U06-03](komponenty/U06-httpd-deployd.md#3-wymagania) | M, P | przegląd kodu, `crtos deploy` pliku z `build/flash0`, odmowy `crtos scp` (`/sd/snes`, `..`, `/dev`) |
| [REQ-U06-04](komponenty/U06-httpd-deployd.md#3-wymagania) | M | `crtos deploy` (każdy plik) |
| [REQ-U06-05](komponenty/U06-httpd-deployd.md#3-wymagania) | M | `crtos flash --net` (27.09.2026, także zły obraz) |
| [REQ-U06-06](komponenty/U06-httpd-deployd.md#3-wymagania) | M | `crtos toolchain install` (ponowne wysłanie `cc1` na miejsce starego) |
| [REQ-U06-07](komponenty/U06-httpd-deployd.md#3-wymagania) | M | `crtos scp -r` tam i z powrotem (pliki identyczne, nazwy ze spacjami) |
| [REQ-U07-01](komponenty/U07-getty.md#3-wymagania) | M | test z komputerem (27.09.2026) |
| [REQ-U07-02](komponenty/U07-getty.md#3-wymagania) | P | przegląd kodu |
| [REQ-U07-03](komponenty/U07-getty.md#3-wymagania) | P | przegląd kodu |
| [REQ-U07-04](komponenty/U07-getty.md#3-wymagania) | M | COM6 (pyserial): Ctrl-C w edytorze wiersza porzuca wiersz |
| [REQ-U08-01](komponenty/U08-vncd.md#3-wymagania) | M, P | DES: wektor FIPS 46 i OpenSSL; `crtos desktop`; przegląd kodu |
| [REQ-U08-02](komponenty/U08-vncd.md#3-wymagania) | M | `crtos desktop --shot` = `crtos shot` (poza kursorem myszy) |
| [REQ-U08-03](komponenty/U08-vncd.md#3-wymagania) | M, P | klient testowy RFB: Zlib, Hextile, Raw w 16 i 32 bitach = `crtos shot`; przegląd kodu |
| [REQ-U08-04](komponenty/U08-vncd.md#3-wymagania) | M, P | przegląd kodu, klient testowy RFB |
| [REQ-U08-05](komponenty/U08-vncd.md#3-wymagania) | M | klient testowy RFB: Super_L otwiera menu, Esc zamyka |
| [REQ-U08-06](komponenty/U08-vncd.md#3-wymagania) | M | klient testowy RFB + `cliptest`: oba kierunki, bez echa |
| [REQ-U08-07](komponenty/U08-vncd.md#3-wymagania) | M, P | `dmesg` (kopia ekranu, połączenie i rozłączenie), przegląd kodu |
| [REQ-U08-08](komponenty/U08-vncd.md#3-wymagania) | M | `deflate.c` na komputerze z `zlib` Pythona (pulpit, gra, szum; 8/16/32 bity) – bajt w bajt; klient testowy RFB = `crtos shot` |
| [REQ-U08-09](komponenty/U08-vncd.md#3-wymagania) | M, P | klient testowy RFB (RGB565: Zlib, Raw = `crtos shot`), przegląd kodu |
| [REQ-U08-10](komponenty/U08-vncd.md#3-wymagania) | M | `vncbench`, `crtos desktop --stats`: 59–60 aktualizacji/s (animacja), 57–60 kl./s (przesuwanie okna) |
| [REQ-L01-01](komponenty/L01-libcrtos.md#3-wymagania) | A, P | przegląd kodu, apptest „errors and permissions” |
| [REQ-L01-02](komponenty/L01-libcrtos.md#3-wymagania) | A | `modcheck.py --apps` przy każdym budowaniu |
| [REQ-L01-03](komponenty/L01-libcrtos.md#3-wymagania) | A | apptest „threads and mutexes”, „POSIX threads” |
| [REQ-L01-04](komponenty/L01-libcrtos.md#3-wymagania) | A, P | przegląd kodu, apptest `crash-stack` |
| [REQ-L01-05](komponenty/L01-libcrtos.md#3-wymagania) | P | przegląd kodu, programy C++ z obiektami globalnymi (tftdemo: `TFTLIB_8BIT tft`) |
| [REQ-L01-06](komponenty/L01-libcrtos.md#3-wymagania) | A | apptest „POSIX processes” (plik, potok, `PATH`) |
| [REQ-L01-07](komponenty/L01-libcrtos.md#3-wymagania) | A | apptest „POSIX processes” |
| [REQ-L01-08](komponenty/L01-libcrtos.md#3-wymagania) | A, P | apptest (`tmpfile`, `tmpnam`), przegląd kodu |
| [REQ-L01-09](komponenty/L01-libcrtos.md#3-wymagania) | A, P | `heaptest` (72 216 sprawdzeń, 60 000 losowych operacji z wzorcami i `__crtos_heap_check`), `nm` programów etapu B (brak `__malloc_av_`) |
| [REQ-L01-10](komponenty/L01-libcrtos.md#3-wymagania) | A, P | `heaptest` (8 MB z programu o stercie 1 MB), przegląd kodu |
| [REQ-L01-11](komponenty/L01-libcrtos.md#3-wymagania) | A, P | przegląd kodu, kompilacja NES na płytce (inne programy działają dalej) |
| [REQ-L01-12](komponenty/L01-libcrtos.md#3-wymagania) | A | `heaptest` (bloki 512 KB w pamięci emulowanej, sterta spójna), kompilacja `snes_core.cpp` na płytce |
| [REQ-L01-13](komponenty/L01-libcrtos.md#3-wymagania) | A, M | `heaptest` bez symboli domyślnych (193 351 sprawdzeń), `netsurf` z `crtos_heap_windows = 1`, `crtos_heap_window_kb = 8192`, `crtos_heap_swap = 0` (blok 10 MB w oknie sterty, zmiana rozmiaru okna programu) |
| [REQ-L01-14](komponenty/L01-libcrtos.md#3-wymagania) | A, M | `heaptest` (podwójne zwolnienie w procesie potomnym: kod 134, komunikat z adresami), `appsym.py` (adres wywołującego → `heaptest.c`) |
| [REQ-L02-01](komponenty/L02-libgfx.md#3-wymagania) | P | przegląd kodu; błąd kończyłby tylko program (arena, K03) |
| [REQ-L02-02](komponenty/L02-libgfx.md#3-wymagania) | M | restart `gfxd` (ręcznie) |
| [REQ-L02-03](komponenty/L02-libgfx.md#3-wymagania) | M | test ręczny (zmiana rozmiaru) |
| [REQ-L02-04](komponenty/L02-libgfx.md#3-wymagania) | P | przegląd kodu |
| [REQ-L02-05](komponenty/L02-libgfx.md#3-wymagania) | P | przegląd kodu |
| [REQ-L02-06](komponenty/L02-libgfx.md#3-wymagania) | M, P | przegląd kodu, `edit` w `term`, Insert w wierszu poleceń (`gfxtap key 110`) |
| [REQ-L02-07](komponenty/L02-libgfx.md#3-wymagania) | M | `term` Ctrl+C/Ctrl+V, `cliptest` z klientem VNC |
| [REQ-L02-08](komponenty/L02-libgfx.md#3-wymagania) | M | `image.c` na komputerze: pliki obcięte, za duże, śmieci, brak pliku; PAM = PNG |
| [REQ-L02-09](komponenty/L02-libgfx.md#3-wymagania) | M | `image.c` na komputerze: skala zgodna z dokładną średnią (≤ 0,61), rysowanie jak Pillow |
| [REQ-L02-10](komponenty/L02-libgfx.md#3-wymagania) | M | `appearance` na płytce: skale 100–200%, rodziny noto i liberation, przezroczystość, zaokrąglenia; programy zmienione bez restartu |
| [REQ-L02-11](komponenty/L02-libgfx.md#3-wymagania) | M, P | przegląd kodu; cztery rodziny w pięciu skalach na płytce (zrzuty) |
| [REQ-L02-12](komponenty/L02-libgfx.md#3-wymagania) | M | `wallpaper.c` na komputerze (6 tapet, plik w trybach fill/fit/center); tapeta z pliku na płytce w 101 ms |
| [REQ-L02-13](komponenty/L02-libgfx.md#3-wymagania) | M, P | zrzuty (przyciski, menu, paski tytułu, przełączniki); przegląd kodu |
| [REQ-A01-01](komponenty/A01-wm.md#3-wymagania) | P | przegląd kodu; jądro i tak przycina uprawnienia (K08) |
| [REQ-A01-02](komponenty/A01-wm.md#3-wymagania) | P | przegląd kodu |
| [REQ-A01-03](komponenty/A01-wm.md#3-wymagania) | M | restart z programu Settings (ręcznie) |
| [REQ-A01-04](komponenty/A01-wm.md#3-wymagania) | M, P | przegląd kodu, test ręczny |
| [REQ-A01-05](komponenty/A01-wm.md#3-wymagania) | M | test ręczny |
| [REQ-A01-06](komponenty/A01-wm.md#3-wymagania) | M | `gfxtap key 125/108/103/28` (zrzuty), Super_L i Esc przez VNC |
| [REQ-A01-07](komponenty/A01-wm.md#3-wymagania) | M, P | zrzuty: menu, pasek zadań i paski tytułu z ikonami; przegląd kodu |
| [REQ-A01-08](komponenty/A01-wm.md#3-wymagania) | M | `gfxtap hover`, przytrzymanie palcem (`gfxtap drag` w miejscu), mysz przez VNC |
| [REQ-A01-09](komponenty/A01-wm.md#3-wymagania) | M | `appearance`: skale 100/150/200%, przezroczystość, krycie, zaokrąglenia (zrzuty paska, menu, ramek) |
| [REQ-A01-10](komponenty/A01-wm.md#3-wymagania) | M, P | Settings przy 200% wraca na ekran (zrzut); przegląd kodu |
| [REQ-A01-11](komponenty/A01-wm.md#3-wymagania) | P | przegląd kodu (`load_apps`), budowanie `--base` |
| [REQ-A02-01](komponenty/A02-aplikacje.md#3-wymagania) | M | test ręczny ([x] w ramce) |
| [REQ-A02-02](komponenty/A02-aplikacje.md#3-wymagania) | M | test ręczny (uchwyt, pełny ekran) |
| [REQ-A02-03](komponenty/A02-aplikacje.md#3-wymagania) | P | przegląd kodu |
| [REQ-A02-04](komponenty/A02-aplikacje.md#3-wymagania) | M, P | przegląd kodu, test ręczny |
| [REQ-A02-05](komponenty/A02-aplikacje.md#3-wymagania) | A, M | apptest `crash-*` (K04), obserwacja |
| [REQ-A02-06](komponenty/A02-aplikacje.md#3-wymagania) | M | `gfxtap mdrag`, `key 29+46`, `key 29+47` (zrzuty: zaznaczenie, wklejony tekst) |
| [REQ-A02-07](komponenty/A02-aplikacje.md#3-wymagania) | M, P | `gfxtap wheel` (terminal, `sysmon`, menu); NetSurf – przegląd kodu |
| [REQ-A02-08](komponenty/A02-aplikacje.md#3-wymagania) | M | na płytce: pasek adresu (zaznaczenie, Ctrl+A/C/V), wyszukiwanie DuckDuckGo z paska |
| [REQ-A02-09](komponenty/A02-aplikacje.md#3-wymagania) | M | na płytce: strona A podmienia `Array.prototype.join` i prototyp DOM, strona B ma własne |
| [REQ-A02-10](komponenty/A02-aplikacje.md#3-wymagania) | M | `netsurf -v` na płytce: 1,08 s pierwsza strona, 0,22 s następne, brak dla stron bez skryptów |
| [REQ-A02-11](komponenty/A02-aplikacje.md#3-wymagania) | M | strona testowa na płytce: kolejność zdarzeń, ok. 50 sprawdzeń API |
| [REQ-A02-12](komponenty/A02-aplikacje.md#3-wymagania) | M | 41 przeładowań strony ze skryptem co 0,4 s, `crtos-app info` (arena 12288 KB); 30 przeładowań ze stałą stertą, blok 10 MB w oknie sterty, SDRAM oddana przy wyjściu |
| [REQ-A02-13](komponenty/A02-aplikacje.md#3-wymagania) | M | test przeglądarki Google: przerwany po 10 s, z `script_timeout:60` do końca |
| [REQ-A02-14](komponenty/A02-aplikacje.md#3-wymagania) | M, P | przegląd kodu (`schedule.c`, pasek stanu, `fbtk_set_text`), awarie sprzed poprawek (`crtos crash`), dwie strony wyczerpujące pamięć (DOM i układ, 16 dużych skryptów): bez awarii, następna strona działa |
| [REQ-A02-15](komponenty/A02-aplikacje.md#3-wymagania) | M | zmiany przez Settings i `appearance`, `ui.cfg` po każdej |
| [REQ-A02-16](komponenty/A02-aplikacje.md#3-wymagania) | M, P | miniatura pliku z `crtos wallpaper`; przegląd kodu |
| [REQ-A02-17](komponenty/A02-aplikacje.md#3-wymagania) | M | 100 → 150 → 200 → 150% z otwartymi `settings`, `sysmon`, `files`, `term` (zrzuty) |
| [REQ-A03-01](komponenty/A03-programy-konsolowe.md#3-wymagania) | A, M | `shtest.sh` (12, 16), test ręczny |
| [REQ-A03-02](komponenty/A03-programy-konsolowe.md#3-wymagania) | M, P | przegląd kodu, Ctrl-C w `getty` i `term`; test na rurach terminalowych (wbudowane \| program, skrypt po programie) |
| [REQ-A03-03](komponenty/A03-programy-konsolowe.md#3-wymagania) | P | przegląd kodu (K08) |
| [REQ-A03-04](komponenty/A03-programy-konsolowe.md#3-wymagania) | A, M | `crtos run apptest` (1762 sprawdzenia, 0 błędów) |
| [REQ-A03-05](komponenty/A03-programy-konsolowe.md#3-wymagania) | M, P | przegląd kodu, test na partycji danych |
| [REQ-A03-06](komponenty/A03-programy-konsolowe.md#3-wymagania) | A | `shtest.sh` (20 sprawdzeń) |
| [REQ-A03-07](komponenty/A03-programy-konsolowe.md#3-wymagania) | M, P | przegląd kodu, test przez konsolę szeregową |
| [REQ-A03-08](komponenty/A03-programy-konsolowe.md#3-wymagania) | A | `maketest.sh` |
| [REQ-A03-09](komponenty/A03-programy-konsolowe.md#3-wymagania) | A, M | konsola szeregowa i USB (pyserial), `term` (zrzuty), `shtest.sh` 20/20 |
| [REQ-A03-10](komponenty/A03-programy-konsolowe.md#3-wymagania) | M | `where` ze wzorcem, `-t`, `-r`, `-q` (30.09.2026) |
| [REQ-A03-11](komponenty/A03-programy-konsolowe.md#3-wymagania) | M | `crtos netbench` (02.10.2026) |
| [REQ-A03-12](komponenty/A03-programy-konsolowe.md#3-wymagania) | M, P | `appearance` przez kmon: zmiany ustawień, błędny klucz i dopasowanie (kod 2, plik bez zmian); przegląd kodu |
| [REQ-A03-13](komponenty/A03-programy-konsolowe.md#3-wymagania) | M | test na rurach terminalowych jak `getty` (17 sprawdzeń: `sleep`, `cat /dev/zero`, kod 130, klawisze wpisane w trakcie, `wait`, skrypty); `shtest.sh`, `maketest.sh`, `apptest` |
| [REQ-T01-01](komponenty/T01-narzedzia.md#3-wymagania) | A | `modcheck.py` w każdym budowaniu |
| [REQ-T01-02](komponenty/T01-narzedzia.md#3-wymagania) | M | użycie codzienne |
| [REQ-T01-03](komponenty/T01-narzedzia.md#3-wymagania) | P | przegląd kodu (`deploy_files`: tylko `build/sdcard/crtos/` i `build/flash0/`), `deployd` REQ-U06-03 |
| [REQ-T01-04](komponenty/T01-narzedzia.md#3-wymagania) | P | przegląd kodu (`BuildLock`) |
| [REQ-T01-05](komponenty/T01-narzedzia.md#3-wymagania) | P | przegląd kodu |
| [REQ-T01-06](komponenty/T01-narzedzia.md#3-wymagania) | M | `crtos flash` przy zapisanym `/flash0` |
| [REQ-T01-07](komponenty/T01-narzedzia.md#3-wymagania) | A, M, P | `crtos src --check`, `paint` zbudowany na płytce (kod i dane jak z komputera), przegląd kodu |
| [REQ-T01-08](komponenty/T01-narzedzia.md#3-wymagania) | M | `crtos scp -r` tam i z powrotem, odmowy (`/sd/snes`, `..`, `/dev`) |
| [REQ-T01-09](komponenty/T01-narzedzia.md#3-wymagania) | M | `crtos desktop --password`, `crtos desktop --shot` |
| [REQ-T01-10](komponenty/T01-narzedzia.md#3-wymagania) | M | `crtos desktop` zmaksymalizowany, pełny ekran, bez Pillow, `--zoom 2`; kliknięcie przez przeskalowany obraz (menu Apps), czarny pas bez komunikatów |
| [REQ-T01-11](komponenty/T01-narzedzia.md#3-wymagania) | M | ikony 13 programów, program z SDK z `ICON` (szablon `crtos new`) |
| [REQ-T01-12](komponenty/T01-narzedzia.md#3-wymagania) | M | `crtos desktop --stats` (800×480): 59–60 aktualizacji/s, 1,3 ms na klatkę |
| [REQ-T01-13](komponenty/T01-narzedzia.md#3-wymagania) | M | `crtos netbench` (02.10.2026) |
| [REQ-T01-14](komponenty/T01-narzedzia.md#3-wymagania) | M | `crtos wallpaper` z obrazem 1024×640 (PPM 800×500, tapeta ustawiona) |
| [REQ-T01-15](komponenty/T01-narzedzia.md#3-wymagania) | M | DejaVu 11 px z `fontconv.py` bajt w bajt jak w `system/lib/libgfx`; 57 plików `.fnt` |
| [REQ-T01-16](komponenty/T01-narzedzia.md#3-wymagania) | M | `crtos build --base`, pełne budowanie (te same pliki karty), `--rtos`, `flash --rtos` |
| [REQ-T01-17](komponenty/T01-narzedzia.md#3-wymagania) | M, P | pobranie 29 katalogów, budowanie z nich (73/74 pliki identyczne, obraz RTOS – tylko data), zmiana commitu i łatki, odmowa przy własnej zmianie; przegląd kodu |
| [REQ-T02-01](komponenty/T02-toolchain.md#3-wymagania) | M | `hello.c`/`hello.cpp` z `arm-crtos-gcc`/`g++` na płytce; budowa drzewa, apptest, sumy kontrolne |
| [REQ-T02-02](komponenty/T02-toolchain.md#3-wymagania) | M | `hello.cpp` na płytce (`init_priority(200)` przed domyślnym) |
| [REQ-T02-03](komponenty/T02-toolchain.md#3-wymagania) | A, P | przegląd kodu (`crtos.specs`), apptest (1720 sprawdzeń) |
| [REQ-T02-04](komponenty/T02-toolchain.md#3-wymagania) | M, P | przegląd kodu, argument z `>` i `&` w PowerShell |
| [REQ-T02-05](komponenty/T02-toolchain.md#3-wymagania) | M | `crtos-app check` na wszystkich programach i na programie z brakującą biblioteką |
| [REQ-T02-06](komponenty/T02-toolchain.md#3-wymagania) | P | przegląd kodu (`CRTOS_ABI_HEADERS`) |
| [REQ-T02-07](komponenty/T02-toolchain.md#3-wymagania) | A, M | `crtos-app check`, `xiptest` i `apptest_xip` z `/flash0` i z karty, programy kompilatora natywnego |
| [REQ-T02-08](komponenty/T02-toolchain.md#3-wymagania) | P | przegląd kodu (`build.sh fetch`) |
| [REQ-T02-09](komponenty/T02-toolchain.md#3-wymagania) | M, P | test na płytce: `hello.c`, program C++, `sh.c`, `make`, `-mxip`; przegląd kodu (`native-specs.py`) |
| [REQ-T02-10](komponenty/T02-toolchain.md#3-wymagania) | A | `build.sh install` (kontrola długości ścieżek) |
| [REQ-T02-11](komponenty/T02-toolchain.md#3-wymagania) | A | `nm` programów etapu B (brak `__malloc_av_`, jest `grow_brk_by`) |
| [REQ-T02-12](komponenty/T02-toolchain.md#3-wymagania) | A, P | kompilacja na płytce (`cc*.ii` w `/sd/crtos/tmp`), przegląd `libiberty-tmpdir.patch` |
| [REQ-T02-13](komponenty/T02-toolchain.md#3-wymagania) | A | `paint` zbudowany na płytce: kod i dane jak z komputera; `cc1plus -E` osobno (4,6 MB) |
| [REQ-T02-14](komponenty/T02-toolchain.md#3-wymagania) | M | cały SNES zbudowany na płytce (7 plików z pamięcią emulowaną), sumy kontrolne Zelda i Yoshi jak z komputera |
| [REQ-T02-15](komponenty/T02-toolchain.md#3-wymagania) | M | porównanie bibliotek `lib/xip` z bibliotekami etapu A obiekt po obiekcie (3050 z 3057 identycznych, reszta wyjaśniona), `.text` programów XIP bez zmian |

## 5. Weryfikacja projektu

- Dokumenty komponentów powstały z przeglądu kodu (27.09.2026); rozbieżności między kodem,
  komentarzami i zamierzonym działaniem są wpisane w rozdz. 12 komponentów i zebrane
  w [03 Analiza bezpieczeństwa, rozdz. 7](03-analiza-bezpieczenstwa.md#7-znalezione-problemy)
  (ISS-01 … ISS-31).
- Diagramy PlantUML są sprawdzane składniowo (`python tools/diagrams.py --check`).
- Brak niezależnego przeglądu (ta sama osoba / narzędzie pisało kod i dokumentację).

## 6. Pewność narzędzi (ISO 26262-8, rozdz. 11)

TI – wpływ narzędzia (TI1: błąd narzędzia nie może wprowadzić ani ukryć błędu w produkcie,
TI2: pozostałe), TD – wykrywanie błędu narzędzia (TD1 wysokie, TD2 średnie, TD3 niskie),
TCL – poziom pewności (TCL1 = TI1 albo TI2 + TD1; TCL2 = TI2 + TD2; TCL3 = TI2 + TD3).

| Narzędzie | Wersja | Użycie | TI | TD i uzasadnienie | TCL | Kwalifikacja |
|---|---|---|---|---|---|---|
| Arm GNU Toolchain (GCC, binutils) | 14.3.Rel1 (GCC 14.3.1) | kompilacja, łączenie, `strip` | TI2 | TD2 – testy na płytce (jądro, apptest) wykrywają część błędów kodu wynikowego | TCL2 | brak |
| newlib-nano, libgcc | z Arm GNU 14.3.Rel1 | biblioteka C programów | – | komponent oprogramowania (rozdz. 7) | – | – |
| CMake / Ninja | 4.4.3 / 1.13.2 | sterowanie budowaniem | TI2 | TD2 – złe flagi / pominięty plik zwykle psują łączenie lub testy | TCL2 | brak |
| MCUXpresso IDE (headless) | 25.6.136 | alternatywne budowanie jądra (`crtos kbuild`) | TI2 | TD2 – jw. | TCL2 | brak |
| `tools/dtc.py` | repozytorium | DTS → DTB | TI2 | TD2 – błędy wychodzą przy `probe`; `dtc.py --dump`, `kmon dt` | TCL2 | brak |
| `tools/gen_pinfunc.py` | repozytorium | nagłówek funkcji pinów z SDK | TI2 | TD2 – piny sprawdzane działaniem urządzeń | TCL2 | brak |
| `tools/modcheck.py` | repozytorium | kontrola symboli i relokacji | TI2 | TD1 – loader jądra (K16) sprawdza to samo przy ładowaniu | TCL1 | nie wymaga |
| `toolchain/crtos.specs`, `crtos-app.ld`, `arm-crtos-*` (T02) | repozytorium | reguły kompilacji i łączenia programów | TI2 | TD2 – błędny program odrzuca loader albo wykrywają go testy (apptest, sumy kontrolne) | TCL2 | brak |
| `crtos-app` (T02) | repozytorium | kontrola programu przed wgraniem | TI2 | TD1 – loader jądra (K16) sprawdza to samo przy ładowaniu | TCL1 | nie wymaga |
| etap A (`toolchain/native`, T02): GCC krzyżowy i biblioteki XIP | Arm GNU 14.3.Rel1 ze źródeł (SHA-256) | biblioteki `lib/xip`, programy XIP | TI2 | TD2 – ta sama wersja GCC co na PC; `apptest_xip` (1759 sprawdzeń), `xiptest` | TCL2 | brak |
| kompilator na płytce (GCC 14.3.1, binutils 2.44, T02) | etap B z 5 poprawkami GCC (`patches/arm-gnu-toolchain/gcc`) | programy budowane na płytce | TI2 | TD2 – te same reguły i biblioteki co na PC; wyniki sprawdza `crtos-app check` i loader; brak porównania kodu wynikowego z PC | TCL2 | brak – tylko do programów użytkownika |
| `tools/stackcheck.py` | repozytorium | ramki stosu jądra > strażnik | TI2 | TD3 – nic innego tego nie sprawdza | TCL3 | brak |
| `crtos.py`, `netdeploy.py`, `swdcon.py` | repozytorium | wgrywanie plików na kartę | TI2 | TD1 – CRC-32 liczone niezależnie na płytce (`deployd`, `kmon savestage`) | TCL1 | nie wymaga |
| pyOCD / LinkServer | 0.45.1 / z MCUXpresso | zapis jądra do flash | TI2 | TD2 – start i autotesty jądra po zapisie; brak porównania z plikiem | TCL2 | brak |
| `crtos flash --net` (+ D07) | repozytorium | zapis jądra przez sieć | TI2 | TD1 – CRC obrazu i weryfikacja po zapisie w jądrze (D07) | TCL1 | nie wymaga |
| `tools/fontconv.py` | repozytorium | czcionki | TI2 | TD2 – kontrola wzrokowa (`--preview`) | TCL2 | brak |
| `cmake/xiplibs.cmake` z przepisem `toolchain/xiplibs` (`tools/xiplibs_recipe.py`) | repozytorium | biblioteki C i C++ programów XIP (`lib/xip`) ze źródeł newlib i GCC | TI2 | TD2 – przepis z budowania newlib i GCC; porównanie z bibliotekami etapu A obiekt po obiekcie (04.10.2026: 3050 z 3057 identycznych, reszta wyjaśniona); kontrola wersji GCC | TCL2 | brak |
| `tools/thirdparty.py` | repozytorium | pobranie kodu z zewnątrz (commity z `third_party/sources.txt`) i nałożenie łatek | TI2 | TD2 – commit sprawdzany przy każdej konfiguracji, łatka nakładana w całości albo wcale (`git apply`); przy zmianie wersji porównanie plików wynikowych (03.10.2026: identyczne z budowaniem z kopii w repozytorium) | TCL2 | brak |
| `netsurf_prepare.py`, `nsmake.py` | repozytorium | budowanie przeglądarki (opcjonalnej) | TI2 | TD2 | TCL2 | brak |
| `tools/appsym.py`, `rescue.py`, `crtos crash/shot/log` | repozytorium | diagnostyka | TI1 | – | TCL1 | nie wymaga |
| PlantUML + `tools/diagrams.py` | 1.2026.8 (SHA-256 sprawdzane) | diagramy dokumentacji | TI1 | – | TCL1 | nie wymaga |

Dla użycia z ASIL narzędzia z TCL2 i TCL3 wymagałyby kwalifikacji (ISO 26262-8, rozdz.
11: zwiększona pewność z użytkowania, ocena procesu rozwoju narzędzia, walidacja narzędzia
albo rozwój wg normy bezpieczeństwa); dla kompilatora zwykle stosuje się zestaw testów
walidacyjnych albo kwalifikowaną dystrybucję.

## 7. Komponenty zewnętrzne (ISO 26262-8, rozdz. 12)

| Komponent | Wersja | Użycie | Zmiany | Weryfikacja w CRTOS | Stan kwalifikacji |
|---|---|---|---|---|---|
| NXP MCUXpresso SDK (sterowniki `fsl_*`, CMSIS, startup) | 25.6.0 | sterowniki L1, platforma | bez zmian (BSP w `kernel/platform`; sterowniki modułów pobierane: commity mcuxsdk-manifests v25.06.00 w `third_party/sources.txt`) | testy na płytce | brak |
| FatFs | R0.15 | system plików karty (K14) | konfiguracja | `apptest` (pliki na `/sd`), `kmon sdstress` | brak |
| lwIP | 2.2.1 (fork NXP) | stos TCP/IP (D05) | konfiguracja, warstwa `sys_arch`, `patches/lwip.patch` (hak zdarzeń gniazd, typ w komunikacie debugowania) | testy sieci, `crtos netbench`, `crtos deploy` | brak |
| pdpmake | commit 02f03e2 (28.08.2026) | `make` na płytce (A03) | `patches/pdpmake.patch` (`CC`, powłoka, czasy FAT) | `maketest.sh` | brak |
| TinyUSB | 0.21.0 | USB urządzenie i host (D06) | port na CRTOS | testy ręczne USB | brak |
| newlib-nano | Arm GNU 14.3.Rel1 | biblioteka C programów (L01) | bez zmian | `apptest` | brak |
| newlib, libgcc, libstdc++ dla programów XIP (`lib/xip`) | newlib 4.5.0 (`364226a`), GCC 14.3.1 (`b588d02`): commity źródeł Arm 14.3.Rel1 | programy wykonywane w miejscu (T02) | bez zmian w kodzie; opcje PIC-r9 (przepis `toolchain/xiplibs`) | porównanie z bibliotekami etapu A, `apptest_xip`, `xiptest`, `heaptest` | brak |
| Mbed TLS, curl, libcss, libdom, ... (NetSurf) | wg `third_party/sources.txt` (grupa `netsurf`) | przeglądarka (A02, opcjonalna) | `patches/netsurf.patch`, `patches/netsurf-libs/` | ręcznie | brak (poza zakresem funkcji systemu) |
| mostek Bluetooth pada (ESP32, Zephyr) | stan `esp32-s3-bt-mod/` | źródło ramek dla `esp32-pad` (D04) | bez zmian | ramki na `/dev/ttyS3` (`uart -x`), `dmesg` | brak |

Żaden komponent zewnętrzny nie ma specyfikacji wymagań ani dowodów kwalifikacji
wymaganych przez ISO 26262-8, rozdz. 12 – przy użyciu z ASIL trzeba by je wykonać albo zastąpić
kwalifikowanymi odpowiednikami.

## 8. Luki weryfikacji

| ID | Luka | Wpływ | Działanie |
|---|---|---|---|
| W-01 | Brak specyfikacji testów z powiązaniem test ↔ wymaganie w kodzie testów (powiązanie tylko w dokumentach) | trudna ocena kompletności | identyfikatory `REQ-*` w testach, raport z przebiegu |
| W-02 | 113 wymagań tylko z przeglądem kodu; sterowniki i usługi bez testów automatycznych | regresje wykrywane późno | testy automatyczne sterowników (pętle zwrotne SPI/UART, I2C, sieć), testy usług przez IPC |
| W-03 | Brak pomiaru pokrycia (instrukcje, gałęzie, MC/DC) | nieznana część kodu nietestowana | testy jednostkowe na komputerze (np. alokator, parser FDT, loader ELF) z `gcov` |
| W-04 | Brak analizy statycznej zgodności z zasadami kodowania (MISRA C:2012, CERT C) | ukryte błędy klasy UB | narzędzie analizy statycznej, zasady kodowania (05) |
| W-05 | Brak ciągłej integracji: testy uruchamiane ręcznie | regresje między uruchomieniami | stanowisko HIL z płytką: budowanie, `flash`, `test all`, `apptest`, `bench` |
| W-06 | Brak testów wstrzykiwania błędów poza MPU (np. błędy karty, sieci, magistral) | nieznane zachowanie przy awariach sprzętu | wstrzykiwanie błędów w sterownikach (tryb testowy) |
| W-07 | Brak testów czasowych w najgorszym przypadku (WCET, czas blokady przerwań) | brak gwarancji czasów | pomiary DWT w najgorszych ścieżkach, analiza WCET |
| W-08 | Brak niezależności weryfikacji i środków potwierdzających | wymagane dla ASIL (ISO 26262-2) | niezależny przegląd, audyt i ocena |
| W-09 | Autotesty `sched` i `irq` sporadycznie zgłaszają błąd przy pracy w tle (pulpit, sieć): 2 z 3 przebiegów `test all` 29.09.2026, pojedynczo 3/3 | fałszywe alarmy albo przeoczony rzadki błąd | testy mierzące tylko własne wątki i przydziały, powtórzenie przy niejednoznacznym wyniku, przebieg na płytce bez pulpitu |
