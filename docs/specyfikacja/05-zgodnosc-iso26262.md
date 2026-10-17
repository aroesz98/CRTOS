# 05 Zgodność z ISO 26262

Ocena luk (gap analysis): czego wymaga ISO 26262:2018 od oprogramowania, co z tego
CRTOS ma dziś i czego brakuje. Ocena dotyczy stanu z 27.09.2026 i części oprogramowania
(ISO 26262-6) oraz procesów wspierających (ISO 26262-8, -9, -2, -10).

**Wniosek:** CRTOS jest oprogramowaniem **QM**. Ma architekturę i mechanizmy, które dobrze
nadają się na podstawę do dalszego rozwoju (warstwy, izolacja MPU programów,
uprawnienia, IPC przez jądro), ale nie ma wymagań bezpieczeństwa, procesu, specyfikacji
testów ani kwalifikacji narzędzi i komponentów, których norma wymaga dla jakiegokolwiek
poziomu ASIL.

Stan: **S** – spełnione, **C** – częściowo, **B** – brak.

## 1. ISO 26262-6: rozwój oprogramowania

| Rozdz. | Wymaganie (skrót) | Stan | Co jest | Czego brakuje |
|---|---|---|---|---|
| 5 | Planowanie: model cyklu życia, metody, narzędzia, zasady modelowania i kodowania | C | model warstwowy, etapy M0–M9, narzędzia opisane (T01, [Budowanie](../budowanie.md)) | plan rozwoju oprogramowania, zasady kodowania (np. MISRA C:2012 / C++), wybór i uzasadnienie języka (C/C++ podzbiór) |
| 6 | Specyfikacja wymagań bezpieczeństwa oprogramowania | B | wymagania funkcjonalne komponentów odtworzone z kodu (441 × `REQ-*`) | wymagania bezpieczeństwa wyprowadzone z technicznej koncepcji bezpieczeństwa, ASIL, weryfikacja wymagań, specyfikacja HSI |
| 7 | Architektura oprogramowania | C | [02 Architektura](02-architektura.md): warstwy, komponenty, interfejsy, zachowanie, zasady projektowe (tabela 3), zasoby, czasy; notacja semi-formalna (UML/PlantUML) | przypisanie wymagań bezpieczeństwa do komponentów, oszacowanie WCET i zużycia zasobów w najgorszym przypadku, weryfikacja architektury przez przegląd niezależny |
| 7 | Analiza bezpieczeństwa i awarii zależnych na poziomie architektury | C | [03 Analiza bezpieczeństwa](03-analiza-bezpieczenstwa.md): FFI wg załącznika D, FMEA oprogramowania, DFA | analiza względem rzeczywistych wymagań bezpieczeństwa, ocena skuteczności mechanizmów, działania dla ISS-01 … ISS-31 |
| 8 | Projekt i implementacja jednostek | C | [Komponenty](komponenty/README.md): interfejs, struktura, zachowanie, obsługa błędów każdego komponentu | zasady projektowe jednostek (tabela 6) nie są egzekwowane – zob. rozdz. 3 niżej |
| 9 | Weryfikacja jednostek | C | autotesty jądra (13), przegląd kodu, `-Wall` | testy jednostkowe z pokryciem (instrukcje, gałęzie, MC/DC wg ASIL), analiza statyczna, specyfikacja testów |
| 10 | Integracja i jej weryfikacja | C | `modcheck`, `apptest` (1720 sprawdzeń), testy interfejsów przez ABI | specyfikacja testów integracyjnych, pokrycie funkcji i wywołań, testy wstrzykiwania błędów |
| 11 | Testy oprogramowania wbudowanego | C | testy na docelowych płytkach (NXP MIMXRT1050-EVKB, ST STM32H7S78-DK) | środowisko HIL, testy wymagań bezpieczeństwa, testy czasowe, obciążeniowe i przy błędach sprzętu |
| zał. C | Konfiguracja oprogramowania (dane kalibracyjne, konfiguracja) | C | drzewo urządzeń, `init.cfg`, `launcher.cfg` opisane (01, K17, U01) | weryfikacja danych konfiguracyjnych, ochrona integralności plików na karcie |
| zał. D | Brak zakłóceń między elementami | C | izolacja przestrzenna programów (MPU); ocena w 03 | izolacja czasowa (ISS-02 … ISS-04), zasobów (ISS-17), sterowników (ISS-05) |

## 2. Zasady architektury (ISO 26262-6, tabela 3)

Realizacja: [02 Architektura, rozdz. 10](02-architektura.md#10-zasady-projektowe-iso-26262-6-tabela-3).
Luki:

| Zasada | Stan | Luka |
|---|---|---|
| hierarchiczna struktura, spójność, luźne powiązania | S | – |
| ograniczony rozmiar komponentów | C | `gfxd` (ok. 1300 linii), sterownik SD (ok. 1150), `wm` (ok. 1050) – kandydaci do podziału |
| właściwe szeregowanie | C | brak budżetów czasu i watchdoga (ISS-03, ISS-04); priorytety programów ponad sterownikami (ISS-02) |
| ograniczone użycie przerwań | S | – |
| izolacja przestrzenna | C | tylko L2/L3; sterowniki L1 bez izolacji (ISS-05) |
| zarządzanie zasobami współdzielonymi | C | brak limitów pamięci na proces (ISS-17), brak odwołań na moduły dostawców (ISS-01) |

## 3. Zasady projektu i implementacji jednostek (ISO 26262-6, tabela 6)

Stan kodu jądra i sterowników (ok. 23 300 linii bez lwIP, SDK i FatFs):

| Zasada | Stan | Uwagi |
|---|---|---|
| jedno wejście i jedno wyjście z funkcji | C | wiele funkcji z wczesnym `return` przy błędzie (styl Linuksa) |
| brak obiektów dynamicznych w czasie pracy | B | alokacja dynamiczna jest podstawą projektu (TLSF, 68 wywołań `kmalloc`/`kzalloc`); deterministyczny czas O(1), ale możliwe wyczerpanie i fragmentacja |
| inicjalizacja zmiennych | C | brak kontroli narzędziem |
| brak wielokrotnego użycia nazw | C | brak kontroli narzędziem |
| unikanie zmiennych globalnych | C | stan komponentów w zmiennych `static` plików (np. `s_*`) |
| ograniczone użycie wskaźników | C | wskaźniki i rzutowania powszechne (C/C++ systemowy); dostęp do pamięci programów tylko przez `uaccess_ok` |
| brak niejawnych konwersji typów | C | `-Wall` bez `-Wconversion` |
| brak ukrytego przepływu danych i sterowania | C | wywołania przez tablice funkcji (`*_ops`) – udokumentowane w komponentach |
| brak skoków bezwarunkowych | C | 24 × `goto` (ścieżki sprzątania po błędzie) |
| brak rekurencji | C | rekurencja: `populate_bus`, `of_find_node_by_path` (alias), `print_node` (kmon) – głębokość zależy od drzewa urządzeń |

## 4. Zasady modelowania i kodowania (ISO 26262-6, tabela 1)

| Zasada | Stan | Uwagi |
|---|---|---|
| ograniczenie złożoności | C | brak metryk (np. złożoność cyklomatyczna) |
| podzbiór języka | B | brak zasad (MISRA C:2012 / MISRA C++:2023 / AUTOSAR C++14) i narzędzia |
| silne typowanie | C | C/C++ z typami o stałej szerokości (`uint32_t`), bez `-Wconversion` |
| programowanie defensywne | C | kontrola argumentów na granicach (wywołania systemowe, IPC, ioctl); wewnątrz jądra zaufanie do wołającego |
| uznane zasady projektowe | S | wzorce Linuksa (model sterowników, VFS, DT), mikrojądro-podobne usługi |
| jednoznaczna reprezentacja graficzna | S | UML w PlantUML (ta dokumentacja) |
| zasady stylu i nazewnictwa | C | styl spójny w obrębie części (jądro i sterowniki: K&R; aplikacje: Allman), brak spisanych zasad i automatycznego formatowania |
| współbieżność | C | opisana w komponentach (konteksty wywołań, blokady); brak analizy wyścigów narzędziem |

## 5. ISO 26262-8: procesy wspierające

| Rozdz. | Wymaganie | Stan | Co jest | Czego brakuje |
|---|---|---|---|---|
| 6 | Specyfikacja i zarządzanie wymaganiami | C | identyfikatory `REQ-*`, macierz w [04](04-weryfikacja.md#4-śledzenie-wymagań) | narzędzie do zarządzania wymaganiami, śledzenie w górę (do wymagań systemu) i do testów w kodzie |
| 7 | Zarządzanie konfiguracją | C | git, wersja w `CMakeLists.txt`, elementy konfiguracji w [01](01-struktura-projektu.md#6-elementy-konfiguracji) | linie bazowe (tagi), zasada, że weryfikowany i wydawany jest tylko stan zatwierdzony w commicie po przeglądzie; identyfikacja wersji na płytce powiązana z commitem |
| 8 | Zarządzanie zmianami | B | historia git | wnioski o zmianę, analiza wpływu, zatwierdzanie |
| 9 | Weryfikacja | C | [04 Weryfikacja](04-weryfikacja.md) | plan weryfikacji, specyfikacje testów, kryteria zakończenia |
| 10 | Dokumentacja | C | dokumentacja użytkownika i projektowa w repozytorium (Markdown + PlantUML) | zarządzanie wersjami dokumentów, przeglądy i zatwierdzenia |
| 11 | Pewność narzędzi | C | klasyfikacja TI/TD/TCL w [04, rozdz. 6](04-weryfikacja.md#6-pewność-narzędzi-iso-26262-8-rozdz-11) | kwalifikacja narzędzi z TCL2/TCL3 (kompilator, CMake, pyOCD, `stackcheck.py`, `dtc.py`) |
| 12 | Kwalifikacja komponentów oprogramowania | B | lista komponentów zewnętrznych i wersji ([01](01-struktura-projektu.md#7-komponenty-zewnętrzne), [04](04-weryfikacja.md#7-komponenty-zewnętrzne-iso-26262-8-rozdz-12)) | specyfikacje wymagań i dowody kwalifikacji (SDK NXP, FatFs, lwIP, TinyUSB, newlib) |

## 6. ISO 26262-9, -2 i -10

| Część | Wymaganie | Stan | Uwagi |
|---|---|---|---|
| 9, rozdz. 5 | dekompozycja ASIL | – | nie dotyczy (brak ASIL) |
| 9, rozdz. 6 | kryteria współistnienia elementów o różnych ASIL | C | izolacja MPU pozwala na współistnienie programów; jądro i sterowniki musiałyby mieć najwyższy ASIL (wspólna przestrzeń) |
| 9, rozdz. 7 | analiza awarii zależnych | C | [03, rozdz. 6](03-analiza-bezpieczenstwa.md#6-analiza-awarii-zależnych) |
| 9, rozdz. 8 | analizy bezpieczeństwa | C | [03, rozdz. 5](03-analiza-bezpieczenstwa.md#5-analiza-trybów-awarii-komponentów) |
| 2 | zarządzanie bezpieczeństwem, kultura, plan bezpieczeństwa, środki potwierdzające (przeglądy, audyt, ocena) | B | brak planu bezpieczeństwa, ról i niezależności |
| 10, rozdz. 9 | element rozwijany poza kontekstem (SEooC) | C | założenia w [03, rozdz. 2](03-analiza-bezpieczenstwa.md#2-założone-wymagania-bezpieczeństwa) (ASR-01 … ASR-09); brak zatwierdzonego podręcznika bezpieczeństwa (safety manual) z założeniami użycia |

## 7. Plan dojścia do ASIL (propozycja)

Kolejność od działań o największym wpływie:

1. **Kontekst bezpieczeństwa**: zdefiniować użycie (SEooC) i założone wymagania
   bezpieczeństwa z ASIL; spisać podręcznik bezpieczeństwa (założenia dla integratora).
2. **Luki architektury** z [03](03-analiza-bezpieczenstwa.md#7-znalezione-problemy):
   ISS-02 (priorytety), ISS-03 (watchdog), ISS-01 (odwołania dostawców), ISS-17 (limity
   pamięci), ISS-06/ISS-07 (kontrola dostępu do usług), ISS-10 (restart `init`);
   decyzja co do sterowników (ISS-05): kwalifikacja z jądrem albo izolacja.
3. **Zasady kodowania**: wybór podzbioru (MISRA C:2012 dla C, MISRA C++:2023 lub AUTOSAR
   C++14 dla C++), analiza statyczna w budowaniu, lista odstępstw (alokacja dynamiczna,
   rekurencja, `goto`).
4. **Weryfikacja**: specyfikacja testów z identyfikatorami wymagań, testy jednostkowe na
   komputerze z pomiarem pokrycia, stanowisko HIL z płytką w ciągłej integracji, testy
   czasowe (WCET, blokady przerwań) i wstrzykiwanie błędów.
5. **Narzędzia i komponenty**: kwalifikacja kompilatora (albo kwalifikowana dystrybucja),
   ograniczenie zależności zewnętrznych w części bezpieczeństwa (np. bez stosu USB
   i sieci), kwalifikacja pozostałych.
6. **Proces**: zarządzanie konfiguracją (linie bazowe, tagi, wersja na płytce), zmianami,
   przeglądy niezależne, audyt i ocena bezpieczeństwa funkcjonalnego.
