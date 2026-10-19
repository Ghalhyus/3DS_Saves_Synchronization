#include "envois.hpp"

#include "config.hpp"

#include <cstdio>
#include <mbedtls/sha256.h>
#include <sys/stat.h>

namespace {

const std::string FICHIER = std::string(DOSSIER_APPLI) + "/envois.txt";

} // namespace

std::string empreinte(const std::vector<u8>& contenu)
{
    unsigned char somme[32];
    mbedtls_sha256_ret(contenu.data(), contenu.size(), somme, 0);
    std::string hex;
    char octet[3];
    for (unsigned char c : somme)
    {
        snprintf(octet, sizeof octet, "%02X", c);
        hex += octet;
    }
    return hex;
}

void Envois::charger()
{
    empreintes_.clear();
    FILE* f = fopen(FICHIER.c_str(), "r");
    if (!f)
        return;
    char ligne[1024];
    while (fgets(ligne, sizeof ligne, f))
    {
        std::string texte = ligne;
        while (!texte.empty() && (texte.back() == '\n' || texte.back() == '\r'))
            texte.pop_back();
        auto tab = texte.find('\t');
        if (tab != std::string::npos)
            empreintes_[texte.substr(tab + 1)] = texte.substr(0, tab);
    }
    fclose(f);
}

bool Envois::enregistrer() const
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(DOSSIER_APPLI, 0777);
    FILE* f = fopen(FICHIER.c_str(), "w");
    if (!f)
        return false;
    for (const auto& [cle, valeur] : empreintes_)
        fprintf(f, "%s\t%s\n", valeur.c_str(), cle.c_str());
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

std::string Envois::dernier(const std::string& cle) const
{
    auto trouve = empreintes_.find(cle);
    return trouve == empreintes_.end() ? "" : trouve->second;
}

void Envois::noter(const std::string& cle, const std::string& valeur)
{
    empreintes_[cle] = valeur;
}
