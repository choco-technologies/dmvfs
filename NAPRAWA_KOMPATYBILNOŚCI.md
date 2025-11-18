# Naprawa problemu kompatybilności VFS z DMOD

## Opis problemu

Testy DMVFS nie przechodziły, ponieważ VFS nie mógł znaleźć zamontowanych modułów systemu plików. Przyczyna: błąd w makrach preprocesora C w DMOD, który uniemożliwia prawidłowe generowanie sygnatur DIF (DMOD Interface).

## Krótka diagnoza

### Objaw
```
[WARN] File system 'testfs' not found
[ERROR] Cannot mount file system 'testfs': Not found
```

### Przyczyna
Makro `DMOD_MAKE_DIF_SIGNATURE` używa bezpośredniej stringifikacji (`#VERSION`), która NIE rozwija zagnieżdżonych makr przed konwersją na string.

Efekt:
- **Generowana sygnatura** (błędna): `DDIF_fopen@dmfsi:DMOD_MAKE_VERSION(1.0,1.0)`
- **Oczekiwana sygnatura** (poprawna): `DDIF_fopen@dmfsi:1.0/1.0`

## Rozwiązanie

**Plik do naprawy**: `dmod/inc/dmod_defs.h` (około linii 97-106)

**Patch**: Zobacz plik `dmod_signature_fix.patch`

**Repozytorium DMOD**: https://github.com/choco-technologies/dmod (branch: develop)

### Zmiany

1. Dodanie makr pomocniczych:
```c
#define DMOD_STRINGIFY(x) #x
#define DMOD_STRINGIFY_EXPANDED(x) DMOD_STRINGIFY(x)
```

2. Aktualizacja makr sygnatur - zamiana `#VERSION` na `DMOD_STRINGIFY_EXPANDED(VERSION)`:
   - `DMOD_MAKE_SIGNATURE`
   - `DMOD_MAKE_MAL_SIGNATURE`
   - `DMOD_MAKE_DIF_SIGNATURE`

## Weryfikacja naprawy

Po zastosowaniu patcha i przebudowie:

**Przed**:
```bash
$ strings testfs.dmf | grep "_fopen@dmfsi"
_fopen@dmfsi:1.0/1.0
_fopen@dmfsi:DMOD_MAKE_VERSION(1.0,1.0)  # BŁĄD
```

**Po**:
```bash
$ strings testfs.dmf | grep "_fopen@dmfsi"
_fopen@dmfsi:1.0/1.0  # OK
```

**Testy**: Wszystkie testy DMVFS przechodzą pomyślnie.

## Pliki

1. `DMOD_COMPATIBILITY_FIX.md` - Szczegółowy opis techniczny (EN)
2. `dmod_signature_fix.patch` - Patch do zastosowania w repozytorium DMOD
3. `NAPRAWA_KOMPATYBILNOŚCI.md` - Ten plik (PL)

## Jak zastosować patch w DMOD

```bash
cd /ścieżka/do/dmod
git checkout develop
git apply dmod_signature_fix.patch
# lub ręcznie edytuj inc/dmod_defs.h według patcha
git commit -am "Fix DIF signature macro stringification bug"
```

## Wpływ

Ten bug wpływa na:
- Wszystkie moduły używające interfejsów DIF
- Systemy próbujące znaleźć i połączyć się z modułami opartymi na DIF
- DMVFS montujący systemy plików korzystające z interfejsu DMFSI
