#pragma once

#include <3ds.h>
#include <map>
#include <string>
#include <vector>

/// Empreinte SHA-256 (hexadécimal) d'une sauvegarde.
std::string empreinte(const std::vector<u8>& contenu);

/// Empreinte et nom de fichier de la dernière version envoyée (et acceptée ou déjà à jour) de chaque sauvegarde, gardés dans
/// sdmc:/3ds/PokeBankSync/envois.txt : une sauvegarde identique est « déjà envoyée » et n'est pas cochée par défaut.
class Envois
{
public:
    void charger();
    bool enregistrer() const;
    /// Empreinte du dernier envoi, vide si la sauvegarde n'a jamais été envoyée.
    std::string dernier(const std::string& cle) const;
    void noter(const std::string& cle, const std::string& empreinte);
    /// Tout oublier (console reliée à un autre compte ou un autre site).
    void vider() { empreintes_.clear(); }

private:
    std::map<std::string, std::string> empreintes_; // clé de la sauvegarde → empreinte
};
