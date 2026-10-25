#pragma once

#include <3ds.h>
#include <string>
#include <vector>

enum class Origine
{
    Jeu3DS,           // X/Y à Ultra-Soleil/Ultra-Lune, installé ou sur cartouche : fichier « main » de sa sauvegarde
    ConsoleVirtuelle, // Rouge à Cristal, eShop ou injecté en CIA : fichier « sav.dat » de sa sauvegarde
    JeuGBA,           // Rubis à Vert Feuille injecté en CIA (AGB_FIRM) : fichier spécial de « sauvegarde et contenu »
    CarteSD,          // fichier .sav d'un émulateur ou de TWiLight Menu++ sur la carte SD
};

/// Sauvegarde Pokémon trouvée sur la console.
struct Sauvegarde
{
    Origine origine = Origine::CarteSD;
    std::string cle;     // identifiant stable, pour retrouver l'empreinte du dernier envoi (« titre:0004000000055D00 », chemin)
    std::string nom;     // affiché : « Pokémon X », « Pokémon Noir 2 »
    std::string fichier; // nom envoyé au site, affiché à côté de « Jeu — Dresseur » : « Pokémon X.sav »
    std::string detail;  // d'où elle vient : « cartouche », « installé », chemin sur la carte SD

    // Jeu 3DS et Console virtuelle
    u64 titre = 0;
    FS_MediaType media = MEDIATYPE_SD;
    std::string fichierInterne; // « /main » ou « /sav.dat »

    // Carte SD
    std::string chemin;
};

/// Jeux 3DS et Console virtuelle installés ou sur la cartouche : seuls les jeux Pokémon principaux (identifiants de titre
/// connus ; Game Boy d'autres régions ou injecté : un nom en « Pokémon » avec une sauvegarde « sav.dat » ; GBA injecté :
/// une sauvegarde de 3e génération, quel que soit son nom).
void chercherJeux(std::vector<Sauvegarde>& trouvees);

/// Fichiers .sav des dossiers donnés (sous-dossiers compris), gardés si leur contenu est celui d'un jeu Pokémon principal
/// (vérifications de PKHeX) ; la ROM du même nom, si elle est là, donne la version exacte.
void chercherCarteSD(const std::vector<std::string>& dossiers, std::vector<Sauvegarde>& trouvees);

/// Ferme le service PxiFS0 ouvert pour lire les jeux GBA injectés (fin du programme).
void terminerJeux();

/// Contenu actuel de la sauvegarde ; faux avec un message si elle ne peut pas être lue.
bool lireSauvegarde(const Sauvegarde& sauvegarde, std::vector<u8>& contenu, std::string& erreur);

/// Plus grosse sauvegarde envoyée (même limite que le site).
constexpr size_t TAILLE_MAX = 4 * 1024 * 1024;

// --- Récupération (écriture sur la console) ---------------------------------------------------------------------------

/// ROM DS trouvée sur la carte SD, et le fichier où TWiLight Menu++ range sa sauvegarde.
struct RomDs
{
    std::string chemin;     // « sdmc:/Pokemon - Version Noire 2.nds »
    std::string code;       // 3 premières lettres du code du jeu (« IRE »)
    std::string sauvegarde; // « sdmc:/saves/Pokemon - Version Noire 2.sav »
};

/// ROM DS des dossiers donnés (sous-dossiers compris) et de la racine de la carte SD.
std::vector<RomDs> chercherRomsDs(const std::vector<std::string>& dossiers);

/// Codes de jeu DS (3 lettres) d'une famille donnée par le site (« B2W2 » : Noir 2 et Blanc 2 ; « B2 » : Noir 2 seul).
std::vector<std::string> codesPourFamille(const std::string& famille);

/// Le fichier existe sur la carte SD.
bool fichierExiste(const std::string& chemin);

/// Écrit une sauvegarde récupérée à cet emplacement de la carte SD. S'il y en a déjà une, elle est d'abord copiée dans
/// sdmc:/3ds/PokeBankSync/copies/ (rien n'est écrit si la copie échoue) ; la nouvelle est écrite dans un fichier
/// temporaire, qui ne remplace l'ancienne qu'une fois complet. `message` : où est la copie, ou ce qui a échoué.
bool ecrireSauvegardeSD(const std::string& chemin, const std::vector<u8>& contenu, std::string& message);
