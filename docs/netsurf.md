# NetSurf: przeglądarka internetowa

CRTOS zawiera port przeglądarki [NetSurf](https://www.netsurf-browser.org/) z nakładką
graficzną *framebuffer*, która rysuje w oknie CRTOS. Obsługuje HTML i CSS, obrazy (PNG, JPEG,
GIF, BMP, WebP, SVG), HTTP i HTTPS (curl + Mbed TLS), ciasteczka, asynchroniczny DNS
i JavaScript (Duktape, [niżej](#javascript)). Wyszukiwarka Google czasem pokazuje wyniki,
ale po częstych zapytaniach odpowiada stroną reCAPTCHA. Do wyszukiwania służy pasek adresu
(DuckDuckGo, niżej).

## Pobranie źródeł i budowanie

Źródeł przeglądarki nie ma w repozytorium. Pobiera je:

```
.\crtos setup --netsurf
.\crtos build
```

`crtos setup --netsurf`:

1. pobiera NetSurf, 19 bibliotek i bibliotekę polyfilli JavaScript do `third_party/netsurf`
   i `third_party/netsurf-libs`, a libjpeg i Mbed TLS z MCUXpresso SDK do
   `third_party/nxp-sdk/middleware` (razem około 110 MB). Pobiera wersje z grupy `netsurf`
   w `third_party/sources.txt`,
2. nakłada zmiany CRTOS z `patches/` (`netsurf.patch` oraz `netsurf-libs/`: curl, nsgenbind,
   libdom),
3. instaluje narzędzia potrzebne przy budowaniu: `perl` (jest w Git for Windows), `gperf`
   i kompilator C dla komputera (`gcc`, na Windows MinGW z pakietu WinLibs).

Bez tych źródeł `crtos build` pomija przeglądarkę, a reszta systemu buduje się normalnie.
Przeglądarkę wyłączysz też ręcznie: `cmake -S . -B build -DCRTOS_BUILD_NETSURF=OFF`.

Pobrane katalogi są checkoutami gita. Gdy commit w `sources.txt` albo łatka się zmienią,
każde `crtos build` przełączy je na nową wersję ([patches/README.md](../patches/README.md)).

## Uruchamianie

- menu **Apps → NetSurf** na ekranie płytki,
- z komputera: `crtos run netsurf https://example.com`,
- `netsurf -v` wypisuje log przeglądarki.

Płytka musi być w sieci (kabel Ethernet). Adres i DNS ustawia `netmgr`
([Budowanie → Sieć](budowanie.md#sieć)). Certyfikaty HTTPS są w
`/crtos/etc/ssl/cacert.pem`, a ciasteczka w `/crtos/var/netsurf`.

## Pasek adresu, wyszukiwanie i klawisze

Wpisz w pasek adresu słowa i naciśnij Enter: przeglądarka wyszuka je w DuckDuckGo, w wersji
bez JavaScriptu (`html.duckduckgo.com`). Tekst ze spacją albo jedno słowo bez kropki,
dwukropka i ukośnika to wyszukiwanie. Reszta to adres; bez `https://` przeglądarka go
dopisze. Wyniki bez JavaScriptu daje też Brave Search (`search.brave.com`), ale jego strony
są kilka razy większe.

Pierwsze kliknięcie w pasek adresu zaznacza cały adres, więc wpisany tekst go zastępuje.
Kolejne kliknięcie stawia kursor.

| Klawisze | W pasku adresu | W polu tekstowym strony |
|---|---|---|
| Ctrl+A | zaznacza cały tekst | zaznacza cały tekst |
| Ctrl+C, Ctrl+X | kopiuje, wycina zaznaczony tekst | kopiuje, wycina zaznaczenie |
| Ctrl+V | wkleja (zamiast zaznaczenia) | wkleja |
| Ctrl+U | czyści pasek | usuwa wiersz |
| Home, End | początek, koniec tekstu | początek, koniec wiersza (z Ctrl: całego tekstu); poza polem – góra i koniec strony |
| Delete, Backspace | znak za kursorem, przed kursorem (albo zaznaczenie) | tak samo |
| Ctrl+Backspace | słowo przed kursorem | słowo przed kursorem |
| Ctrl + `+`, `-`, `0` | – | powiększenie strony |

Schowek przeglądarki jest schowkiem systemu: tekst skopiowany w terminalu można wkleić
w przeglądarce i odwrotnie (także przez zdalny pulpit).

## JavaScript

Skrypty stron wykonuje Duktape 2.7 (ECMAScript 5.1 z częścią 2015). Włącza je
`enable_javascript:1` w `apps/netsurf/Choices` (`0` wyłącza). Skrypt, który działa dłużej niż
`script_timeout` sekund (domyślnie 10), zostaje przerwany.

Przed pierwszym skryptem strony przeglądarka uruchamia skrypty przygotowujące:

| Plik | Co daje |
|---|---|
| `polyfill.js` NetSurf | `Array.from`, `DOMTokenList.toString` |
| `apps/netsurf/js/prelude.js` | `self`, `top`, `globalThis`, `console.assert`, `performance`, `Image`, `btoa`, `atob` |
| biblioteka [inexorabletash/polyfill](https://github.com/inexorabletash/polyfill) (MIT lub Unlicense) | ECMAScript do 2017: `Symbol.iterator` i iteratory, `Map`, `Set`, `WeakMap`, `Promise`, `Object.assign`, metody tablic, napisów, liczb i `Math`; `URL`, `URLSearchParams`, `fetch`, stałe `Node` |
| `apps/netsurf/js/newer.js` | `flat`, `flatMap`, `at`, `replaceAll`, `trimStart`, `Object.fromEntries`, `Object.hasOwn`, `Promise.allSettled`, `any`, `finally`, `queueMicrotask` |
| `apps/netsurf/js/dom.js` | DOM, które wiązania NetSurf tylko deklarują (atrapy zwracające `undefined`): `querySelector(All)`, `matches`, `closest`, `children`, `append`, `before`, `replaceWith`, `insertAdjacentHTML`, `dataset`, `hidden`, `style` (atrybut `style`), `click()`, części adresu linku, `form.elements`, `selectedIndex`, `localStorage`, `history`, `requestAnimationFrame`, `matchMedia`, `postMessage`, `MessageChannel`, `new Event`, `new CustomEvent`, `CharacterData.data` |

W C (wiązania i `dukky.c`) są: `document.readyState`, `title`, `URL`, `characterSet`,
`compatMode`, `defaultView`, `tagName`, `localName`, `remove()`, pełny adres `a.href`,
`form.submit()`, `innerWidth`, `innerHeight`, `screen`, zdarzenie `DOMContentLoaded`,
słuchacze okna (`window.addEventListener('load', …)`, `window.dispatchEvent`) oraz poprawne
`location.protocol` (`https:`), `search` (`?…`) i `hash`.

Ograniczenia:

- **Składnia ES2015 i nowsza się nie parsuje**: funkcje strzałkowe, `class`, `let`/`const`,
  szablony napisów, `async`. Taki skrypt kończy się `SyntaxError` (np. jQuery 4). Polyfille
  dodają funkcje, nie składnię.
- **Układ strony jest statyczny**: zmiany DOM ze skryptów działających przy ładowaniu (także
  w `DOMContentLoaded` i `load`) są widoczne, późniejsze już nie (poza tytułem i wartościami
  pól formularzy).
- **Skrypty nie mają sieci**: nie ma `XMLHttpRequest`, a `fetch` kończy się błędem sieci.
- `localStorage` trzyma dane do końca strony, `history.pushState` nie zmienia adresu,
  a rozmiary i położenie elementów (`getBoundingClientRect`, `offsetWidth`) to 0.
- **Google**: test przeglądarki liczy się ok. 6 s, po czym wyniki czasem się pokazują.
  Po częstych albo automatycznych zapytaniach (np. seriach testów) Google odpowiada stroną
  reCAPTCHA („nietypowy ruch”), której NetSurf nie przejdzie.

Koszt: przygotowanie środowiska strony (prototypy DOM) trwa 40–50 ms. Skrypty przygotowujące
uruchamiają się dopiero przed pierwszym kodem strony (skrypt, obsługa zdarzenia), więc strona
bez skryptów ich nie potrzebuje. Pierwsza taka strona w sesji czeka ok. 1,1 s (kompilacja
270 KB skryptów, których bajtkod, ok. 270 KB, przeglądarka zachowuje), każda następna
ok. 0,22 s. Środowisko strony zajmuje do ok. 1 MB i znika, gdy okno przechodzi na inną
stronę. Nieużywane strony wypadają z pamięci podręcznej po 3 s.

Bezpieczeństwo: każda strona ma własne środowisko (obiekty wbudowane i prototypy DOM), więc
nie podmieni funkcji, które wywoła następna strona w tym oknie. Błąd w jednym ze skryptów
przygotowujących trafia do logu (`netsurf -v`), a strona dalej wykonuje swoje skrypty.

## Pamięć i ograniczenia

| Ustawienie | Wartość | Gdzie |
|---|---|---|
| sterta przeglądarki | 8,25 MB (arena 12 MB) | `HEAP` w `apps/netsurf/CMakeLists.txt` |
| dodatkowa sterta, gdy tamta się zapełni | jedno okno do 8 MB | `crtos_heap_windows`, `crtos_heap_window_kb` w `apps/netsurf/main.c` |
| pamięć na jeden obraz | 4 MB | `NETSURF_IMAGE_MAX_BYTES` w `apps/netsurf/CMakeLists.txt` |
| szerokość renderowania miniatur | 232 px | `FB_THUMBNAIL_RENDER_WIDTH` w tym samym pliku |
| miniatury stron w historii | 8 ostatnich | `browser_window_history_add` (`netsurf.patch`) |
| pamięć podręczna stron i obrazów | 3 MB | `memory_cache_size` w `apps/netsurf/Choices` |

- Obrazy większe niż limit są zmniejszane już przy dekodowaniu (PNG, JPEG, WebP). Na
  ekranie 480×272 czy 800×480 zwykle nie widać różnicy.
- Progresywne pliki JPEG powyżej około 2 megapikseli się nie wyświetlą, bo dekoder
  potrzebuje dla nich więcej pamięci, niż mieści limit.
- Duże, ciężkie strony ładują się wolno: procesor ma 600 MHz.
- Przydział pamięci (`malloc`) robi `libcrtosheap` (TLSF): przy wielu drobnych obiektach
  skryptów lista wolnych bloków newlib zajmowała większość czasu (skrypt testu Google liczył
  się 54 s zamiast 6 s).
- **Komunikat „NetSurf is running out of memory”**: strony z dużymi skryptami (np. wyniki
  Google) potrzebują bloków po pół megabajta. Gdy sterta w arenie się zapełni, przeglądarka
  bierze z wolnej pamięci systemu jedno okno do 8 MB i oddaje je dopiero przy wyjściu.
  Komunikat pojawi się, gdy i to nie wystarczy, np. gdy obok działa inny duży program
  (emulator, kompilator), a okno dostanie wtedy mniej. Strona może być wtedy niepełna, ale
  przeglądarka działa dalej. Pomaga zamknięcie innych programów albo samej przeglądarki.
- Zajętość sterty pokazuje log: `netsurf -v` wypisuje ją przy każdej nowej stronie (`heap with
  a new page: … KB used of … KB`). `export CRTOS_HEAP_STATS=1` przed uruchomieniem dodaje raport
  przy nieudanym przydziale (rozmiar żądania, największy wolny blok).

## Zmiany w źródłach NetSurf

Zmiany dla CRTOS w samym NetSurf, w curl, nsgenbind (generator wiązań JavaScript) i libdom
są przechowywane jako łatki. Po zmianie plików w `third_party/netsurf*` zapisz je ponownie:

```
python tools/thirdparty.py --save netsurf netsurf-libs/curl   # third_party -> patches/*.patch
python tools/thirdparty.py --status                          # stan każdego katalogu
```

Kod CRTOS przeglądarki (okno, konfiguracja bibliotek) jest w `apps/netsurf/`: `main.c`,
`nsfb_crtos.c` (powierzchnia libnsfb na oknie CRTOS i schowek systemu), `tls_crtos.c`, `iconv.c` i pliki
`*_config.h`, a skrypty przygotowujące strony w `apps/netsurf/js/`. `tools/netsurf_prepare.py`
robi to, co robiłyby pliki Makefile NetSurf: generuje tabele, obrazki i czcionkę, buduje
nsgenbind i nim wiązania DOM z plików `.bnd` i Web IDL, składa skrypty przygotowujące
w `polyfill.js.inc` oraz kopiuje zasoby do `/crtos/share/netsurf`.
