#include "sauvegardes.hpp"

#include "config.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <functional>
#include <map>
#include <sys/stat.h>

namespace {

/// Journal de la dernière recherche (sdmc:/3ds/PokeBankSync/diagnostic.txt) : ce qui a été lu pour chaque jeu installé
/// hors de JEUX, pour comprendre pourquoi un jeu (injecté surtout) n'est pas reconnu. Null en dehors de chercherJeux.
FILE* journal = nullptr;

void noter(const char* format, ...) __attribute__((format(printf, 1, 2)));
void noter(const char* format, ...)
{
    if (!journal)
        return;
    va_list arguments;
    va_start(arguments, format);
    vfprintf(journal, format, arguments);
    va_end(arguments);
}

// ---------------------------------------------------------------------------------------------------------------------
// Jeux 3DS et Console virtuelle
// ---------------------------------------------------------------------------------------------------------------------

struct JeuConnu
{
    u64 titre;
    const char* nom;
    const char* region; // Console virtuelle : langue de cette version ; nullptr pour les jeux 3DS (sauvegarde « main »)
};

// Les jeux 3DS ont le même identifiant de titre dans toutes les régions (repris de PKSM, FlagBrew, GPL-3.0 :
// assets/romfs/config.json). La Console virtuelle a un identifiant par version et par langue : liste des titres Pokémon
// 3DS de Bl4ckSh4rk (gist « List of Pokémon titles for Nintendo 3DS »), qui recoupe celle de PKSM. Un jeu Game Boy
// absent d'ici (injecté) est reconnu à sa sauvegarde, sans sa version exacte (voir jeuGameBoy).
const JeuConnu JEUX[] = {
    {0x0004000000055D00, "Pokémon X", nullptr},
    {0x0004000000055E00, "Pokémon Y", nullptr},
    {0x000400000011C400, "Pokémon Rubis Oméga", nullptr},
    {0x000400000011C500, "Pokémon Saphir Alpha", nullptr},
    {0x0004000000164800, "Pokémon Soleil", nullptr},
    {0x0004000000175E00, "Pokémon Lune", nullptr},
    {0x00040000001B5000, "Pokémon Ultra-Soleil", nullptr},
    {0x00040000001B5100, "Pokémon Ultra-Lune", nullptr},

    {0x0004000000170C00, "Pokémon Rouge", "japonais"},
    {0x0004000000170D00, "Pokémon Vert", "japonais"},
    {0x0004000000170E00, "Pokémon Bleu", "japonais"},
    {0x0004000000170F00, "Pokémon Jaune", "japonais"},
    {0x0004000000171000, "Pokémon Rouge", "anglais"},
    {0x0004000000171100, "Pokémon Bleu", "anglais"},
    {0x0004000000171200, "Pokémon Jaune", "anglais"},
    {0x0004000000171300, "Pokémon Rouge", "allemand"},
    {0x0004000000171400, "Pokémon Bleu", "allemand"},
    {0x0004000000171500, "Pokémon Jaune", "allemand"},
    {0x0004000000171600, "Pokémon Rouge", "français"},
    {0x0004000000171700, "Pokémon Bleu", "français"},
    {0x0004000000171800, "Pokémon Jaune", "français"},
    {0x0004000000171900, "Pokémon Rouge", "espagnol"},
    {0x0004000000171A00, "Pokémon Bleu", "espagnol"},
    {0x0004000000171B00, "Pokémon Jaune", "espagnol"},
    {0x0004000000171C00, "Pokémon Rouge", "italien"},
    {0x0004000000171D00, "Pokémon Bleu", "italien"},
    {0x0004000000171E00, "Pokémon Jaune", "italien"},

    {0x0004000000172300, "Pokémon Or", "japonais"},
    {0x0004000000172400, "Pokémon Argent", "japonais"},
    {0x0004000000172500, "Pokémon Cristal", "japonais"},
    {0x0004000000172600, "Pokémon Or", "anglais"},
    {0x0004000000172700, "Pokémon Argent", "anglais"},
    {0x0004000000172800, "Pokémon Cristal", "anglais"},
    {0x0004000000172900, "Pokémon Or", "allemand"},
    {0x0004000000172A00, "Pokémon Argent", "allemand"},
    {0x0004000000172B00, "Pokémon Cristal", "allemand"},
    {0x0004000000172C00, "Pokémon Or", "français"},
    {0x0004000000172D00, "Pokémon Argent", "français"},
    {0x0004000000172E00, "Pokémon Cristal", "français"},
    {0x0004000000172F00, "Pokémon Or", "espagnol"},
    {0x0004000000173000, "Pokémon Argent", "espagnol"},
    {0x0004000000173100, "Pokémon Cristal", "espagnol"},
    {0x0004000000173200, "Pokémon Or", "italien"},
    {0x0004000000173300, "Pokémon Argent", "italien"},
    {0x0004000000173400, "Pokémon Cristal", "italien"},
    {0x0004000000173500, "Pokémon Or", "coréen"},
    {0x0004000000173600, "Pokémon Argent", "coréen"},
};

constexpr u32 APPLICATION = 0x00040000; // partie haute des identifiants de jeux (ni mises à jour, ni démos, ni système)

const JeuConnu* jeuConnu(u64 titre)
{
    for (const auto& jeu : JEUX)
        if (jeu.titre == titre)
            return &jeu;
    return nullptr;
}

/// Fichier de l'archive « sauvegarde et contenu » d'un titre (icône, sauvegarde d'un jeu GBA injecté). Ouvert
/// directement (comme Checkpoint), sinon en ouvrant l'archive puis le fichier (comme PKSM) ; fermé à la destruction.
struct FichierDeContenu
{
    Handle fichier = 0;
    FS_Archive archive = 0;
    bool ouvert = false;
    Result direct = 0, parArchive = 0; // codes d'erreur des deux façons, pour le journal

    FichierDeContenu(u64 titre, FS_MediaType media, const u32* chemin, u32 tailleChemin)
    {
        u32 cheminArchive[4] = {static_cast<u32>(titre), static_cast<u32>(titre >> 32), static_cast<u32>(media), 0};
        FS_Path archivePath{PATH_BINARY, sizeof cheminArchive, cheminArchive};
        FS_Path filePath{PATH_BINARY, tailleChemin, chemin};
        direct = FSUSER_OpenFileDirectly(&fichier, ARCHIVE_SAVEDATA_AND_CONTENT, archivePath, filePath, FS_OPEN_READ, 0);
        if (R_SUCCEEDED(direct))
        {
            ouvert = true;
            return;
        }
        parArchive = FSUSER_OpenArchive(&archive, ARCHIVE_SAVEDATA_AND_CONTENT, archivePath);
        if (R_FAILED(parArchive))
        {
            archive = 0;
            return;
        }
        parArchive = FSUSER_OpenFile(&fichier, archive, filePath, FS_OPEN_READ, 0);
        ouvert = R_SUCCEEDED(parArchive);
    }

    ~FichierDeContenu()
    {
        if (ouvert)
            FSFILE_Close(fichier);
        if (archive)
            FSUSER_CloseArchive(archive);
    }

    FichierDeContenu(const FichierDeContenu&) = delete;
    FichierDeContenu& operator=(const FichierDeContenu&) = delete;

    void noterEchec(const char* quoi) const
    {
        noter("  %s : illisible (directement 0x%08lX, par l'archive 0x%08lX)\n", quoi,
            static_cast<unsigned long>(direct), static_cast<unsigned long>(parArchive));
    }
};

/// Nom du jeu dans son icône (SMDH) : français, anglais, japonais, puis les autres langues.
bool lireNomDuJeu(u64 titre, FS_MediaType media, std::string& nom)
{
    static const u32 cheminIcone[5] = {0, 0, 2, 0x6E6F6369 /* « icon » */, 0};
    FichierDeContenu icone(titre, media, cheminIcone, sizeof cheminIcone);
    if (!icone.ouvert)
    {
        icone.noterEchec("icone");
        return false;
    }

    // En-tête (8 octets) puis 16 langues de 0x200 octets : nom court (0x40 caractères UTF-16), nom long, éditeur ;
    // puis les réglages et les images. Lue en entier (0x36C0 octets), comme Checkpoint et le Homebrew Launcher : une
    // lecture partielle de ce fichier de contenu peut être refusée.
    std::vector<u8> smdh(0x36C0);
    u32 lus = 0;
    Result resultat = FSFILE_Read(icone.fichier, &lus, 0, smdh.data(), smdh.size());
    if (R_FAILED(resultat) || lus < 8 + 16 * 0x200 || memcmp(smdh.data(), "SMDH", 4) != 0)
    {
        noter("  icone : ouverte, lecture 0x%08lX, %lu octets lus, debut %02X %02X %02X %02X\n",
            static_cast<unsigned long>(resultat), static_cast<unsigned long>(lus), smdh[0], smdh[1], smdh[2], smdh[3]);
        return false;
    }

    for (int langue : {2, 1, 0, 3, 4, 5, 6, 7, 8, 9, 10, 11})
    {
        u16 court[0x41] = {};
        memcpy(court, smdh.data() + 8 + langue * 0x200, 0x80);
        u8 utf8[0x101] = {};
        utf16_to_utf8(utf8, court, sizeof utf8 - 1);
        if (utf8[0] != 0)
        {
            nom = reinterpret_cast<char*>(utf8);
            std::replace(nom.begin(), nom.end(), '\n', ' ');
            return true;
        }
    }
    return false;
}

bool ouvrirSauvegardeDuJeu(u64 titre, FS_MediaType media, FS_Archive& archive)
{
    u32 chemin[3] = {static_cast<u32>(media), static_cast<u32>(titre), static_cast<u32>(titre >> 32)};
    return R_SUCCEEDED(FSUSER_OpenArchive(&archive, ARCHIVE_USER_SAVEDATA, FS_Path{PATH_BINARY, sizeof chemin, chemin}));
}

/// Taille d'un fichier de la sauvegarde d'un jeu, 0 s'il n'existe pas.
u64 tailleDansArchive(FS_Archive archive, const char* nom)
{
    Handle fichier;
    if (R_FAILED(FSUSER_OpenFile(&fichier, archive, fsMakePath(PATH_ASCII, nom), FS_OPEN_READ, 0)))
        return 0;
    u64 taille = 0;
    FSFILE_GetSize(fichier, &taille);
    FSFILE_Close(fichier);
    return taille;
}

/// Contenu d'un fichier de la sauvegarde d'un jeu (au plus `tailleMax` octets) ; faux s'il n'existe pas ou dépasse.
bool lireDansArchive(FS_Archive archive, const char* nom, std::vector<u8>& contenu, u64 tailleMax)
{
    Handle fichier;
    if (R_FAILED(FSUSER_OpenFile(&fichier, archive, fsMakePath(PATH_ASCII, nom), FS_OPEN_READ, 0)))
        return false;
    u64 taille = 0;
    bool ok = R_SUCCEEDED(FSFILE_GetSize(fichier, &taille)) && taille > 0 && taille <= tailleMax;
    if (ok)
    {
        contenu.resize(taille);
        u32 lus = 0;
        ok = R_SUCCEEDED(FSFILE_Read(fichier, &lus, 0, contenu.data(), contenu.size())) && lus == contenu.size();
    }
    FSFILE_Close(fichier);
    return ok;
}

/// Jeu d'une sauvegarde Pokémon Game Boy (« sav.dat » de 32 Ko de la Console virtuelle, officielle ou injectée), reconnu
/// à son contenu comme le fait PKHeX : la liste de l'équipe (nombre de Pokémon, 6 au plus, puis 0xFF après le dernier)
/// à sa place dans chaque version, internationale ou japonaise. Vérifié sur de vraies sauvegardes Rouge, Bleu et
/// Argent (Noir et Rubis Oméga écartées). Nullptr si ce n'en est pas une.
const char* jeuGameBoy(const std::vector<u8>& sauvegarde)
{
    struct Liste
    {
        u32 position;
        const char* jeu;
    };
    static const Liste LISTES[] = {
        {0x2F2C, "Pokémon Rouge, Bleu ou Jaune"}, {0x2ED5, "Pokémon Rouge, Vert, Bleu ou Jaune"},
        {0x288A, "Pokémon Or ou Argent"}, {0x283E, "Pokémon Or ou Argent"},
        {0x2865, "Pokémon Cristal"}, {0x281A, "Pokémon Cristal"},
    };
    if (sauvegarde.size() < 0x8000 || sauvegarde.size() > 0x8100)
        return nullptr;
    for (const auto& liste : LISTES)
    {
        u8 nombre = sauvegarde[liste.position];
        if (nombre <= 6 && sauvegarde[liste.position + 1 + nombre] == 0xFF)
            return liste.jeu;
    }
    return nullptr;
}

// --- Jeux GBA injectés (Console virtuelle GBA faite maison, AGB_FIRM) ------------------------------------------------
// Pas d'archive de sauvegarde ordinaire : la sauvegarde est un fichier spécial de l'archive « sauvegarde et contenu »
// (chemin binaire 1, 1, 3), lu comme PKSM (titles/loader.cpp). Deux emplacements écrits à tour de rôle, chacun avec un
// en-tête de 0x200 octets (« .SAV » en 0x00, nombre d'écritures en 0x34, taille : voir lireEnteteGba), suivi de la
// sauvegarde GBA brute. PKSM vérifie aussi la signature (CMAC) de l'en-tête ; inutile ici, rien n'est réécrit.

constexpr u32 ENTETE_GBA = 0x200;
constexpr u32 SAUVEGARDE_GBA_MAX = 0x20000; // 128 Ko (puce flash des jeux Pokémon)

/// Lecture d'un fichier à une position donnée ; vrai si exactement `taille` octets ont été lus.
using Lecteur = std::function<bool(u64 position, void* sortie, u32 taille)>;

/// Service PxiFS0 (accès bas niveau au système de fichiers) : seul chemin vers la sauvegarde d'un jeu GBA injecté, comme
/// le fait PKSM (Archive::saveAndContents avec pxi). 0 s'il n'a pas pu être obtenu.
Handle servicePxi = 0;
bool servicePxiEssaye = false;

/// Appel système de Luma3DS (luma.s) : copie de la connexion d'un autre processus à un service.
extern "C" Result svcControlServiceLuma(u32 operation, Handle* sortie, const char* service);
constexpr u32 SERVICEOP_STEAL_CLIENT_SESSION = 0;

/// Luma3DS répond à svcGetSystemInfo(0x10000) (sa version) ; un autre noyau le refuse. Sans Luma, svc 0xB0 n'existe pas.
bool lumaPresent()
{
    s64 version = 0;
    return R_SUCCEEDED(svcGetSystemInfo(&version, 0x10000, 0));
}

Handle obtenirServicePxi()
{
    if (servicePxiEssaye)
        return servicePxi;
    servicePxiEssaye = true;
    // Chaque service PXI n'accepte qu'une connexion, déjà prise par le processus FS du système (3dbrew, « PXI
    // Services ») : une demande ordinaire est refusée (0xD0401834). Demandée sans attente, pour ne pas bloquer la console.
    srvSetBlockingPolicy(true);
    Result resultat = srvGetServiceHandle(&servicePxi, "PxiFS0");
    srvSetBlockingPolicy(false);
    if (R_SUCCEEDED(resultat))
    {
        noter("Service PxiFS0 (sauvegardes GBA) : ouvert.\n");
        return servicePxi;
    }
    servicePxi = 0;
    if (!lumaPresent())
    {
        noter("Service PxiFS0 (sauvegardes GBA) : refuse (0x%08lX), et pas de Luma3DS pour emprunter celui du systeme.\n",
            static_cast<unsigned long>(resultat));
        return 0;
    }
    // Luma3DS : on emprunte la connexion du système (lecture seule ici).
    Result emprunt = svcControlServiceLuma(SERVICEOP_STEAL_CLIENT_SESSION, &servicePxi, "PxiFS0");
    if (R_FAILED(emprunt))
        servicePxi = 0;
    noter("Service PxiFS0 (sauvegardes GBA) : refuse (0x%08lX), connexion du systeme %s (0x%08lX).\n",
        static_cast<unsigned long>(resultat), servicePxi ? "empruntee" : "refusee", static_cast<unsigned long>(emprunt));
    return servicePxi;
}

/// En-tête « .SAV » de l'emplacement à cette position : taille de la sauvegarde et nombre d'écritures (0x34) ; faux s'il
/// est absent. La taille est déduite de celle du fichier (`tailleEmplacement`, deux emplacements identiques) ; sans elle,
/// du champ de l'en-tête : 0x54 d'après 3dbrew (« AGB_FIRM »), 0x58 d'après la lecture de PKSM. Sur la console de
/// l'utilisateur, 0x58 valait 0xFFFFFFFF : d'où la taille du fichier d'abord.
bool lireEnteteGba(const Lecteur& lire, u64 position, u32 tailleEmplacement, u32& taille, u32& ecritures)
{
    u8 entete[ENTETE_GBA];
    if (!lire(position, entete, sizeof entete) || memcmp(entete, ".SAV", 4) != 0)
        return false;
    u32 champ54, champ58;
    memcpy(&ecritures, entete + 0x34, 4);
    memcpy(&champ54, entete + 0x54, 4);
    memcpy(&champ58, entete + 0x58, 4);
    noter("  GBA : en-tete en 0x%llX : %lu ecritures, champ 0x54 = 0x%lX, champ 0x58 = 0x%lX\n", position,
        static_cast<unsigned long>(ecritures), static_cast<unsigned long>(champ54), static_cast<unsigned long>(champ58));
    auto valide = [](u32 valeur) { return valeur > 0 && valeur <= SAUVEGARDE_GBA_MAX; };
    if (tailleEmplacement)
        taille = tailleEmplacement;
    else if (valide(champ54))
        taille = champ54;
    else if (valide(champ58))
        taille = champ58;
    else
        return false;
    return true;
}

/// Sauvegarde GBA brute de l'emplacement le plus récent, lue aux positions exactes comme PKSM (lire au-delà de la fin
/// de ce fichier spécial peut échouer). `tailleFichier` : 0 si inconnue. Faux si le fichier n'a pas ce format.
bool extraireSauvegardeGba(const Lecteur& lire, u64 tailleFichier, std::vector<u8>& sauvegarde)
{
    // Deux emplacements « en-tête de 0x200 octets + sauvegarde » : 0x40400 octets pour 128 Ko.
    u32 emplacement = 0;
    if (tailleFichier > 2 * ENTETE_GBA && (tailleFichier - 2 * ENTETE_GBA) % 2 == 0
        && (tailleFichier - 2 * ENTETE_GBA) / 2 <= SAUVEGARDE_GBA_MAX)
        emplacement = static_cast<u32>((tailleFichier - 2 * ENTETE_GBA) / 2);

    u64 position = 0; // de l'en-tête choisi
    u32 taille = 0, ecritures = 0;
    bool trouve = false;
    if (lireEnteteGba(lire, 0, emplacement, taille, ecritures))
    {
        trouve = true;
        u32 taille2 = 0, ecritures2 = 0;
        u64 second = ENTETE_GBA + taille;
        bool deuxieme = lireEnteteGba(lire, second, emplacement, taille2, ecritures2);
        if (deuxieme && ecritures2 == ecritures + 1) // écrit après le premier
            position = second, taille = taille2;
        noter("  GBA : emplacement %d choisi (0x%lX octets)\n", position ? 2 : 1, static_cast<unsigned long>(taille));
    }
    else if (emplacement) // premier emplacement jamais écrit : le second suit
    {
        trouve = lireEnteteGba(lire, ENTETE_GBA + emplacement, emplacement, taille, ecritures);
        position = ENTETE_GBA + emplacement;
        noter("  GBA : emplacement 1 vide, emplacement 2 %s\n", trouve ? "choisi" : "introuvable");
    }
    else // taille du fichier inconnue : le second est après une sauvegarde de taille inconnue
    {
        for (u32 essai = 0x400; essai <= SAUVEGARDE_GBA_MAX && !trouve; essai *= 2)
            if (lireEnteteGba(lire, ENTETE_GBA + essai, 0, taille, ecritures))
                trouve = true, position = ENTETE_GBA + essai;
        noter("  GBA : emplacement 1 vide ou illisible, emplacement 2 %s\n", trouve ? "trouve" : "introuvable");
    }
    if (!trouve)
        return false;
    sauvegarde.resize(taille);
    if (lire(position + ENTETE_GBA, sauvegarde.data(), taille))
        return true;
    noter("  GBA : lecture de la sauvegarde impossible\n");
    return false;
}

/// Sauvegarde d'un jeu GBA injecté : fichier spécial (chemin binaire 1, 1, 3) de l'archive « sauvegarde et contenu »,
/// ouvert par PxiFS0 comme PKSM (FSUSER le refuse : « argument invalide », 0xE0E046BE). Faux si le titre n'en a pas.
bool lireSauvegardeGba(u64 titre, FS_MediaType media, std::vector<u8>& sauvegarde)
{
    static const u32 cheminSauvegarde[5] = {1, 1, 3, 0, 0};
    u32 cheminArchive[4] = {static_cast<u32>(titre), static_cast<u32>(titre >> 32), static_cast<u32>(media), 0};
    Handle pxi = obtenirServicePxi();
    if (pxi)
    {
        FSPXI_Archive archive;
        Result resultat = FSPXI_OpenArchive(pxi, &archive, ARCHIVE_SAVEDATA_AND_CONTENT,
            FS_Path{PATH_BINARY, sizeof cheminArchive, cheminArchive});
        if (R_FAILED(resultat))
        {
            noter("  GBA : archive refusee par PxiFS0 (0x%08lX)\n", static_cast<unsigned long>(resultat));
            return false;
        }
        FSPXI_File fichier;
        resultat = FSPXI_OpenFile(pxi, &fichier, archive, FS_Path{PATH_BINARY, sizeof cheminSauvegarde, cheminSauvegarde},
            FS_OPEN_READ, 0);
        bool ok = false;
        if (R_SUCCEEDED(resultat))
        {
            // Pour le journal : taille du fichier et premiers octets (format attendu : « .SAV » en tête).
            u64 tailleFichier = 0;
            Result resultatTaille = FSPXI_GetFileSize(pxi, fichier, &tailleFichier);
            u8 debut[16] = {};
            u32 lusDebut = 0;
            Result resultatLecture = FSPXI_ReadFile(pxi, fichier, &lusDebut, 0, debut, sizeof debut);
            noter("  GBA : fichier de 0x%llX octets (0x%08lX), lecture 0x%08lX (%lu octets) :", tailleFichier,
                static_cast<unsigned long>(resultatTaille), static_cast<unsigned long>(resultatLecture),
                static_cast<unsigned long>(lusDebut));
            for (u8 octet : debut)
                noter(" %02X", octet);
            noter("\n");

            Lecteur lire = [&](u64 position, void* sortie, u32 taille) {
                u32 lus = 0;
                return R_SUCCEEDED(FSPXI_ReadFile(pxi, fichier, &lus, position, sortie, taille)) && lus == taille;
            };
            ok = extraireSauvegardeGba(lire, R_SUCCEEDED(resultatTaille) ? tailleFichier : 0, sauvegarde);
            FSPXI_CloseFile(pxi, fichier);
        }
        else
            noter("  GBA : pas de fichier de sauvegarde GBA (0x%08lX)\n", static_cast<unsigned long>(resultat));
        FSPXI_CloseArchive(pxi, archive);
        return ok;
    }

    // Sans PxiFS0 : essai par FSUSER (refusé jusqu'ici, gardé pour le journal).
    FichierDeContenu contenu(titre, media, cheminSauvegarde, sizeof cheminSauvegarde);
    if (!contenu.ouvert)
    {
        contenu.noterEchec("sauvegarde GBA");
        return false;
    }
    Lecteur lire = [&](u64 position, void* sortie, u32 taille) {
        u32 lus = 0;
        return R_SUCCEEDED(FSFILE_Read(contenu.fichier, &lus, position, sortie, taille)) && lus == taille;
    };
    u64 tailleFichier = 0;
    if (R_FAILED(FSFILE_GetSize(contenu.fichier, &tailleFichier)))
        tailleFichier = 0;
    return extraireSauvegardeGba(lire, tailleFichier, sauvegarde);
}

/// Sauvegarde de Rubis, Saphir, Émeraude, Rouge Feu ou Vert Feuille : des secteurs de 4 Ko terminés par la signature
/// 0x08012025 (en 0xFF8). Ne dépend pas du nom donné au jeu injecté.
bool estSauvegardePokemonGba(const std::vector<u8>& sauvegarde)
{
    for (size_t secteur = 0; secteur + 0x1000 <= sauvegarde.size(); secteur += 0x1000)
    {
        u32 signature;
        memcpy(&signature, sauvegarde.data() + secteur + 0xFF8, 4);
        if (signature == 0x08012025)
            return true;
    }
    return false;
}

std::string cleDuTitre(u64 titre, FS_MediaType media)
{
    char cle[48];
    snprintf(cle, sizeof cle, "titre:%016llX:%d", titre, static_cast<int>(media));
    return cle;
}

/// Fichiers à la racine de la sauvegarde d'un jeu, pour le journal (« /sav.dat (32784) »).
void noterFichiers(FS_Archive archive)
{
    Handle dossier;
    if (R_FAILED(FSUSER_OpenDirectory(&dossier, archive, fsMakePath(PATH_ASCII, "/"))))
        return;
    FS_DirectoryEntry entrees[8];
    u32 lues = 0;
    noter("  fichiers :");
    while (R_SUCCEEDED(FSDIR_Read(dossier, &lues, 8, entrees)) && lues > 0)
        for (u32 i = 0; i < lues; i++)
        {
            u8 nom[0x110] = {};
            utf16_to_utf8(nom, entrees[i].name, sizeof nom - 1);
            noter(" /%s (%llu)", reinterpret_cast<char*>(nom), entrees[i].fileSize);
        }
    noter("\n");
    FSDIR_Close(dossier);
}

/// Titre absent de JEUX : Console virtuelle Game Boy d'une autre région ou injectée (« sav.dat » reconnu à son
/// contenu), sinon jeu GBA injecté (sauvegarde de 3e génération reconnue à son contenu, quel que soit son nom).
void chercherJeuNonRepertorie(u64 titre, FS_MediaType media, std::vector<Sauvegarde>& trouvees)
{
    std::string nom;
    bool nomLu = lireNomDuJeu(titre, media, nom);
    noter("%016llX %s \"%s\"\n", titre, media == MEDIATYPE_GAME_CARD ? "cartouche" : "SD", nomLu ? nom.c_str() : "(nom illisible)");
    Sauvegarde s;
    s.titre = titre;
    s.media = media;
    s.cle = cleDuTitre(titre, media);

    FS_Archive archive;
    const char* jeuGb = nullptr;
    if (ouvrirSauvegardeDuJeu(titre, media, archive))
    {
        noterFichiers(archive);
        std::vector<u8> sav;
        if (lireDansArchive(archive, "/sav.dat", sav, 0x8100))
        {
            jeuGb = jeuGameBoy(sav);
            noter("  sav.dat : %s\n", jeuGb ? jeuGb : "pas une sauvegarde Pokemon Game Boy");
        }
        FSUSER_CloseArchive(archive);
    }
    else
        noter("  pas de sauvegarde ordinaire\n");

    bool gameBoy = jeuGb != nullptr;
    if (gameBoy)
    {
        s.origine = Origine::ConsoleVirtuelle;
        s.fichierInterne = "/sav.dat";
        s.detail = "jeu installé, Game Boy (Console virtuelle), titre " + s.cle.substr(6, 16);
        if (!nomLu)
            nom = jeuGb;
    }
    else
    {
        // Pas un Game Boy Pokémon : peut-être un jeu GBA (certains ont aussi une sauvegarde ordinaire).
        std::vector<u8> sauvegarde;
        if (!lireSauvegardeGba(titre, media, sauvegarde))
        {
            noter("  -> ecarte\n");
            return;
        }
        if (!estSauvegardePokemonGba(sauvegarde))
        {
            noter("  -> ecarte : sauvegarde GBA qui n'est pas de Pokemon (3e generation)\n");
            return;
        }
        s.origine = Origine::JeuGBA;
        s.detail = "jeu installé, GBA (injecté), titre " + s.cle.substr(6, 16);
        if (!nomLu)
            nom = "Jeu GBA " + s.cle.substr(6, 16);
    }
    noter("  -> retenu (%s)\n", gameBoy ? "Game Boy" : "GBA");
    s.nom = nom;
    s.fichier = nom + ".sav";
    trouvees.push_back(s);
}

void chercherSurSupport(FS_MediaType media, std::vector<Sauvegarde>& trouvees)
{
    u32 nombre = 0;
    if (R_FAILED(AM_GetTitleCount(media, &nombre)) || nombre == 0)
        return;
    std::vector<u64> titres(nombre);
    u32 lus = 0;
    if (R_FAILED(AM_GetTitleList(&lus, media, nombre, titres.data())))
        return;
    titres.resize(lus);

    for (u64 titre : titres)
    {
        if (static_cast<u32>(titre >> 32) != APPLICATION)
            continue;
        auto* connu = jeuConnu(titre);
        if (!connu)
        {
            chercherJeuNonRepertorie(titre, media, trouvees);
            continue;
        }

        FS_Archive archive;
        if (!ouvrirSauvegardeDuJeu(titre, media, archive))
            continue; // pas encore de sauvegarde (jeu jamais lancé)
        bool consoleVirtuelle = connu->region != nullptr;
        const char* interne = consoleVirtuelle ? "/sav.dat" : "/main";
        bool present = tailleDansArchive(archive, interne) > 0;
        FSUSER_CloseArchive(archive);
        if (!present)
            continue;

        Sauvegarde s;
        s.origine = consoleVirtuelle ? Origine::ConsoleVirtuelle : Origine::Jeu3DS;
        s.titre = titre;
        s.media = media;
        s.fichierInterne = interne;
        s.nom = connu->nom;
        s.fichier = s.nom + ".sav";
        s.cle = cleDuTitre(titre, media);
        s.detail = std::string(media == MEDIATYPE_GAME_CARD ? "cartouche" : "jeu installé")
            + (consoleVirtuelle ? std::string(", Console virtuelle, version en ") + connu->region : "");
        trouvees.push_back(s);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Carte SD
// ---------------------------------------------------------------------------------------------------------------------

struct CodeDeJeu
{
    const char* code;
    const char* nom;
};

// Code du jeu dans l'en-tête de la ROM (3 premiers caractères ; le 4e est la langue).
const CodeDeJeu JEUX_DS[] = {
    {"ADA", "Pokémon Diamant"}, {"APA", "Pokémon Perle"}, {"CPU", "Pokémon Platine"},
    {"IPK", "Pokémon Or HeartGold"}, {"IPG", "Pokémon Argent SoulSilver"},
    {"IRB", "Pokémon Noir"}, {"IRA", "Pokémon Blanc"}, {"IRE", "Pokémon Noir 2"}, {"IRD", "Pokémon Blanc 2"},
};
const CodeDeJeu JEUX_GBA[] = {
    {"AXV", "Pokémon Rubis"}, {"AXP", "Pokémon Saphir"}, {"BPE", "Pokémon Émeraude"},
    {"BPR", "Pokémon Rouge Feu"}, {"BPG", "Pokémon Vert Feuille"},
};
// Début du titre dans l'en-tête des jeux Game Boy (0x134).
const CodeDeJeu JEUX_GB[] = {
    {"POKEMON RED", "Pokémon Rouge"}, {"POKEMON GREEN", "Pokémon Vert"}, {"POKEMON BLUE", "Pokémon Bleu"},
    {"POKEMON YELLOW", "Pokémon Jaune"}, {"POKEMON_GLD", "Pokémon Or"}, {"POKEMON_SLV", "Pokémon Argent"},
    {"PM_CRYSTAL", "Pokémon Cristal"},
};

std::string minuscules(std::string s)
{
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z')
            c = c - 'A' + 'a';
    return s;
}

std::string extension(const std::string& nom)
{
    auto point = nom.rfind('.');
    return point == std::string::npos ? "" : minuscules(nom.substr(point));
}

std::string sansExtension(const std::string& nom)
{
    auto point = nom.rfind('.');
    return point == std::string::npos ? nom : nom.substr(0, point);
}

bool estRom(const std::string& ext)
{
    return ext == ".nds" || ext == ".dsi" || ext == ".srl" || ext == ".gba" || ext == ".gb" || ext == ".gbc";
}

struct Trouves
{
    std::vector<std::string> sauvegardes;
    std::map<std::string, std::string> roms; // nom sans extension, en minuscules → chemin
};

void parcourir(const std::string& dossier, int profondeur, Trouves& trouves)
{
    DIR* d = opendir(dossier.c_str());
    if (!d)
        return;
    while (dirent* entree = readdir(d))
    {
        std::string nom = entree->d_name;
        if (nom.empty() || nom[0] == '.')
            continue;
        std::string chemin = dossier + "/" + nom;
        struct stat infos;
        if (stat(chemin.c_str(), &infos) != 0)
            continue;
        if (S_ISDIR(infos.st_mode))
        {
            if (profondeur < 6)
                parcourir(chemin, profondeur + 1, trouves);
            continue;
        }
        auto ext = extension(nom);
        if (ext == ".sav" && infos.st_size >= 0x2000 && static_cast<size_t>(infos.st_size) <= TAILLE_MAX)
            trouves.sauvegardes.push_back(chemin);
        else if (estRom(ext))
            trouves.roms.emplace(minuscules(sansExtension(nom)), chemin); // le premier trouvé l'emporte
    }
    closedir(d);
}

bool lireOctets(const std::string& chemin, long position, char* sortie, size_t taille)
{
    FILE* f = fopen(chemin.c_str(), "rb");
    if (!f)
        return false;
    bool ok = fseek(f, position, SEEK_SET) == 0 && fread(sortie, 1, taille, f) == taille;
    fclose(f);
    return ok;
}

/// Nom du jeu Pokémon de cette ROM, ou vide si ce n'en est pas un (ou si l'en-tête est illisible).
std::string jeuDeLaRom(const std::string& rom)
{
    auto ext = extension(rom);
    char entete[17] = {};
    if (ext == ".gba")
    {
        if (lireOctets(rom, 0xAC, entete, 4))
            for (const auto& jeu : JEUX_GBA)
                if (strncmp(entete, jeu.code, 3) == 0)
                    return jeu.nom;
    }
    else if (ext == ".gb" || ext == ".gbc")
    {
        if (lireOctets(rom, 0x134, entete, 16))
            for (const auto& jeu : JEUX_GB)
                if (strncmp(entete, jeu.code, strlen(jeu.code)) == 0)
                    return jeu.nom;
    }
    else if (lireOctets(rom, 0x0C, entete, 4))
    {
        for (const auto& jeu : JEUX_DS)
            if (strncmp(entete, jeu.code, 3) == 0)
                return jeu.nom;
    }
    return "";
}

std::string nomDeFichier(const std::string& chemin)
{
    auto barre = chemin.rfind('/');
    return barre == std::string::npos ? chemin : chemin.substr(barre + 1);
}

/// « sdmc:/saves/x.sav » → « sdmc:/saves » ; « sdmc:/saves » → « sdmc:/ ».
std::string dossierParent(const std::string& chemin)
{
    auto barre = chemin.rfind('/');
    if (barre == std::string::npos)
        return "";
    auto parent = chemin.substr(0, barre);
    return !parent.empty() && parent.back() == ':' ? parent + "/" : parent;
}

std::string joindre(const std::string& dossier, const std::string& nom)
{
    return !dossier.empty() && dossier.back() == '/' ? dossier + nom : dossier + "/" + nom;
}

/// ROM du même nom que la sauvegarde, dans son dossier, celui du dessus ou à la racine de la carte : TWiLight Menu++
/// range les sauvegardes dans un dossier « saves » à côté des ROM (sdmc:/saves pour des ROM à la racine de la carte,
/// sdmc:/roms/nds/saves...).
std::string romVoisine(const std::string& sauvegarde, const std::string& base)
{
    auto dossier = dossierParent(sauvegarde);
    auto parent = dossierParent(dossier);
    for (const auto& lieu : {dossier, parent, std::string("sdmc:/")})
    {
        if (lieu.empty() || (lieu == "sdmc:/" && (dossier == lieu || parent == lieu)))
            continue;
        for (const char* ext : {".nds", ".dsi", ".srl", ".gba", ".gb", ".gbc"})
        {
            auto rom = joindre(lieu, base + ext);
            struct stat infos;
            if (stat(rom.c_str(), &infos) == 0 && S_ISREG(infos.st_mode))
                return rom;
        }
    }
    return "";
}

/// Contenu d'un fichier de la carte SD (au plus TAILLE_MAX octets) ; faux avec un message s'il ne peut pas être lu.
bool lireFichierSD(const std::string& chemin, std::vector<u8>& contenu, std::string& erreur)
{
    FILE* f = fopen(chemin.c_str(), "rb");
    if (!f)
    {
        erreur = "Fichier introuvable sur la carte SD.";
        return false;
    }
    fseek(f, 0, SEEK_END);
    long taille = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (taille <= 0 || static_cast<size_t>(taille) > TAILLE_MAX)
    {
        fclose(f);
        erreur = "Fichier vide ou trop gros.";
        return false;
    }
    contenu.resize(taille);
    bool ok = fread(contenu.data(), 1, contenu.size(), f) == contenu.size();
    fclose(f);
    if (!ok)
        erreur = "Lecture impossible sur la carte SD.";
    return ok;
}

u32 lire32(const std::vector<u8>& donnees, size_t position)
{
    u32 valeur;
    memcpy(&valeur, donnees.data() + position, 4);
    return valeur;
}

/// CRC16-CCITT (valeur initiale 0xFFFF), somme de contrôle des sauvegardes de 5e génération.
u16 crc16Ccitt(const u8* donnees, size_t taille)
{
    u16 crc = 0xFFFF;
    for (size_t i = 0; i < taille; i++)
    {
        crc ^= static_cast<u16>(donnees[i] << 8);
        for (int bit = 0; bit < 8; bit++)
            crc = (crc & 0x8000) ? static_cast<u16>((crc << 1) ^ 0x1021) : static_cast<u16>(crc << 1);
    }
    return crc;
}

/// 4e génération (PKHeX : IsValidGeneralFooter2) : le bloc général se termine par sa taille (en -0xC) et la signature
/// du kit de développement (en -0x8 : 0x20060623, ou 0x20070903 en coréen). Vérifié sur les deux copies de la
/// sauvegarde (0 et 0x40000) : PKHeX ne regarde que la seconde, absente si le jeu n'a été sauvegardé qu'une fois.
bool blocGeneral4Valide(const std::vector<u8>& sauvegarde, u32 tailleBloc)
{
    for (size_t copie : {size_t(0), size_t(0x40000)})
    {
        u32 taille = lire32(sauvegarde, copie + tailleBloc - 0xC);
        u32 signature = lire32(sauvegarde, copie + tailleBloc - 0x8);
        if (taille == tailleBloc && (signature == 0x20060623 || signature == 0x20070903))
            return true;
    }
    return false;
}

/// 5e génération (PKHeX : IsValidFooter5) : bloc d'informations 0x100 octets avant la fin de la partie principale,
/// suivi (0x10 octets plus loin) de sa somme CRC16.
bool piedDePage5Valide(const std::vector<u8>& sauvegarde, size_t partiePrincipale, size_t tailleInfos)
{
    size_t debut = partiePrincipale - 0x100;
    size_t fin = debut + tailleInfos + 0x10;
    u16 attendu;
    memcpy(&attendu, sauvegarde.data() + fin - 2, 2);
    return attendu == crc16Ccitt(sauvegarde.data() + debut, tailleInfos);
}

/// Version d'une sauvegarde de 5e génération : octet du jeu dans les informations du dresseur (0x1941F ; numéros de
/// PKHeX : 20 Blanc, 21 Noir, 22 Blanc 2, 23 Noir 2). Vérifié sur de vraies sauvegardes Noir et Noire 2.
std::string versionGen5(const std::vector<u8>& sauvegarde, const char* paire)
{
    switch (sauvegarde[0x1941F])
    {
    case 20: return "Pokémon Blanc";
    case 21: return "Pokémon Noir";
    case 22: return "Pokémon Blanc 2";
    case 23: return "Pokémon Noir 2";
    default: return paire;
    }
}

/// Jeu Pokémon principal reconnu au seul contenu de la sauvegarde, comme le fait PKHeX (SaveUtil, mêmes vérifications
/// dans le même ordre pour les jeux DS) ; vide si ce n'en est pas un (Ranger, Donjon Mystère, autre jeu...). La 4e
/// génération ne dit pas lequel des deux jeux d'une paire (Diamant ou Perle) : la ROM, si elle est là, le précise.
std::string jeuDuContenu(const std::vector<u8>& sauvegarde)
{
    if (sauvegarde.size() == 0x80000)
    {
        if (blocGeneral4Valide(sauvegarde, 0xC100))
            return "Pokémon Diamant ou Perle";
        if (blocGeneral4Valide(sauvegarde, 0xCF2C))
            return "Pokémon Platine";
        if (blocGeneral4Valide(sauvegarde, 0xF628))
            return "Pokémon Or HeartGold ou Argent SoulSilver";
        if (piedDePage5Valide(sauvegarde, 0x24000, 0x8C))
            return versionGen5(sauvegarde, "Pokémon Noir ou Blanc");
        if (piedDePage5Valide(sauvegarde, 0x26000, 0x94))
            return versionGen5(sauvegarde, "Pokémon Noir 2 ou Blanc 2");
        return "";
    }
    if (sauvegarde.size() >= 0x10000 && sauvegarde.size() <= 0x20100 && estSauvegardePokemonGba(sauvegarde))
        return "Pokémon 3e génération (GBA)";
    if (const char* jeu = jeuGameBoy(sauvegarde))
        return jeu;
    return "";
}

} // namespace

void chercherJeux(std::vector<Sauvegarde>& trouvees)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/PokeBankSync", 0777);
    journal = fopen("sdmc:/3ds/PokeBankSync/diagnostic.txt", "w");
    u64 programme = 0;
    APT_GetProgramID(&programme);
    noter("PokeBank Sync : jeux installes hors liste connue (identifiant, support, nom), derniere recherche.\n");
    noter("Lance %s, titre %016llX.\n\n", envIsHomebrew() ? "en homebrew (.3dsx)" : "en CIA", programme);
    chercherSurSupport(MEDIATYPE_SD, trouvees);

    bool inseree = false;
    FS_CardType type;
    if (R_SUCCEEDED(FSUSER_CardSlotIsInserted(&inseree)) && inseree
        && R_SUCCEEDED(FSUSER_GetCardType(&type)) && type == CARD_CTR)
        chercherSurSupport(MEDIATYPE_GAME_CARD, trouvees);
    if (journal)
        fclose(journal);
    journal = nullptr;
}

void chercherCarteSD(const std::vector<std::string>& dossiers, std::vector<Sauvegarde>& trouvees)
{
    Trouves trouves;
    for (auto dossier : dossiers)
    {
        while (dossier.size() > 1 && dossier.back() == '/')
            dossier.pop_back();
        parcourir(dossier, 0, trouves);
    }

    std::sort(trouves.sauvegardes.begin(), trouves.sauvegardes.end());
    trouves.sauvegardes.erase(std::unique(trouves.sauvegardes.begin(), trouves.sauvegardes.end()), trouves.sauvegardes.end());

    for (const auto& chemin : trouves.sauvegardes)
    {
        // Le contenu décide si c'est un jeu Pokémon principal ; la ROM du même nom, si elle est là, donne la version
        // exacte (Diamant plutôt que « Diamant ou Perle »). Une ROM d'un autre code (jeu modifié) ne l'emporte pas.
        std::vector<u8> contenu;
        std::string erreur;
        if (!lireFichierSD(chemin, contenu, erreur))
            continue;
        auto jeu = jeuDuContenu(contenu);
        if (jeu.empty())
            continue;
        auto fichier = nomDeFichier(chemin);
        auto base = sansExtension(fichier);
        auto connue = trouves.roms.find(minuscules(base));
        auto rom = connue != trouves.roms.end() ? connue->second : romVoisine(chemin, base);
        auto exact = rom.empty() ? std::string() : jeuDeLaRom(rom);

        Sauvegarde s;
        s.nom = exact.empty() ? jeu : exact;
        s.detail = exact.empty() ? chemin + " (version d'après la sauvegarde)" : chemin;
        s.origine = Origine::CarteSD;
        s.chemin = chemin;
        s.fichier = fichier;
        s.cle = "sd:" + chemin;
        trouvees.push_back(s);
    }
}

void terminerJeux()
{
    if (servicePxi)
        svcCloseHandle(servicePxi);
    servicePxi = 0;
    servicePxiEssaye = false;
}

bool lireSauvegarde(const Sauvegarde& sauvegarde, std::vector<u8>& contenu, std::string& erreur)
{
    contenu.clear();
    if (sauvegarde.origine == Origine::CarteSD)
        return lireFichierSD(sauvegarde.chemin, contenu, erreur);

    if (sauvegarde.origine == Origine::JeuGBA)
    {
        if (lireSauvegardeGba(sauvegarde.titre, sauvegarde.media, contenu))
            return true;
        erreur = "Lecture de la sauvegarde du jeu GBA impossible.";
        return false;
    }

    FS_Archive archive;
    if (!ouvrirSauvegardeDuJeu(sauvegarde.titre, sauvegarde.media, archive))
    {
        erreur = sauvegarde.media == MEDIATYPE_GAME_CARD ? "Cartouche retirée ?" : "Sauvegarde du jeu inaccessible.";
        return false;
    }
    Handle fichier;
    bool ok = false;
    if (R_SUCCEEDED(FSUSER_OpenFile(&fichier, archive, fsMakePath(PATH_ASCII, sauvegarde.fichierInterne.c_str()), FS_OPEN_READ, 0)))
    {
        u64 taille = 0;
        if (R_SUCCEEDED(FSFILE_GetSize(fichier, &taille)) && taille > 0 && taille <= TAILLE_MAX)
        {
            contenu.resize(taille);
            u32 lus = 0;
            ok = R_SUCCEEDED(FSFILE_Read(fichier, &lus, 0, contenu.data(), contenu.size())) && lus == contenu.size();
        }
        FSFILE_Close(fichier);
    }
    FSUSER_CloseArchive(archive);
    if (!ok)
        erreur = "Lecture de la sauvegarde du jeu impossible.";
    return ok;
}

// ---------------------------------------------------------------------------------------------------------------------
// Récupération : ROM DS, emplacement des sauvegardes de TWiLight Menu++, écriture sûre
// ---------------------------------------------------------------------------------------------------------------------

bool fichierExiste(const std::string& chemin)
{
    struct stat infos;
    return stat(chemin.c_str(), &infos) == 0 && S_ISREG(infos.st_mode);
}

namespace {

/// Sauvegarde que TWiLight Menu++ associe à cette ROM : dans le dossier « saves » à côté d'elle (réglage par défaut),
/// ou à côté d'elle si elle y est déjà. Pour des ROM à la racine : sdmc:/saves/<nom de la ROM>.sav.
std::string emplacementTwilight(const std::string& rom)
{
    auto dossier = dossierParent(rom);
    auto nom = sansExtension(nomDeFichier(rom)) + ".sav";
    auto dansSaves = joindre(joindre(dossier, "saves"), nom);
    auto aCote = joindre(dossier, nom);
    if (!fichierExiste(dansSaves) && fichierExiste(aCote))
        return aCote;
    return dansSaves;
}

bool estRomDs(const std::string& ext)
{
    return ext == ".nds" || ext == ".dsi" || ext == ".srl";
}

/// Écrit le fichier en entier ; faux s'il n'a pas la bonne taille à la fin.
bool ecrireFichier(const std::string& chemin, const std::vector<u8>& contenu)
{
    FILE* f = fopen(chemin.c_str(), "wb");
    if (!f)
        return false;
    bool ok = fwrite(contenu.data(), 1, contenu.size(), f) == contenu.size();
    ok = fclose(f) == 0 && ok;
    struct stat infos;
    return ok && stat(chemin.c_str(), &infos) == 0 && static_cast<size_t>(infos.st_size) == contenu.size();
}

} // namespace

std::vector<RomDs> chercherRomsDs(const std::vector<std::string>& dossiers)
{
    Trouves trouves;
    for (auto dossier : dossiers)
    {
        while (dossier.size() > 1 && dossier.back() == '/')
            dossier.pop_back();
        parcourir(dossier, 0, trouves);
    }
    // Racine de la carte, sans ses sous-dossiers : TWiLight Menu++ y lit les ROM posées là (sauvegardes dans sdmc:/saves).
    if (DIR* d = opendir("sdmc:/"))
    {
        while (dirent* entree = readdir(d))
        {
            std::string nom = entree->d_name;
            if (nom.empty() || nom[0] == '.' || !estRomDs(extension(nom)))
                continue;
            auto chemin = joindre("sdmc:/", nom);
            if (fichierExiste(chemin))
                trouves.roms.emplace(minuscules(sansExtension(nom)), chemin);
        }
        closedir(d);
    }

    std::vector<RomDs> roms;
    for (const auto& [base, chemin] : trouves.roms)
    {
        char code[5] = {};
        if (!estRomDs(extension(chemin)) || !lireOctets(chemin, 0x0C, code, 4))
            continue;
        roms.push_back(RomDs{chemin, std::string(code, 3), emplacementTwilight(chemin)});
    }
    return roms;
}

std::vector<std::string> codesPourFamille(const std::string& famille)
{
    if (famille == "DP")
        return {"ADA", "APA"}; // la sauvegarde ne dit pas lequel des deux
    if (famille == "Pt")
        return {"CPU"};
    if (famille == "HGSS")
        return {"IPK", "IPG"};
    if (famille == "B")
        return {"IRB"};
    if (famille == "W")
        return {"IRA"};
    if (famille == "BW")
        return {"IRB", "IRA"};
    if (famille == "B2")
        return {"IRE"};
    if (famille == "W2")
        return {"IRD"};
    if (famille == "B2W2")
        return {"IRE", "IRD"};
    return {};
}

bool ecrireSauvegardeSD(const std::string& chemin, const std::vector<u8>& contenu, std::string& message)
{
    bool existe = fichierExiste(chemin);
    if (existe)
    {
        // Copie de sécurité de la sauvegarde de la console, datée, avant tout remplacement.
        mkdir("sdmc:/3ds", 0777);
        mkdir(DOSSIER_APPLI, 0777);
        auto copies = std::string(DOSSIER_APPLI) + "/copies";
        mkdir(copies.c_str(), 0777);
        time_t maintenant = time(nullptr);
        char date[32];
        strftime(date, sizeof date, "%Y%m%d-%H%M%S", localtime(&maintenant));
        auto copie = copies + "/" + date + "_" + nomDeFichier(chemin);
        std::vector<u8> ancienne;
        std::string erreur;
        if (!lireFichierSD(chemin, ancienne, erreur) || !ecrireFichier(copie, ancienne))
        {
            message = "Copie de sécurité impossible (" + copie + ") : rien n'a été écrit.";
            return false;
        }
        message = "Ancienne sauvegarde copiée dans " + copie + ".";
    }
    else
    {
        mkdir(dossierParent(chemin).c_str(), 0777); // « saves » à côté de la ROM, s'il n'existe pas encore
        message.clear();
    }

    auto temporaire = chemin + ".tmp";
    if (!ecrireFichier(temporaire, contenu))
    {
        remove(temporaire.c_str());
        message = "Écriture impossible sur la carte SD (pleine ou protégée ?) : rien n'a été remplacé.";
        return false;
    }
    if (existe && remove(chemin.c_str()) != 0)
    {
        remove(temporaire.c_str());
        message = "L'ancienne sauvegarde n'a pas pu être remplacée : rien n'a changé.";
        return false;
    }
    if (rename(temporaire.c_str(), chemin.c_str()) != 0)
    {
        message = "Renommage impossible : la sauvegarde du site est dans " + temporaire
            + (existe ? ", l'ancienne dans le dossier copies." : ".");
        return false;
    }
    return true;
}
