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

/// Envoie une sauvegarde ; `cle` (emplacement sur la console) est notée par le site pour la récupération.
ResultatEnvoi envoyer(const std::string& site, const std::string& jeton, const std::string& fichier, const std::string& cle,
    const std::vector<u8>& contenu);

// --- Récupération ---------------------------------------------------------------------------------------------------

/// Sauvegarde stockée sur le site, telle que la liste « Récupérer » la montre.
struct SauvegardeSite
{
    int id = 0;
    std::string jeu;        // « Noire 2 »
    std::string dresseur;
    std::string idDresseur; // « 12345 / 54321 »
    std::string fichier;    // nom du fichier sur le site
    std::string empreinte;  // SHA-256 du fichier actuel du site (comme empreinte() de envois.hpp)
    int tempsDeJeu = 0;     // secondes
    bool modifieeSurSite = false; // retrait d'argent, transfert d'objets... que la console n'a peut-être pas
    std::string console;    // « 3ds », « ds », « gba », « gb », « autre »
    std::string famille;    // DS : « DP », « Pt », « HGSS », « B », « W », « BW », « B2 », « W2 », « B2W2 »
    std::vector<std::string> cles; // emplacements connus sur cette console (« sd:sdmc:/saves/x.sav »)
};

struct ResultatListe
{
    bool ok = false;
    bool jetonRefuse = false;
    std::string message;
    std::vector<SauvegardeSite> sauvegardes;
};

ResultatListe listerSauvegardesSite(const std::string& site, const std::string& jeton);

enum class Verdict
{
    Recuperable, // la console a moins de temps de jeu, ou le site a modifié la partie
    AJour,       // identique
    Refusee,     // autre partie, ou plus de temps de jeu sur la console : ne pas écraser
    Erreur,      // réseau, serveur
};

struct ResultatComparaison
{
    Verdict verdict = Verdict::Erreur;
    bool modifieeSurSite = false;
    bool jetonRefuse = false;
    std::string message;
};

/// Le site compare la sauvegarde de la console (`contenu`) à la sienne : peut-elle être remplacée ?
ResultatComparaison comparer(const std::string& site, const std::string& jeton, int id, const std::vector<u8>& contenu);

struct ResultatTelechargement
{
    bool ok = false;
    bool jetonRefuse = false;
    std::string message;
    std::vector<u8> contenu;
};

/// Fichier de la version du site ; `cle` : où la console va l'écrire (noté par le site).
ResultatTelechargement telecharger(const std::string& site, const std::string& jeton, int id, const std::string& cle);
