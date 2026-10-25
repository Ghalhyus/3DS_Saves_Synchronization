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
Pas d'émulateur ni de console ici : tout essai réel se fait par l'utilisateur sur sa 3DS. Une copie de sa carte SD est
dans `D:\Backups 3DS` (ROM DS à la racine, sauvegardes TWiLight dans `saves/`, jeux injectés en CIA installés : leurs
fichiers sous `Nintendo 3DS/` sont chiffrés, illisibles sur le PC) : s'en servir pour vérifier la recherche sur la SD. Tester l'API avec curl contre
une instance de test du site (voir son CLAUDE.md).

## Pièges

- La console de libctru affiche un octet par case avec une police **page de code 437** (vérifié dans `default_font.bin`) :
  tout texte passe par `versConsole` (`texte.cpp`), jamais d'UTF-8 brut dans `printf`.
- 3ds-mbedtls tire son aléa de `sslcGenerateRandomData` : `sslcInit(0)` avant toute connexion HTTPS.
- `CURLOPT_CAINFO_BLOB` : curl recopie le PEM avec un zéro final pour mbedTLS ; le fichier de `data/` n'en a pas besoin.
- Jeux GBA injectés : leur sauvegarde (archive « sauvegarde et contenu », chemin binaire 1, 1, 3) ne s'ouvre que par
  `PxiFS0` (FSPXI, comme PKSM) ; `FSUSER` répond 0xE0E046BE. Ce service n'accepte qu'une connexion, déjà prise par FS (0xD0401834 à la demande) :
  elle est empruntée par l'appel système de Luma3DS `svcControlService` (svc 0xB0, `source/luma.s`), seulement après
  avoir vérifié la présence de Luma (`svcGetSystemInfo(0x10000)`) : sans Luma, svc 0xB0 n'existe pas.
- Format de cette sauvegarde : 2 emplacements (en-tête « .SAV » de 0x200 + sauvegarde), 0x40400 octets pour 128 Ko ;
  taille d'un emplacement déduite de la taille du fichier (le champ 0x58 valait 0xFFFFFFFF sur la console), plus récent
  d'après le nombre d'écritures (0x34). Le chemin 1, 1, 3 existe aussi pour les jeux 3DS (image chiffrée, sans « .SAV »).
- Écrire la sauvegarde d'un jeu installé (comme PKSM) : `FSFILE_Write` puis `FSUSER_ControlArchive(...COMMIT_SAVE_DATA)`
  (sinon rien n'est gardé), puis `FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE, (SLOT_SD << 32) | (lowId & ~0xFF))`,
  sinon le jeu croit sa sauvegarde corrompue (protection contre le retour en arrière). Jeu jamais lancé : pas d'archive.
- Icône (SMDH) d'un titre : la lire en entier (0x36C0 octets), comme Checkpoint ; une lecture partielle a échoué.
- `diagnostic.txt` (sdmc:/3ds/PokeBankSync) : journal de la recherche des jeux installés, à demander à l'utilisateur.
- Pas d'exceptions ni de RTTI (`-fno-exceptions -fno-rtti`, Makefile de devkitPro).
- Ne pas écrire sur la dernière case de l'écran (ligne 30, colonne 50/40) : la console défile.
