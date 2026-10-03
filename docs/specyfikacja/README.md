# Dokumentacja projektowa CRTOS

Ten katalog opisuje, **jak zbudowany jest system operacyjny CRTOS**: jego architekturę,
każdy komponent (interfejs, budowa, zachowanie), analizę bezpieczeństwa i weryfikację.
Dokumentacja dla użytkownika (instalacja, budowanie, API programów) jest w katalogu
[docs/](../) wyżej.

## Status względem ISO 26262

Układ i zawartość dokumentów odpowiadają produktom pracy normy **ISO 26262-6:2018**
(„Road vehicles — Functional safety — Part 6: Product development at the software level”):

| Dokument | Produkt pracy ISO 26262 |
|---|---|
| [01 Struktura projektu](01-struktura-projektu.md) | elementy konfiguracji, komponenty zewnętrzne (ISO 26262-8, rozdz. 7 i 12) |
| [02 Architektura oprogramowania](02-architektura.md) | specyfikacja architektury oprogramowania (ISO 26262-6, 7.5.1) |
| [Komponenty](komponenty/README.md) | specyfikacja projektu jednostek oprogramowania (ISO 26262-6, 8.5.1) |
| [03 Analiza bezpieczeństwa](03-analiza-bezpieczenstwa.md) | analiza bezpieczeństwa i analiza awarii zależnych (ISO 26262-6, 7.5.2 i 7.5.3; ISO 26262-9) |
| [04 Weryfikacja](04-weryfikacja.md) | specyfikacja i raport weryfikacji (ISO 26262-6, 9.5 i 10.5), pewność narzędzi (ISO 26262-8, 11) |

**CRTOS nie był rozwijany w procesie zgodnym z ISO 26262.** Nie ma celów bezpieczeństwa,
przydzielonego poziomu ASIL, wymagań bezpieczeństwa z poziomu systemu, kwalifikacji
narzędzi ani niezależnej oceny. Dokumenty opisują system tak, jak go zbudowano (wymagania
w dokumentach komponentów są odtworzone z kodu). Oprogramowanie należy więc traktować jako
**QM** (bez wymagań bezpieczeństwa funkcjonalnego).

Wersja opisywanego oprogramowania: CRTOS 0.8 (`project(crtos VERSION 0.8)` w
`CMakeLists.txt`), stan źródeł z 27.09.2026.

## Konwencje

**Diagramy** są w UML, zapisane w [PlantUML](https://plantuml.com) (notacja semi-formalna,
zalecana przez ISO 26262-6, tabela 2). Każdy diagram to plik źródłowy `.puml`
w [diagramy/](diagramy/) (katalog na komponent, wspólny styl w `diagramy/styl.iuml`), a obok
leży wygenerowany z niego obraz `.svg`, który pokazują dokumenty. Pod każdym obrazem jest
odnośnik do źródła.

| Diagram UML | Co pokazuje | Jak czytać w kodzie C/C++ |
|---|---|---|
| diagram klas | struktura statyczna: typy danych, moduły i ich powiązania | klasa `«struct»` to struktura C, `«moduł»` to plik źródłowy z funkcjami i zmiennymi `static` (`-` prywatne, `+` publiczne), interfejs `«interfejs»` to zestaw funkcji albo tablica wskaźników do funkcji (`*_ops`), `enum` to wyliczenie |
| diagram sekwencji | zachowanie dynamiczne: kto kogo wywołuje, w jakiej kolejności, w jakim kontekście | uczestnik to wątek, przerwanie (ISR), wyjątek procesora albo moduł; `alt`/`loop`/`opt` to rozgałęzienia i pętle w kodzie |
| diagram stanów | cykl życia obiektu | stany odpowiadają wartościom pól `state` |
| diagram aktywności | algorytm z decyzjami | kroki i warunki jednej funkcji |
| diagram komponentów i wdrożenia | warstwy, zależności, rozmieszczenie w pamięci i na nośnikach | strzałka przerywana to zależność (wywołanie, użycie interfejsu) |

**Generowanie obrazów** (po zmianie pliku `.puml`):

```
python tools/diagrams.py            tylko zmienione diagramy
python tools/diagrams.py --all      wszystkie
python tools/diagrams.py --check    tylko kontrola składni
```

Obrazy powstają lokalnie, bez wysyłania treści na serwer PlantUML. Skrypt potrzebuje Javy
11 lub nowszej: z `PATH`, z `JAVA_HOME` albo tej dołączonej do MCUXpresso IDE. PlantUML
(1.2026.8, wersja na licencji MIT) pobiera przy pierwszym użyciu do `~/.crtos/tools` i
sprawdza jego sumę SHA-256. Diagramy używają wbudowanego układu `smetana`, więc Graphviz
nie jest potrzebny. Podgląd na żywo daje np. rozszerzenie *PlantUML* (jebbs) dla VS Code.

**Identyfikatory.** Każdy komponent ma identyfikator: `K` jądro, `S` frameworki podsystemów
jądra, `D` sterowniki (moduły `.ko`), `U` usługi, `A` aplikacje i programy, `L` biblioteki,
`T` narzędzia. Wymaganie ma identyfikator `REQ-<komponent>-<numer>`, np. `REQ-K03-02`.
Tabela śledzenia (wymaganie → test) jest w [04 Weryfikacja](04-weryfikacja.md).

**Kontekst wywołania** funkcji interfejsu:

| Oznaczenie | Znaczenie |
|---|---|
| wątek | wolno wołać tylko z wątku (jądra albo wywołania systemowego); funkcja może blokować |
| ISR | wolno wołać także z obsługi przerwania; funkcja nigdy nie blokuje |
| sekcja krytyczna | wołający musi trzymać `irq_lock()` |

**Kody błędów** to ujemne wartości `errno` (`-EINVAL`, `-ENOMEM`, ...) z
`kernel/include/crtos/errno.h`, takie same jak w newlib.

## Szablon dokumentu komponentu

Każdy dokument w [komponenty/](komponenty/README.md) ma te same rozdziały:

1. **Identyfikacja**: identyfikator, warstwa, pliki źródłowe, nagłówek interfejsu.
2. **Odpowiedzialność**: co komponent robi, a czego nie.
3. **Wymagania**: odtworzone z kodu, z identyfikatorami i sposobem weryfikacji.
4. **Interfejs udostępniany**: funkcje, warunki wstępne i końcowe, błędy, kontekst wywołania.
5. **Interfejsy wymagane**: z czego komponent korzysta.
6. **Struktura statyczna**: diagram klas.
7. **Zachowanie dynamiczne**: diagramy sekwencji, stanów i aktywności.
8. **Implementacja**: struktury danych, algorytmy, współbieżność, zasoby, czasy.
9. **Obsługa błędów i mechanizmy bezpieczeństwa**.
10. **Konfiguracja**.
11. **Weryfikacja**: testy komponentu.
12. **Ograniczenia i znane problemy**.

Rozdziały bez treści dla danego komponentu są pominięte.

## Słownik

| Termin | Znaczenie |
|---|---|
| arena | ciągły blok pamięci procesu (kod, dane, sterta, stosy wątków), otwarty dla kodu nieuprzywilejowanego jednym regionem MPU |
| `.app` | program: relokowalny plik ELF (`ld -r`) z biblioteką C, bez nierozwiązanych symboli |
| BASEPRI | rejestr Cortex-M7 maskujący przerwania o priorytecie liczbowo ≥ progu; `irq_lock()` ustawia go na `KERNEL_BASEPRI` |
| `caps` | uprawnienia procesu (`CAP_SPAWN`, `CAP_KILL`, `CAP_MODULE`, `CAP_SYS`, `CAP_DEV`) |
| DTB, DTS | drzewo urządzeń: skompilowane (`board.dtb`) i źródłowe (`dts/*.dts`) |
| `.ko` | moduł jądra (sterownik): relokowalny ELF uruchamiany w trybie uprzywilejowanym |
| MPU | jednostka ochrony pamięci Cortex-M7, 16 regionów |
| PendSV | wyjątek o najniższym priorytecie, w którym jądro przełącza kontekst |
| proces | izolowany program: arena, wątki, tablica uchwytów, uprawnienia |
| SVC | instrukcja wywołania systemowego (`svc #0`) |
| uchwyt | indeks w tablicy procesu wskazujący obiekt jądra (plik, port IPC, pamięć współdzielona); deskryptory plików to uchwyty |
| XIP | wykonywanie kodu wprost z pamięci flash (jądro działa z HyperFlash) |
| wątek jądra | wątek uprzywilejowany bez procesu (`kthread_create`) |
| wątek programu | wątek procesu: kod programu w trybie nieuprzywilejowanym, wywołania systemowe na własnym stosie jądra |
