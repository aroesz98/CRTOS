# SDK: programy poza repozytorium

Programy możesz pisać na dwa sposoby:

1. **W drzewie źródeł CRTOS**: `crtos new NAZWA` tworzy katalog w `apps/`, `system/commands/` albo
   `system/services/`, a `crtos build` buduje go razem z systemem. Tak jest najprościej, gdy i tak
   pracujesz na źródłach systemu.
2. **Z SDK, we własnym katalogu**: nie potrzebujesz źródeł jądra i sterowników, a program
   może mieć własne repozytorium. SDK zawiera toolchain (`arm-crtos-gcc`, nagłówki, gotowe
   biblioteki), obsługę CMake, szablony i polecenie `crtos`.
3. **Samym kompilatorem**: `arm-crtos-gcc hello.c -o hello.app`, bez CMake
   ([Toolchain](toolchain.md)).

## Tworzenie SDK

W drzewie źródeł CRTOS:

```
.\crtos sdk                  -> build\sdk
.\crtos sdk C:\dev\crtos-sdk -> dodatkowo kopia we wskazanym katalogu
```

SDK pasuje do systemu zbudowanego z tych samych źródeł. Po zmianach w `lib/` albo
w interfejsie jądra (`kernel/include/crtos/syscall.h`) utwórz SDK ponownie.

## Praca z SDK

Dodaj katalog SDK do zmiennej `PATH` albo wywołuj `crtos` z pełną ścieżką.

```
crtos new hello              # ./hello: CMakeLists.txt, hello.c i icon.png (--console, --service)
cd hello
crtos build                  # -> build/sdcard/crtos/apps/hello.app
crtos deploy                 # na kartę SD płytki (sieć albo sonda USB)
crtos run hello
```

W SDK działają polecenia do pracy z płytką: `deploy`, `run`, `put`, `kmon`, `serial`, `log`,
`shot`, `crash`, `reboot`, `find`, `doctor` i `setup`. Poleceń dotyczących jądra (`flash`, `kbuild`,
`sdk`) w SDK nie ma: jądro wgrywa się ze źródeł CRTOS.

Plik `CMakeLists.txt` programu i parametry `crtos_app` opisuje `README.md` w katalogu SDK
(źródło: [sdk/README.md](../sdk/README.md)).

Na Windows trzymaj projekt w krótkiej ścieżce (np. `C:\dev\hello`), bo kompilator nie
obsługuje ścieżek dłuższych niż 260 znaków, a CMake tworzy głęboko zagnieżdżone katalogi.
