#include "config.hpp"

#include <cstdio>
#include <sys/stat.h>

namespace {

const std::string FICHIER = std::string(DOSSIER_APPLI) + "/config.ini";

std::string sansEspaces(const std::string& s)
{
    auto debut = s.find_first_not_of(" \t\r\n");
    if (debut == std::string::npos)
        return "";
    auto fin = s.find_last_not_of(" \t\r\n");
    return s.substr(debut, fin - debut + 1);
}

} // namespace

const std::vector<std::string>& dossiersParDefaut()
{
    // TWiLight Menu++ : « saves » à côté des ROM (sdmc:/saves pour des ROM à la racine), ou son dossier sdmc:/roms.
    static const std::vector<std::string> dossiers = {
        "sdmc:/saves",
        "sdmc:/roms",
    };
    return dossiers;
}

namespace {

/// Dossiers par défaut de la première version, écrits tels quels dans config.ini à la liaison : ce n'est pas un choix de
/// l'utilisateur, ils sont remplacés par les dossiers par défaut actuels.
bool anciensDossiersParDefaut(const std::vector<std::string>& dossiers)
{
    return dossiers == std::vector<std::string>{"sdmc:/roms", "sdmc:/3ds/open_agb_firm", "sdmc:/3ds/mGBA"};
}

} // namespace

Config chargerConfig()
{
    Config config;
    if (FILE* f = fopen(FICHIER.c_str(), "r"))
    {
        char ligne[512];
        while (fgets(ligne, sizeof ligne, f))
        {
            std::string texte = sansEspaces(ligne);
            if (texte.empty() || texte[0] == '#' || texte[0] == ';')
                continue;
            auto egal = texte.find('=');
            if (egal == std::string::npos)
                continue;
            auto cle = sansEspaces(texte.substr(0, egal));
            auto valeur = sansEspaces(texte.substr(egal + 1));
            if (cle == "site")
                config.site = normaliserSite(valeur);
            else if (cle == "jeton")
                config.jeton = valeur;
            else if (cle == "dossier" && !valeur.empty())
                config.dossiers.push_back(valeur);
        }
        fclose(f);
    }
    config.dossiersChoisis = !config.dossiers.empty() && !anciensDossiersParDefaut(config.dossiers);
    if (!config.dossiersChoisis)
        config.dossiers = dossiersParDefaut();
    return config;
}

bool enregistrerConfig(const Config& config)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(DOSSIER_APPLI, 0777);
    FILE* f = fopen(FICHIER.c_str(), "w");
    if (!f)
        return false;
    fprintf(f, "# PokeBank Sync : adresse du site et jeton de cette console (donne par le site a la liaison,\n");
    fprintf(f, "# ne pas le partager). Relier de nouveau la console avec Y, ou la retirer depuis le site.\n");
    fprintf(f, "site=%s\n", config.site.c_str());
    fprintf(f, "jeton=%s\n", config.jeton.c_str());
    fprintf(f, "\n# Dossiers ou chercher les sauvegardes (.sav) des jeux DS, GBA et Game Boy, sous-dossiers compris.\n");
    if (config.dossiersChoisis)
        for (const auto& dossier : config.dossiers)
            fprintf(f, "dossier=%s\n", dossier.c_str());
    else
    {
        fprintf(f, "# Sans ligne dossier= (enlever le # pour en choisir), ceux-ci :\n");
        for (const auto& dossier : dossiersParDefaut())
            fprintf(f, "#dossier=%s\n", dossier.c_str());
    }
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

std::string normaliserSite(std::string site)
{
    site = sansEspaces(site);
    while (!site.empty() && site.back() == '/')
        site.pop_back();
    if (!site.empty() && site.find("://") == std::string::npos)
        site = "https://" + site;
    return site;
}
