# Pierwszy program

Ten rozdział prowadzi od pustego katalogu do własnego programu z oknem, działającego na
płytce. Zakładamy, że system jest już zbudowany i wgrany ([Szybki start](../README.md)).

## 1. Utwórz program

```
.\crtos new licznik
```

```
new: apps\licznik
       CMakeLists.txt
       icon.png
       licznik.c
     programs menu: "Licznik" added to rootfs\etc\launcher.cfg
next:  crtos build   crtos deploy   crtos run licznik
```

Powstały trzy pliki:

- `apps/licznik/CMakeLists.txt` mówi, jak zbudować program:

  ```cmake
  crtos_app(licznik SOURCES licznik.c STACK 16384 HEAP 65536 LIBS gfx ICON icon.png)
  ```

  `STACK` i `HEAP` to rozmiar stosu głównego wątku i sterty (w bajtach). `LIBS gfx`
  dołącza bibliotekę okien i rysowania. `ICON` to ikona programu.

- `apps/licznik/licznik.c` zawiera okno z przyciskiem, który liczy kliknięcia.

- `apps/licznik/icon.png` to ikona programu w menu **Apps**, na pasku zadań i w pasku
  tytułu okna: kwadratowy obraz PNG 64 × 64 z przezroczystością. Na początek to ikona
  zwykłego okna; zamień ją na własną w dowolnym programie graficznym. Budowanie zapisuje ją
  na kartę jako `/crtos/share/icons/licznik.pam`.

Program trafił też do menu **Apps** na ekranie (plik `rootfs/etc/launcher.cfg`). Nazwę
w menu możesz podać od razu: `crtos new licznik --title "Mój licznik"`.

## 2. Zbuduj, wgraj, uruchom

```
.\crtos build
.\crtos deploy
.\crtos run licznik
```

Na ekranie płytki pojawi się okno. Stuknij przycisk *Press me*, a licznik wzrośnie.
Zamknij okno krzyżykiem albo naciśnij Ctrl-C w terminalu komputera.

Menu **Apps** czyta `launcher.cfg` przy starcie menedżera okien. Nowy wpis zobaczysz po
`crtos reboot`. Uruchomienie przez `crtos run` działa od razu.

## 3. Jak działa program

Najważniejsze fragmenty `licznik.c`:

```c
struct gfx *g = gfx_open();                     /* połączenie z serwerem grafiki (gfxd) */
struct gfx_win *w = gfx_win_create(g, -1, -1, W, H, GFX_WIN_RESIZABLE, "Licznik");
```

`gfx_win_create` tworzy okno o rozmiarze W×H (na start `ui_px(240)` × `ui_px(150)`).
Wartości `-1, -1` oznaczają, że miejsce wybierze menedżer okien. Okno ma własny bufor
pikseli `w->s`, w którym program rysuje:

```c
int x = ui_px(12), y = ui_px(10) + gfx_font_ascent(ui_theme->bold);
gfx_fill(&w->s, 0, 0, W, H, ui_theme->bg);                       /* tło */
gfx_text(&w->s, ui_theme->bold, x, y, "Licznik", ui_theme->text);    /* y = linia bazowa */
ui_button(&w->s, &b, "Press me", pressed);                       /* przycisk z gfx_ui.h */
gfx_present(w);                                                  /* pokaż zmiany */
```

`ui_px()` przelicza wymiar dobrany dla skali 100% na skalę interfejsu ustawioną w Settings
(np. 150%), a `ui_theme` daje czcionki i kolory z tych ustawień. Dzięki temu okno wygląda
jak reszta systemu w każdej skali.

Potem program w pętli czeka na zdarzenia:

```c
for (;;) {
    struct gfx_event ev;
    if (gfx_next_event(g, &ev, CRTOS_FOREVER))
        continue;
    if (ev.h.type == GFX_EV_CLOSE)          /* krzyżyk na ramce okna */
        break;
    if (ev.h.type == GFX_EV_POINTER) {      /* dotyk: ev.kind = GFX_PTR_DOWN/MOVE/UP */
        ...                                 /* ev.x, ev.y: współrzędne w oknie */
    }
}
gfx_close(g);
```

Klawisze (z klawiatury albo z klawiatury ekranowej systemu) przychodzą jako `GFX_EV_KEY`.
Na znaki zamienia je `ui_key_char()` ([API](api.md#grafika-gfxh)).

Zdarzenie `GFX_EV_CONFIGURE` oznacza, że użytkownik zmienia rozmiar okna. Program
odpowiada `gfx_win_resize()` i rysuje okno od nowa. `GFX_EV_SETTINGS` oznacza zmianę wyglądu
(skala, czcionka, kolory): szablon zmienia wtedy rozmiar okna na `ui_px(240)` × `ui_px(150)`
i rysuje je od nowa.

## 4. Zmień coś

Otwórz `apps/licznik/licznik.c` i zmień np. kolor tła w funkcji `draw()`:

```c
gfx_fill(s, 0, 0, W, H, GFX_RGB(0, 90, 60));
```

Potem:

```
.\crtos build licznik
.\crtos deploy
.\crtos run licznik
```

`crtos build licznik` buduje tylko ten program. `crtos deploy` wyśle tylko zmieniony plik.

## Program konsolowy

```
.\crtos new --console narzedzie
```

Powstaje `apps/narzedzie/narzedzie.c` (każdy nowy program trafia do `apps/`; `DEST bin`
w jego `CMakeLists.txt` instaluje go w `/crtos/bin`). To zwykły program w C (`printf`, pliki,
`malloc`). Uruchamiasz go z komputera (`crtos run narzedzie a b`) albo
w powłoce na płytce (`narzedzie a b`). Wartość zwrócona z `main` to kod wyjścia, który
zwraca też `crtos run`.

## Usługa

```
.\crtos new --service strazak
```

Powstaje `apps/strazak/strazak.c`: program działający w tle, instalowany w `/crtos/sbin`
(`DEST sbin`). Żeby startował razem z systemem, dopisz go do `rootfs/etc/init.cfg`:

```
service strazak respawn /sd/crtos/sbin/strazak.app
```

To, co usługa wypisuje przez `printf`, trafia na konsolę szeregową i do logu jądra
(`crtos kmon dmesg`).

## Kilka plików, C++, biblioteki

```cmake
crtos_app(gra SOURCES main.cpp plansza.cpp grafika.c STACK 16384 HEAP 262144 LIBS gfx)
```

- C++ działa (standard C++17), ale bez wyjątków i RTTI (`-fno-exceptions -fno-rtti`).
  `new` i `delete` działają.
- Dostępne biblioteki: `gfx` (okna, rysowanie, przyciski, listy, klawiatura ekranowa),
  `tftlib` (rysowanie w stylu Adafruit GFX / TFT_eSPI, zobacz `apps/tftdemo`).
- Biblioteka C to newlib-nano. `printf` obsługuje liczby zmiennoprzecinkowe, ale nie
  obsługuje `%lld`/`%llu`: rzutuj na `unsigned long`.

## Pamięć i błędy

Każdy program działa we własnym obszarze pamięci (arenie) chronionym przez MPU. Zawiera on
kod, dane, stos (`STACK`) i stertę (`HEAP`). Program nie może zepsuć jądra ani innych
programów. Gdy odwoła się poza swoją pamięć albo przepełni stos, jądro kończy tylko ten
program i wypisuje raport:

```
*** MemManage: data access violation at 00000000
    task 23 'licznik' (process 21 'licznik')
    pc=80123456 lr=80123401 sp=8012ff00 ...
    -> process terminated
```

- Gdy zabraknie sterty, `malloc` zwraca `NULL`. Zwiększ wtedy `HEAP` w `CMakeLists.txt`.
- `crtos kmon procs` pokazuje procesy, ich pamięć i uprawnienia.
- Co oznacza adres `pc` z raportu, ustalisz według rozdziału [Debugowanie](debugowanie.md).

## Co dalej

- [API dla programów](api.md): pliki, wątki, czas, IPC, sieć, okna i rysowanie.
- Przykłady w repozytorium:

| Przykład | Co pokazuje |
|---|---|
| `apps/paint` | proste okno, dotyk, rysowanie zmienionych prostokątów (`gfx_damage`) |
| `apps/gfxdemo` | animacja zsynchronizowana z ekranem (`gfx_wait_frame`) |
| `system/apps/files` | przewijana lista (`ui_list`), pliki i katalogi |
| `system/apps/term` | klawiatura ekranowa, uruchamianie procesów i potoki |
| `system/commands/ping`, `system/commands/nc` | sieć: gniazda, DNS |
| `tests/apptest` | testy prawie całego API, dużo krótkich przykładów |

- [SDK](sdk.md): własne programy poza tym repozytorium.
