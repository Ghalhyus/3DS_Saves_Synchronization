# PokeBank Sync

Homebrew 3DS qui cherche les sauvegardes Pokémon de la console et les envoie au site **PokeBank**
(dépôt `MonstersUniverse`), où elles deviennent des **sauvegardes stockées** : une partie nouvelle est ajoutée, une partie
déjà stockée par ce compte est mise à jour, avec les règles de « Mes sauvegardes » (voir le README du site, « Synchronisation
depuis une 3DS »). Dans l'autre sens, **« Récupérer »** remet sur la console la version du site (sauvegardes DS pour
l'instant ; 3DS, GBA et Game Boy viendront ensuite), après une copie de celle de la console.

- **Console** : 3DS avec un firmware modifié (Luma3DS) et le Homebrew Launcher.
- **Stack** : C++17, libctru (devkitARM), libcurl + mbedTLS (HTTPS), jansson (JSON). Interface en mode texte sur les deux
  écrans.

## Utilisation

1. Copier `PokeBankSync.3dsx` dans `sdmc:/3ds/` et le lancer depuis le Homebrew Launcher.
2. Sur le site, « Synchronisation 3DS » (menu « Sauvegardes », `/sauvegardes/3ds`) → « Relier une 3DS » affiche un code (`ABCD-EFGH`, valable 10 minutes).
3. Sur la 3DS, **Y** : taper l'adresse du site (`https://...`), puis le code. La console reçoit un jeton, gardé dans
   `sdmc:/3ds/PokeBankSync/config.ini` ; le site l'affiche dans sa liste (« 3DS de <nom d'utilisateur de la console> »).
4. La liste (écran du haut) montre les sauvegardes trouvées et leur état : **nouvelle** (jamais envoyée depuis cette
   console), **modifiée** (changée depuis le dernier envoi), **envoyée** (identique au dernier envoi), illisible. Les
   nouvelles et modifiées sont cochées.
5. **X** envoie les sauvegardes cochées. Chaque ligne prend la réponse du site : ajoutée, mise à jour, à jour, refusée,
   erreur ; l'écran du bas affiche le message complet de la ligne choisie (pourquoi une sauvegarde est refusée : moins de
   temps de jeu que la version stockée, partie modifiée sur le serveur à télécharger d'abord, partie d'un autre compte...).

Touches : flèches haut/bas (gauche/droite : page entière), **A** cocher, **R** tout cocher, **L** tout décocher, **X**
envoyer, **Y** relier au site (ou changer de site), **SELECT** chercher de nouveau, **B** récupérer, **START** quitter.

### Récupérer (touche B)

La liste montre les sauvegardes stockées sur le site (« Jeu — Dresseur ») et ce que la console en ferait :

| État | Signification |
|---|---|
| **à récupérer** | Une sauvegarde de la même partie est sur la console, et le site dit qu'elle peut être remplacée : moins de temps de jeu sur la console, ou même temps de jeu mais modifiée par le site (retrait d'argent, transfert d'objets). Cochée d'office seulement dans ce second cas. |
| **à placer** | Pas encore sur la console : sera écrite à côté d'une ROM du même jeu qui n'a pas de sauvegarde. Jamais cochée d'office. |
| **à jour** | Identique sur la console et sur le site. |
| **refusée** | Le site refuse : la console a **plus de temps de jeu** (sa progression serait perdue : envoie-la plutôt) ou c'est **une autre partie** (autre dresseur) au même endroit. |
| **pas de ROM** | Aucune ROM de ce jeu sans sauvegarde sur la carte SD. |
| **plus tard** | Sauvegarde 3DS, GBA ou Game Boy : pas encore récupérable. |

**X** récupère les sauvegardes cochées, **B** revient à l'envoi (la liste est refaite : les sauvegardes écrites y sont
« envoyées »), **SELECT** relit la liste du site.

- **Où** (sauvegardes DS, TWiLight Menu++) : l'emplacement noté par le site lors du dernier envoi ou de la dernière
  récupération depuis cette console ; sinon une sauvegarde de la carte SD du même nom de fichier ; sinon
  `<dossier de la ROM>/saves/<nom de la ROM>.sav` (ou à côté de la ROM si TWiLight y range déjà ses sauvegardes) pour une
  ROM du même jeu (code de son en-tête ; en 4e génération la sauvegarde ne distingue pas Diamant de Perle ni HeartGold de
  SoulSilver : l'une ou l'autre convient), de préférence celle du même nom. ROM cherchées dans les dossiers de `config.ini`
  et à la racine de la carte (`sdmc:/X.nds` → `sdmc:/saves/X.sav`).
- **Avant d'écraser** : copie de la sauvegarde de la console dans `sdmc:/3ds/PokeBankSync/copies/<date>_<fichier>` (rien
  n'est écrit si la copie échoue) ; la version du site est écrite dans un fichier `.tmp`, qui ne remplace l'ancienne
  qu'une fois complet, après vérification de son empreinte SHA-256 (celle annoncée par la liste).
- Le site compare les deux versions avec PKHeX (même partie, temps de jeu) : la console ne sait pas lire le temps de jeu.
- Une récupération vaut « Télécharger » sur le site : le blocage de « Mettre à jour » après un retrait est levé.
- Cas sans issue pour l'instant : tu as joué sur la console **après** une modification du site sans la récupérer
  (envoi refusé, récupération aussi).

## Sauvegardes cherchées

| Source | Comment | Fichier envoyé |
|---|---|---|
| Jeux 3DS installés ou sur cartouche : X, Y, Rubis Oméga, Saphir Alpha, Soleil, Lune, Ultra-Soleil, Ultra-Lune | Identifiant de titre (le même dans toutes les régions) ; fichier `main` de la sauvegarde du jeu | « Pokémon X.sav »... |
| Console virtuelle (eShop ou injectée en CIA) : Rouge, Vert, Bleu, Jaune, Or, Argent, Cristal | Identifiant de titre de chaque version et langue (38 titres : japonais, anglais, allemand, français, espagnol, italien, coréen), qui donne la version exacte ; pour les jeux injectés, une sauvegarde `sav.dat` de 32 Ko reconnue **à son contenu**, comme le fait PKHeX (liste de l'équipe à sa place dans Rouge/Bleu/Jaune, Or/Argent ou Cristal, versions internationales et japonaises), quel que soit le nom du jeu | « Pokémon Rouge.sav »... ; jeu injecté : nom de son icône, sinon « Pokémon Or ou Argent.sav »... |
| Jeux GBA injectés en CIA (AGB_FIRM) : Rubis, Saphir, Émeraude, Rouge Feu, Vert Feuille | Titre dont le fichier spécial de « sauvegarde et contenu » (ouvert par le service bas niveau `PxiFS0`, comme PKSM : `FSUSER` le refuse ; ce service n'accepte qu'une connexion, déjà prise par le système, empruntée grâce à un appel système de Luma3DS ; lu comme PKSM : deux emplacements avec un en-tête `.SAV` de 0x200 octets, le plus récent d'après son nombre d'écritures) contient une sauvegarde de 3e génération (signature `0x08012025` de ses secteurs) : **quel que soit le nom** donné au jeu | Nom du jeu + `.sav` |
| Fichiers `.sav` de la carte SD (TWiLight Menu++...) | Dossiers de `config.ini`, sous-dossiers compris (6 niveaux). **Reconnu à son contenu, sans ROM**, avec les vérifications de PKHeX (`SaveUtil`) : 4e génération (512 Ko, fin du bloc général à sa taille avec la signature 0x20060623 ou 0x20070903 : Diamant/Perle 0xC100, Platine 0xCF2C, HeartGold/SoulSilver 0xF628), 5e génération (512 Ko, CRC16 du bloc d'informations : Noir/Blanc, Noir 2/Blanc 2, version exacte lue en 0x1941F), 3e génération (secteurs signés 0x08012025), 1re et 2e génération (liste de l'équipe). La ROM du même nom (`.nds`, `.gba`, `.gb`...), cherchée dans ces dossiers, le dossier de la sauvegarde, celui du dessus et la racine de la carte (TWiLight : `sdmc:/saves/X.sav` pour `sdmc:/X.nds`), ne sert qu'à nommer la version exacte (Diamant plutôt que « Diamant ou Perle ») | Nom du fichier |

Les jeux annexes (Ranger, Donjon Mystère, Conquest...) sont écartés : leur sauvegarde n'a pas la structure d'un jeu
principal, même avec « Pokémon » dans le nom. Vérifié sur de vraies sauvegardes (SoulSilver, Noire 2, Noir, Platine,
Ranger écartée) et sur des fichiers de zéros ou au hasard. Les cartouches DS ne sont pas lues. Le site revérifie tout avec PKHeX : un fichier qui n'est pas une sauvegarde Pokémon
est refusé.

Une même partie trouvée à plusieurs endroits (cartouche et copie sur la SD) est envoyée plusieurs fois : le site garde la
plus avancée et refuse les autres (« moins de temps de jeu »), sans rien changer.

## Fichiers sur la carte SD

Dans `sdmc:/3ds/PokeBankSync/` :

- `config.ini` : `site=`, `jeton=` (ne pas le partager ; « Retirer » sur le site le rend inutilisable), et une ligne
  `dossier=` par dossier à parcourir. Sans ligne `dossier=` : `sdmc:/saves` et `sdmc:/roms` (TWiLight Menu++ : dossier
  `saves` à côté des ROM, à la racine ou dans `roms/nds`...). La liste de la première version (`sdmc:/roms`,
  `sdmc:/3ds/open_agb_firm`, `sdmc:/3ds/mGBA`), écrite d'office à la liaison, est remplacée par celle-ci.
- `copies/` : copie datée de chaque sauvegarde de la console avant qu'une récupération ne la remplace.
- `envois.txt` : empreinte SHA-256 et nom de fichier de la dernière version envoyée de chaque sauvegarde (état « envoyée » ;
  un nom qui change, version exacte enfin reconnue, la rend « modifiée » et le site renomme la sauvegarde stockée). Vidé quand la
  console est reliée à un autre site ou un autre compte.
- `diagnostic.txt` : réécrit à chaque recherche. Pour chaque jeu installé hors de la liste connue (jeux injectés
  surtout) : façon dont le homebrew a été lancé, puis identifiant de titre, nom, fichiers de sa sauvegarde ordinaire,
  emplacements de sauvegarde GBA, codes d'erreur des deux façons d'ouvrir l'archive « sauvegarde et contenu » (icône,
  sauvegarde GBA), et pourquoi il est retenu ou écarté. À regarder quand un jeu n'apparaît pas.
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
| `source/main.cpp` | Écrans (liste en haut, détail et touches en bas), boucle principale, liaison, envoi, « Récupérer » (destination, comparaison, récupération) |
| `source/sauvegardes.cpp` | Recherche des sauvegardes : jeux 3DS et Console virtuelle (identifiants de titre, nom de l'icône SMDH, archive de sauvegarde), jeux GBA injectés (format AGB_FIRM), fichiers `.sav` de la carte SD (reconnus à leur contenu comme PKHeX, ROM pour le nom exact) ; lecture ; ROM DS et emplacement des sauvegardes de TWiLight Menu++, écriture sûre (copie, fichier temporaire) |
| `source/luma.s` | Appel système de Luma3DS `svcControlService` (svc 0xB0), pour emprunter la connexion du système à `PxiFS0` |
| `source/site.cpp` | API du site (`/api/sync/liaison`, `/compte`, `/sauvegardes` : envoi, liste, comparaison, téléchargement) en HTTPS, messages d'erreur réseau |
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
| [List of Pokémon titles for Nintendo 3DS](https://gist.github.com/Bl4ckSh4rk/256ed3b857c9677310837d5180121f35) (Bl4ckSh4rk) | Identifiants de titre de la Console virtuelle, par version et par langue (`JEUX` dans `sauvegardes.cpp`) | Données publiées (gist) |
| [PKHeX](https://github.com/kwsch/PKHeX) (Kaphotics et contributeurs) | Règles de reconnaissance des sauvegardes à leur contenu (`SaveUtil` : blocs de 4e génération, CRC16 de 5e génération, listes de l'équipe de 1re et 2e génération), numéros de version de 5e génération | GPL-3.0 |
| [PKSM](https://github.com/FlagBrew/PKSM) (FlagBrew) | Identifiants de titre des jeux 3DS et de la Console virtuelle (recoupés), codes des jeux DS, fichiers `main` / `sav.dat` des sauvegardes, format des sauvegardes GBA injectées (`titles/loader.cpp`) | GPL-3.0 |
| [Checkpoint](https://github.com/FlagBrew/Checkpoint) (FlagBrew) | Méthode de lecture de l'icône (SMDH) et de l'archive de sauvegarde d'un titre | GPL-3.0 |
| ISRG (Let's Encrypt), Sectigo (USERTrust) | Certificats racines de `data/racines.pem` | Certificats publics |

Pokémon et tous les noms associés sont des marques de Nintendo, Creatures Inc., GAME FREAK Inc. et The Pokémon Company.
PokeBank Sync est un projet de fan privé, sans lien avec eux.
