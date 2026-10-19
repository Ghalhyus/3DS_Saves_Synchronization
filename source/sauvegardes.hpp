#pragma once

#include <3ds.h>
#include <string>
#include <vector>

enum class Origine
{
    Jeu3DS,           // X/Y à Ultra-Soleil/Ultra-Lune, installé ou sur cartouche : fichier « main » de sa sauvegarde
    ConsoleVirtuelle, // Rouge à Cristal sur l'eShop : fichier « sav.dat » de sa sauvegarde
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
/// connus, ou, pour la Console virtuelle d'autres régions, un nom en « Pokémon » avec une sauvegarde « sav.dat »).
void chercherJeux(std::vector<Sauvegarde>& trouvees);

/// Fichiers .sav des dossiers donnés (sous-dossiers compris). Un fichier est gardé si la ROM du même nom, trouvée dans
/// ces dossiers, est un jeu Pokémon principal (code du jeu dans son en-tête) ; sans ROM, si son nom contient « Pokémon ».
void chercherCarteSD(const std::vector<std::string>& dossiers, std::vector<Sauvegarde>& trouvees);

/// Contenu actuel de la sauvegarde ; faux avec un message si elle ne peut pas être lue.
bool lireSauvegarde(const Sauvegarde& sauvegarde, std::vector<u8>& contenu, std::string& erreur);

/// Plus grosse sauvegarde envoyée (même limite que le site).
constexpr size_t TAILLE_MAX = 4 * 1024 * 1024;
