#pragma once

#include <3ds.h>
#include <string>
#include <vector>

// Appels à l'API de synchronisation de PokeBank (/api/sync/..., voir le README du site), en HTTPS par libcurl et
// mbedTLS. Le certificat du site est vérifié avec les racines de data/racines.pem (Let's Encrypt et ZeroSSL, utilisés
// par Caddy), plus celles d'un éventuel sdmc:/3ds/PokeBankSync/certificats.pem. Tous les messages sont prêts à afficher.

/// À appeler une fois au démarrage (sockets) ; faux avec un message si le réseau ne peut pas être préparé.
bool demarrerReseau(std::string& erreur);
void arreterReseau();

/// Le Wi-Fi est connecté.
bool wifiConnecte();

struct ResultatLiaison
{
    bool ok = false;
    std::string message;
    std::string jeton;
    std::string compte;
};

/// Échange le code affiché dans « Synchronisation 3DS » (site) contre un jeton longue durée.
ResultatLiaison relier(const std::string& site, const std::string& code, const std::string& nomConsole);

struct ResultatCompte
{
    bool ok = false;
    bool jetonRefuse = false; // console retirée du compte (ou jeton faux) : il faut la relier de nouveau
    std::string message;
    std::string compte;
};

/// Vérifie le jeton et donne le nom du compte.
ResultatCompte verifierCompte(const std::string& site, const std::string& jeton);

enum class StatutEnvoi
{
    Ajoutee,
    MiseAJour,
    DejaAJour,
    Refusee,     // règle du site (moins de temps de jeu, partie d'un autre compte...) : message à lire
    JetonRefuse, // console plus reliée
    Erreur,      // réseau, serveur
};

struct ResultatEnvoi
{
    StatutEnvoi statut = StatutEnvoi::Erreur;
    std::string message;
};

ResultatEnvoi envoyer(const std::string& site, const std::string& jeton, const std::string& fichier, const std::vector<u8>& contenu);
