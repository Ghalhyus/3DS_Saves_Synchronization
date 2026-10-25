#include "texte.hpp"

#include <cstdint>

namespace {

struct Correspondance
{
    char32_t point;      // caractère Unicode
    const char* police;  // octets de la police de la console
};

// Lettres accentuées du français (et quelques autres) présentes dans la page de code 437 ; ponctuation remplacée.
const Correspondance TABLE[] = {
    {U'é', "\x82"}, {U'è', "\x8A"}, {U'ê', "\x88"}, {U'ë', "\x89"}, {U'É', "\x90"},
    {U'È', "E"},    {U'Ê', "E"},    {U'Ë', "E"},
    {U'à', "\x85"}, {U'â', "\x83"}, {U'ä', "\x84"}, {U'á', "\xA0"}, {U'å', "\x86"},
    {U'À', "A"},    {U'Â', "A"},    {U'Ä', "\x8E"}, {U'Å', "\x8F"},
    {U'î', "\x8C"}, {U'ï', "\x8B"}, {U'í', "\xA1"}, {U'ì', "\x8D"}, {U'Î', "I"}, {U'Ï', "I"},
    {U'ô', "\x93"}, {U'ö', "\x94"}, {U'ó', "\xA2"}, {U'ò', "\x95"}, {U'Ô', "O"}, {U'Ö', "\x99"},
    {U'û', "\x96"}, {U'ù', "\x97"}, {U'ü', "\x81"}, {U'ú', "\xA3"}, {U'Û', "U"}, {U'Ù', "U"}, {U'Ü', "\x9A"},
    {U'ç', "\x87"}, {U'Ç', "\x80"}, {U'ñ', "\xA4"}, {U'Ñ', "\xA5"}, {U'ÿ', "\x98"}, {U'ß', "\xE1"},
    {U'œ', "oe"},  {U'Œ', "OE"},  {U'æ', "\x91"}, {U'Æ', "\x92"},
    {U'«', "\xAE"}, {U'»', "\xAF"}, {U'’', "'"}, {U'‘', "'"}, {U'“', "\""}, {U'”', "\""},
    {U'—', "-"},    {U'–', "-"},    {U'…', "..."}, {U'·', "\xFA"}, {U'×', "x"},
    {U'→', "->"},  {U'₽', "P"},    {U'°', "\xF8"},   {U' ', " "}, {U' ', " "},
};

const Correspondance* chercher(char32_t c)
{
    for (const auto& entree : TABLE)
        if (entree.point == c)
            return &entree;
    return nullptr;
}

/// Lit un caractère UTF-8 à partir de i (avance i) ; un octet invalide vaut U+FFFD.
char32_t lire(const std::string& s, size_t& i)
{
    auto octet = static_cast<uint8_t>(s[i++]);
    if (octet < 0x80)
        return octet;
    int suite = octet >= 0xF0 ? 3 : octet >= 0xE0 ? 2 : octet >= 0xC0 ? 1 : -1;
    if (suite < 0)
        return U'�';
    char32_t c = octet & (0x3F >> suite);
    for (int k = 0; k < suite; k++)
    {
        if (i >= s.size() || (static_cast<uint8_t>(s[i]) & 0xC0) != 0x80)
            return U'�';
        c = (c << 6) | (static_cast<uint8_t>(s[i++]) & 0x3F);
    }
    return c;
}

} // namespace

std::string versConsole(const std::string& utf8)
{
    std::string sortie;
    for (size_t i = 0; i < utf8.size();)
    {
        char32_t c = lire(utf8, i);
        if (c == U'\n' || (c >= 0x20 && c < 0x7F))
            sortie += static_cast<char>(c);
        else if (auto* entree = chercher(c))
            sortie += entree->police;
        else if (c >= 0x20)
            sortie += '?';
    }
    return sortie;
}

std::vector<std::string> couper(const std::string& console, int largeur)
{
    std::vector<std::string> lignes;
    size_t debut = 0;
    while (debut <= console.size())
    {
        auto finLigne = console.find('\n', debut);
        auto paragraphe = console.substr(debut, finLigne == std::string::npos ? std::string::npos : finLigne - debut);
        if (paragraphe.empty())
            lignes.emplace_back();
        while (!paragraphe.empty())
        {
            if (static_cast<int>(paragraphe.size()) <= largeur)
            {
                lignes.push_back(paragraphe);
                break;
            }
            auto coupure = paragraphe.rfind(' ', largeur);
            if (coupure == std::string::npos || coupure == 0)
                coupure = largeur;
            lignes.push_back(paragraphe.substr(0, coupure));
            paragraphe = paragraphe.substr(coupure);
            if (!paragraphe.empty() && paragraphe[0] == ' ')
                paragraphe.erase(0, 1);
        }
        if (finLigne == std::string::npos)
            break;
        debut = finLigne + 1;
    }
    return lignes;
}

std::string ajuster(const std::string& console, int largeur)
{
    if (static_cast<int>(console.size()) <= largeur)
        return console + std::string(largeur - console.size(), ' ');
    return console.substr(0, largeur - 3) + "...";
}
