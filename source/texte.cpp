#include "texte.hpp"

#include <cstdint>

namespace {

struct Correspondance
{
    char32_t point;      // caractère Unicode
    const char* police;  // octets de la police de la console
    const char* simple;  // sans accent, en minuscules (simplifier)
};

// Lettres accentuées du français (et quelques autres) présentes dans la page de code 437 ; ponctuation remplacée.
const Correspondance TABLE[] = {
    {U'é', "\x82", "e"}, {U'è', "\x8A", "e"}, {U'ê', "\x88", "e"}, {U'ë', "\x89", "e"}, {U'É', "\x90", "e"},
    {U'È', "E", "e"},    {U'Ê', "E", "e"},    {U'Ë', "E", "e"},
    {U'à', "\x85", "a"}, {U'â', "\x83", "a"}, {U'ä', "\x84", "a"}, {U'á', "\xA0", "a"}, {U'å', "\x86", "a"},
    {U'À', "A", "a"},    {U'Â', "A", "a"},    {U'Ä', "\x8E", "a"}, {U'Å', "\x8F", "a"},
    {U'î', "\x8C", "i"}, {U'ï', "\x8B", "i"}, {U'í', "\xA1", "i"}, {U'ì', "\x8D", "i"}, {U'Î', "I", "i"}, {U'Ï', "I", "i"},
    {U'ô', "\x93", "o"}, {U'ö', "\x94", "o"}, {U'ó', "\xA2", "o"}, {U'ò', "\x95", "o"}, {U'Ô', "O", "o"}, {U'Ö', "\x99", "o"},
    {U'û', "\x96", "u"}, {U'ù', "\x97", "u"}, {U'ü', "\x81", "u"}, {U'ú', "\xA3", "u"}, {U'Û', "U", "u"}, {U'Ù', "U", "u"}, {U'Ü', "\x9A", "u"},
    {U'ç', "\x87", "c"}, {U'Ç', "\x80", "c"}, {U'ñ', "\xA4", "n"}, {U'Ñ', "\xA5", "n"}, {U'ÿ', "\x98", "y"}, {U'ß', "\xE1", "ss"},
    {U'œ', "oe", "oe"},  {U'Œ', "OE", "oe"},  {U'æ', "\x91", "ae"}, {U'Æ', "\x92", "ae"},
    {U'«', "\xAE", "\""}, {U'»', "\xAF", "\""}, {U'’', "'", "'"}, {U'‘', "'", "'"}, {U'“', "\"", "\""}, {U'”', "\"", "\""},
    {U'—', "-", "-"},    {U'–', "-", "-"},    {U'…', "...", "..."}, {U'·', "\xFA", "."}, {U'×', "x", "x"},
    {U'→', "->", "->"},  {U'₽', "P", "p"},    {U'°', "\xF8", ""},   {U' ', " ", " "}, {U' ', " ", " "},
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

std::string simplifier(const std::string& utf8)
{
    std::string sortie;
    for (size_t i = 0; i < utf8.size();)
    {
        char32_t c = lire(utf8, i);
        if (c < 0x80)
            sortie += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        else if (auto* entree = chercher(c))
            sortie += entree->simple;
    }
    return sortie;
}
