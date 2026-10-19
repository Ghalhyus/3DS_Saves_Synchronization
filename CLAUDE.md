# Notes pour Claude

PokeBank Sync : homebrew 3DS (C++17, libctru, libcurl/mbedTLS, jansson) qui envoie les sauvegardes Pokémon au site
PokeBank (`C:\Sources\MonstersUniverse`, son API : section « Synchronisation depuis une 3DS » de son README).
Le fonctionnement et l'organisation du code sont dans le `README.md` : le lire en premier.

## Façon de travailler

Mêmes règles que le CLAUDE.md du site : **ne jamais commiter ni pousser** (signaler les fichiers nouveaux) ; nouvelle
fonctionnalité : questions d'abord (avec une recommandation), puis attendre le « go » ; tout en français, interface qui
tutoie ; README à jour (tableau du code, crédits pour toute ressource reprise d'un autre projet).

## Compiler

`compiler.cmd` (devkitPro dans `C:\devkitPro`, make lancé dans son MSYS2). Depuis l'outil Bash : passer par PowerShell,
`$env:DEVKITPRO="/opt/devkitpro"; $env:DEVKITARM="/opt/devkitpro/devkitARM"; & C:\devkitPro\msys2\usr\bin\bash.exe -lc "cd /c/Sources/3DS_Saves_Synchronization && make"`.
Pas d'émulateur ni de console ici : tout essai réel se fait par l'utilisateur sur sa 3DS. Tester l'API avec curl contre
une instance de test du site (voir son CLAUDE.md).

## Pièges

- La console de libctru affiche un octet par case avec une police **page de code 437** (vérifié dans `default_font.bin`) :
  tout texte passe par `versConsole` (`texte.cpp`), jamais d'UTF-8 brut dans `printf`.
- 3ds-mbedtls tire son aléa de `sslcGenerateRandomData` : `sslcInit(0)` avant toute connexion HTTPS.
- `CURLOPT_CAINFO_BLOB` : curl recopie le PEM avec un zéro final pour mbedTLS ; le fichier de `data/` n'en a pas besoin.
- Pas d'exceptions ni de RTTI (`-fno-exceptions -fno-rtti`, Makefile de devkitPro).
- Ne pas écrire sur la dernière case de l'écran (ligne 30, colonne 50/40) : la console défile.
