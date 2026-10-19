# PokeBank Sync

Homebrew 3DS qui cherche les sauvegardes Pokémon de la console et les envoie au site **PokeBank**
(dépôt `MonstersUniverse`), où elles deviennent des **sauvegardes stockées** : une partie nouvelle est ajoutée, une partie
déjà stockée par ce compte est mise à jour, avec les règles de « Mes sauvegardes » (voir le README du site, « Synchronisation
depuis une 3DS »). Envoi seulement : rien n'est jamais écrit dans les sauvegardes de la console.

- **Console** : 3DS avec un firmware modifié (Luma3DS) et le Homebrew Launcher.
- **Stack** : C++17, libctru (devkitARM), libcurl + mbedTLS (HTTPS), jansson (JSON). Interface en mode texte sur les deux
  écrans.

## Utilisation

1. Copier `PokeBankSync.3dsx` dans `sdmc:/3ds/` et le lancer depuis le Homebrew Launcher.
2. Sur le site, « Mes sauvegardes » → « Relier une 3DS » affiche un code (`ABCD-EFGH`, valable 10 minutes).
3. Sur la 3DS, **Y** : taper l'adresse du site (`https://...`), puis le code. La console reçoit un jeton, gardé dans
   `sdmc:/3ds/PokeBankSync/config.ini` ; le site l'affiche dans sa liste (« 3DS de <nom d'utilisateur de la console> »).
4. La liste (écran du haut) montre les sauvegardes trouvées et leur état : **nouvelle** (jamais envoyée depuis cette
   console), **modifiée** (changée depuis le dernier envoi), **envoyée** (identique au dernier envoi), illisible. Les
   nouvelles et modifiées sont cochées.
5. **X** envoie les sauvegardes cochées. Chaque ligne prend la réponse du site : ajoutée, mise à jour, à jour, refusée,
   erreur ; l'écran du bas affiche le message complet de la ligne choisie (pourquoi une sauvegarde est refusée : moins de
   temps de jeu que la version stockée, partie modifiée sur le serveur à télécharger d'abord, partie d'un autre compte...).

Touches : flèches haut/bas (gauche/droite : page entière), **A** cocher, **R** tout cocher, **L** tout décocher, **X**
envoyer, **Y** relier au site (ou changer de site), **SELECT** chercher de nouveau, **START** quitter.

## Sauvegardes cherchées

| Source | Comment | Fichier envoyé |
|---|---|---|
| Jeux 3DS installés ou sur cartouche : X, Y, Rubis Oméga, Saphir Alpha, Soleil, Lune, Ultra-Soleil, Ultra-Lune | Identifiant de titre (le même dans toutes les régions) ; fichier `main` de la sauvegarde du jeu | « Pokémon X.sav »... |
| Console virtuelle : Rouge, Vert, Bleu, Jaune, Or, Argent, Cristal | Identifiants de titre connus (repris de PKSM) ; pour les autres régions, un nom de jeu en « Pokémon » (hors Flipper et Trading Card Game) et une sauvegarde Game Boy de 32 Ko ; fichier `sav.dat` | Nom du jeu + `.sav` |
| Fichiers `.sav` de la carte SD (TWiLight Menu++, open_agb_firm, mGBA...) | Dossiers de `config.ini`, sous-dossiers compris (6 niveaux). Gardé si la ROM du même nom (`.nds`, `.gba`, `.gb`...), cherchée dans ces dossiers, est un jeu Pokémon principal (code du jeu dans son en-tête : Diamant à Blanc 2, Rubis à Vert Feuille, Rouge à Cristal) ; sans ROM, si son nom contient « pokemon » ou « pkmn » | Nom du fichier |

Les cartouches DS ne sont pas lues. Le site revérifie tout avec PKHeX : un fichier qui n'est pas une sauvegarde Pokémon
est refusé.

Une même partie trouvée à plusieurs endroits (cartouche et copie sur la SD) est envoyée plusieurs fois : le site garde la
plus avancée et refuse les autres (« moins de temps de jeu »), sans rien changer.

## Fichiers sur la carte SD

Dans `sdmc:/3ds/PokeBankSync/` :

- `config.ini` : `site=`, `jeton=` (ne pas le partager ; « Retirer » sur le site le rend inutilisable), et une ligne
  `dossier=` par dossier à parcourir. Sans ligne `dossier=` : `sdmc:/roms`, `sdmc:/3ds/open_agb_firm`, `sdmc:/3ds/mGBA`.
- `envois.txt` : empreinte SHA-256 de la dernière version envoyée de chaque sauvegarde (état « envoyée »). Vidé quand la
  console est reliée à un autre site ou un autre compte.
- `certificats.pem` (facultatif) : racines de confiance en plus de celles du programme, si le certificat du site n'est
  pas reconnu.

## HTTPS

Le SSL intégré à la 3DS est trop ancien pour les certificats actuels : la connexion passe par libcurl et mbedTLS (TLS
1.2), avec les racines de `data/racines.pem`, intégrées au programme : **ISRG Root X1** (Let's Encrypt) et **USERTrust RSA
/ ECC** (ZeroSSL, l'autre fournisseur de Caddy), exportées du magasin de certificats de Windows. L'aléa de mbedTLS vient
du service `ssl:C` de la console (`sslcInit`). La date de la console doit être juste (validité des certificats).

## Compiler

Prérequis : [devkitPro](https://github.com/devkitPro/installer/releases) avec « 3DS Development », puis dans son MSYS2 :
`pacman -S --needed 3ds-curl 3ds-mbedtls 3ds-zlib 3ds-jansson`.

```bat
compiler.cmd
```

Produit `PokeBankSync.3dsx` (`compiler.cmd clean` pour tout effacer). Pour l'envoyer sans retirer la carte SD : **Y** dans
le Homebrew Launcher, puis `3dslink PokeBankSync.3dsx` (outils de devkitPro).

## Organisation du code

| Fichier | Rôle |
|---|---|
| `source/main.cpp` | Écrans (liste en haut, détail et touches en bas), boucle principale, liaison, envoi |
| `source/sauvegardes.cpp` | Recherche des sauvegardes : jeux 3DS et Console virtuelle (identifiants de titre, nom de l'icône SMDH, archive de sauvegarde), fichiers `.sav` de la carte SD (en-tête des ROM) ; lecture |
| `source/site.cpp` | API du site (`/api/sync/liaison`, `/compte`, `/sauvegardes`) en HTTPS, messages d'erreur réseau |
| `source/envois.cpp` | Empreintes des derniers envois (`envois.txt`), SHA-256 |
| `source/config.cpp` | `config.ini` : site, jeton, dossiers |
| `source/texte.cpp` | UTF-8 → police de la console (page de code 437 : é, à, «...), découpage en lignes |
| `data/racines.pem` | Certificats racines de confiance, intégrés par le Makefile (`racines_pem`) |

## Crédits

| Projet | Utilisé pour | Licence |
|---|---|---|
| [libctru](https://github.com/devkitPro/libctru), devkitARM ([devkitPro](https://devkitpro.org)) | Accès à la console (écrans, fichiers, titres, clavier), compilation | zlib ; GPL pour la chaîne de compilation |
| [libcurl](https://curl.se) (paquet `3ds-curl`) | Requêtes HTTPS | curl (type MIT) |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) (paquet `3ds-mbedtls`) | TLS, SHA-256 | Apache-2.0 |
| [Jansson](https://github.com/akheron/jansson) (paquet `3ds-jansson`) | JSON du site | MIT |
| [zlib](https://zlib.net) (paquet `3ds-zlib`) | Dépendance de libcurl | zlib |
| [PKSM](https://github.com/FlagBrew/PKSM) (FlagBrew) | Identifiants de titre des jeux 3DS et de la Console virtuelle, codes des jeux DS, fichiers `main` / `sav.dat` des sauvegardes | GPL-3.0 |
| [Checkpoint](https://github.com/FlagBrew/Checkpoint) (FlagBrew) | Méthode de lecture de l'icône (SMDH) et de l'archive de sauvegarde d'un titre | GPL-3.0 |
| ISRG (Let's Encrypt), Sectigo (USERTrust) | Certificats racines de `data/racines.pem` | Certificats publics |

Pokémon et tous les noms associés sont des marques de Nintendo, Creatures Inc., GAME FREAK Inc. et The Pokémon Company.
PokeBank Sync est un projet de fan privé, sans lien avec eux.
