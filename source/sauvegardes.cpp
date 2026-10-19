#include "sauvegardes.hpp"

#include "texte.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <map>
#include <sys/stat.h>

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Jeux 3DS et Console virtuelle
// ---------------------------------------------------------------------------------------------------------------------

struct JeuConnu
{
    u64 titre;
    const char* nom;
    bool consoleVirtuelle; // sauvegarde « sav.dat » au lieu de « main »
};

// Identifiants de titre repris de PKSM (FlagBrew, GPL-3.0 : assets/romfs/config.json, titres par défaut). Les jeux 3DS
// ont le même identifiant dans toutes les régions ; pour la Console virtuelle, les autres régions sont reconnues par
// leur nom (voir estConsoleVirtuellePokemon).
const JeuConnu JEUX[] = {
    {0x0004000000055D00, "Pokémon X", false},
    {0x0004000000055E00, "Pokémon Y", false},
    {0x000400000011C400, "Pokémon Rubis Oméga", false},
    {0x000400000011C500, "Pokémon Saphir Alpha", false},
    {0x0004000000164800, "Pokémon Soleil", false},
    {0x0004000000175E00, "Pokémon Lune", false},
    {0x00040000001B5000, "Pokémon Ultra-Soleil", false},
    {0x00040000001B5100, "Pokémon Ultra-Lune", false},
    {0x0004000000171000, "Pokémon Rouge", true},
    {0x0004000000170D00, "Pokémon Vert", true},
    {0x0004000000171100, "Pokémon Bleu", true},
    {0x0004000000171200, "Pokémon Jaune", true},
    {0x0004000000172600, "Pokémon Or", true},
    {0x0004000000172700, "Pokémon Argent", true},
    {0x0004000000172800, "Pokémon Cristal", true},
};

constexpr u32 APPLICATION = 0x00040000; // partie haute des identifiants de jeux (ni mises à jour, ni démos, ni système)

const JeuConnu* jeuConnu(u64 titre)
{
    for (const auto& jeu : JEUX)
        if (jeu.titre == titre)
            return &jeu;
    return nullptr;
}

/// Nom du jeu dans son icône (SMDH) : français, anglais, japonais, puis les autres langues.
bool lireNomDuJeu(u64 titre, FS_MediaType media, std::string& nom)
{
    static const u32 cheminIcone[5] = {0, 0, 2, 0x6E6F6369 /* « icon » */, 0};
    u32 cheminArchive[4] = {static_cast<u32>(titre), static_cast<u32>(titre >> 32), static_cast<u32>(media), 0};
    Handle fichier;
    if (R_FAILED(FSUSER_OpenFileDirectly(&fichier, ARCHIVE_SAVEDATA_AND_CONTENT,
            FS_Path{PATH_BINARY, sizeof cheminArchive, cheminArchive},
            FS_Path{PATH_BINARY, sizeof cheminIcone, cheminIcone}, FS_OPEN_READ, 0)))
        return false;

    // En-tête (8 octets) puis 16 langues de 0x200 octets : nom court (0x40 caractères UTF-16), nom long, éditeur.
    std::vector<u8> smdh(8 + 16 * 0x200);
    u32 lus = 0;
    Result resultat = FSFILE_Read(fichier, &lus, 0, smdh.data(), smdh.size());
    FSFILE_Close(fichier);
    if (R_FAILED(resultat) || lus < smdh.size() || memcmp(smdh.data(), "SMDH", 4) != 0)
        return false;

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

/// Console virtuelle d'une autre région que celles de JEUX : un nom en « Pokémon », hors des jeux annexes
/// (Flipper, Trading Card Game)... puis une sauvegarde Game Boy de 32 Ko (estSauvegardeGameBoy). Le site revérifie.
bool estNomPokemon(const std::string& nom)
{
    auto simple = simplifier(nom);
    bool pokemon = simple.find("pokemon") != std::string::npos || nom.find("ポケモン") != std::string::npos
        || nom.find("포켓몬") != std::string::npos;
    if (!pokemon)
        return false;
    for (const char* annexe : {"pinball", "flipper", "card", "carte", "karten", "carta", "trading", "tcg"})
        if (simple.find(annexe) != std::string::npos)
            return false;
    return nom.find("ピンボール") == std::string::npos && nom.find("カード") == std::string::npos;
}

bool estSauvegardeGameBoy(FS_Archive archive)
{
    auto taille = tailleDansArchive(archive, "/sav.dat");
    return taille >= 0x8000 && taille <= 0x8100;
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
        std::string nom;
        if (!connu && !(lireNomDuJeu(titre, media, nom) && estNomPokemon(nom)))
            continue;

        FS_Archive archive;
        if (!ouvrirSauvegardeDuJeu(titre, media, archive))
            continue; // pas encore de sauvegarde (jeu jamais lancé)
        if (!connu && !estSauvegardeGameBoy(archive))
        {
            FSUSER_CloseArchive(archive);
            continue;
        }
        bool consoleVirtuelle = connu ? connu->consoleVirtuelle : true;
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
        s.nom = connu ? connu->nom : nom;
        s.fichier = s.nom + ".sav";
        char cle[48];
        snprintf(cle, sizeof cle, "titre:%016llX:%d", titre, static_cast<int>(media));
        s.cle = cle;
        s.detail = std::string(media == MEDIATYPE_GAME_CARD ? "cartouche" : "jeu installé")
            + (consoleVirtuelle ? ", Console virtuelle" : "");
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

} // namespace

void chercherJeux(std::vector<Sauvegarde>& trouvees)
{
    chercherSurSupport(MEDIATYPE_SD, trouvees);

    bool inseree = false;
    FS_CardType type;
    if (R_SUCCEEDED(FSUSER_CardSlotIsInserted(&inseree)) && inseree
        && R_SUCCEEDED(FSUSER_GetCardType(&type)) && type == CARD_CTR)
        chercherSurSupport(MEDIATYPE_GAME_CARD, trouvees);
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
        auto fichier = nomDeFichier(chemin);
        auto base = sansExtension(fichier);
        Sauvegarde s;
        auto rom = trouves.roms.find(minuscules(base));
        if (rom != trouves.roms.end())
        {
            s.nom = jeuDeLaRom(rom->second);
            if (s.nom.empty())
                continue; // un autre jeu
            s.detail = chemin;
        }
        else
        {
            auto simple = simplifier(base);
            if (simple.find("pokemon") == std::string::npos && simple.find("pkmn") == std::string::npos)
                continue;
            s.nom = base;
            s.detail = chemin + " (sans sa ROM : reconnue par son nom)";
        }
        s.origine = Origine::CarteSD;
        s.chemin = chemin;
        s.fichier = fichier;
        s.cle = "sd:" + chemin;
        trouvees.push_back(s);
    }
}

bool lireSauvegarde(const Sauvegarde& sauvegarde, std::vector<u8>& contenu, std::string& erreur)
{
    contenu.clear();
    if (sauvegarde.origine == Origine::CarteSD)
    {
        FILE* f = fopen(sauvegarde.chemin.c_str(), "rb");
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
