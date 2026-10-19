#pragma once

#include <string>
#include <vector>

/// Dossier du programme sur la carte SD : configuration, empreintes des envois, certificats en plus.
constexpr const char* DOSSIER_APPLI = "sdmc:/3ds/PokeBankSync";

/// Réglages gardés dans sdmc:/3ds/PokeBankSync/config.ini (modifiable depuis un PC) :
///   site=https://pokebank.exemple.fr
///   jeton=...                      (donné par le site à la liaison, ne pas le partager)
///   dossier=sdmc:/roms             (une ligne par dossier à parcourir, sous-dossiers compris)
struct Config
{
    std::string site;
    std::string jeton;
    std::vector<std::string> dossiers;

    bool relie() const { return !site.empty() && !jeton.empty(); }
};

/// Dossiers parcourus quand config.ini n'en donne aucun : TWiLight Menu++ (sdmc:/roms/nds/saves...), mGBA,
/// open_agb_firm.
const std::vector<std::string>& dossiersParDefaut();

/// Lit config.ini (réglages par défaut s'il n'existe pas encore).
Config chargerConfig();

/// Écrit config.ini ; faux si la carte SD refuse.
bool enregistrerConfig(const Config& config);

/// « https://site.fr/ » → « https://site.fr » ; ajoute https:// s'il manque.
std::string normaliserSite(std::string site);
