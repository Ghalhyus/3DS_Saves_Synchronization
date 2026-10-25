#pragma once

#include <string>
#include <vector>

// La console de libctru dessine chaque octet avec sa police 8×8, qui est celle de la page de code 437 (IBM PC) :
// é = 0x82, à = 0x85, « = 0xAE... Tout le texte du programme et du site est en UTF-8 : il passe par versConsole avant
// d'être affiché. Les caractères absents de la police sont remplacés (œ → oe, ’ → ', ₽ → P, sinon ?).

/// Texte UTF-8 → octets de la police de la console (un octet = une case à l'écran).
std::string versConsole(const std::string& utf8);

/// Texte déjà converti, coupé en lignes d'au plus `largeur` cases, aux espaces quand c'est possible.
std::vector<std::string> couper(const std::string& console, int largeur);

/// Texte déjà converti, raccourci à `largeur` cases (« ... » à la fin) ou complété par des espaces.
std::string ajuster(const std::string& console, int largeur);

