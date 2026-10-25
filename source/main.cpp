// PokeBank Sync : cherche les sauvegardes Pokémon de la 3DS (jeux 3DS, Console virtuelle, fichiers .sav de la carte
// SD) et les envoie au site PokeBank, où elles deviennent des sauvegardes stockées (ajout ou mise à jour). Dans l'autre
// sens (« Récupérer », touche B), remet sur la console la version du site (sauvegardes DS et Game Boy pour l'instant).
//
// Écran du haut : la liste, à cocher. Écran du bas : le détail de la ligne choisie, l'état et les touches.

#include "config.hpp"
#include "envois.hpp"
#include "sauvegardes.hpp"
#include "site.hpp"
#include "texte.hpp"

#include <3ds.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr int LARGEUR_HAUT = 50, LARGEUR_BAS = 40, HAUTEUR = 30;
constexpr int LIGNES_LISTE = 24;                                  // lignes 3 à 26 de l'écran du haut
constexpr int LARGEUR_ETAT = 11;
constexpr int LARGEUR_NOM = LARGEUR_HAUT - 2 - 4 - 1 - LARGEUR_ETAT; // « > [x] nom état »

const char* const NORMAL = "\x1b[0m";
const char* const VERT = "\x1b[32m";
const char* const JAUNE = "\x1b[33m";
const char* const ROUGE = "\x1b[31m";
const char* const CYAN = "\x1b[36m";
const char* const GRIS = "\x1b[37m";
const char* const INVERSE = "\x1b[47;30m";

/// Une sauvegarde trouvée, avec ce qu'on sait de son dernier envoi.
struct Ligne
{
    Sauvegarde sauvegarde;
    std::string empreinte; // vide si illisible
    bool cochee = false;
    std::string etat;      // court, en UTF-8 : « nouvelle », « modifiée », « ajoutée »...
    const char* couleur = NORMAL;
    std::string message;   // dernier message (lecture, réponse du site)
};

/// Une sauvegarde du site (« Récupérer »), et où la console la mettrait.
struct LigneSite
{
    SauvegardeSite site;
    Sauvegarde cible;        // où la mettre : fichier de la carte SD (DS) ou sauvegarde d'un jeu (GB) ; cle vide : nulle part
    bool remplace = false;   // la même partie y est déjà (copiée avant d'être remplacée)
    bool possible = false;   // peut être cochée
    bool cochee = false;
    std::string etat;
    const char* couleur = NORMAL;
    std::string message;
};

enum class Mode
{
    Envoi,        // sauvegardes de la console → site
    Recuperation, // sauvegardes du site → console
};

PrintConsole haut, bas;
Mode mode = Mode::Envoi;
std::vector<LigneSite> lignesSite;
int choixSite = 0, debutSite = 0;
bool rechercherAuRetour = false; // une sauvegarde a été écrite : la liste d'envoi est à refaire
Config config;
Envois envois;
std::vector<Ligne> lignes;
int choix = 0, debutListe = 0;
std::string compte;               // compte du site, vide si pas (ou plus) relié
std::string statut;               // message général, écran du bas
const char* couleurStatut = NORMAL;
bool reseauPret = false;

void afficherMaintenant()
{
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

void ecrireA(int ligne, int colonne, const std::string& console, const char* couleur = NORMAL)
{
    printf("\x1b[%d;%dH%s%s%s", ligne + 1, colonne + 1, couleur, console.c_str(), NORMAL);
}

void definirStatut(const std::string& texte, const char* couleur = NORMAL)
{
    statut = texte;
    couleurStatut = couleur;
}

/// Ce qui est retenu du dernier envoi : l'empreinte du contenu et le nom du fichier. Un nom qui change (version exacte
/// du jeu enfin reconnue) rend la ligne « modifiée » : le site renomme alors la sauvegarde stockée.
std::string trace(const Ligne& ligne)
{
    return ligne.empreinte + "|" + ligne.sauvegarde.fichier;
}

/// État d'une ligne d'après son dernier envoi.
void etatSelonEnvois(Ligne& ligne)
{
    if (ligne.empreinte.empty())
    {
        ligne.etat = "illisible";
        ligne.couleur = ROUGE;
        ligne.cochee = false;
        return;
    }
    auto dernier = envois.dernier(ligne.sauvegarde.cle);
    if (dernier.empty())
        ligne.etat = "nouvelle", ligne.couleur = CYAN;
    else if (dernier != trace(ligne))
        ligne.etat = "modifiée", ligne.couleur = JAUNE;
    else
        ligne.etat = "envoyée", ligne.couleur = GRIS;
    ligne.cochee = dernier != trace(ligne);
}

/// Ce que la liste montre d'une ligne, dans l'un ou l'autre mode.
struct Affichage
{
    std::string nom;
    std::string etat;
    const char* couleur;
    bool cochee;
    std::vector<std::pair<std::string, const char*>> details; // écran du bas ; le dernier est le message
};

std::string tempsDeJeu(int secondes)
{
    char texte[24];
    snprintf(texte, sizeof texte, "%d h %02d", secondes / 3600, secondes / 60 % 60);
    return texte;
}

/// Où est (ou sera) une sauvegarde sur la console, pour l'afficher.
std::string lieu(const Sauvegarde& cible)
{
    return cible.origine == Origine::CarteSD ? cible.chemin : cible.nom + " (" + cible.detail + ")";
}

int nombreDeLignes()
{
    return static_cast<int>(mode == Mode::Envoi ? lignes.size() : lignesSite.size());
}

int& choixCourant() { return mode == Mode::Envoi ? choix : choixSite; }
int& debutCourant() { return mode == Mode::Envoi ? debutListe : debutSite; }

Affichage affichage(int n)
{
    if (mode == Mode::Envoi)
    {
        const auto& ligne = lignes[n];
        return {ligne.sauvegarde.nom, ligne.etat, ligne.couleur, ligne.cochee,
            {{ligne.sauvegarde.nom, CYAN}, {ligne.sauvegarde.detail, GRIS}, {ligne.message, ligne.couleur}}};
    }
    const auto& ligne = lignesSite[n];
    auto titre = ligne.site.jeu + " — " + ligne.site.dresseur;
    auto destination = ligne.cible.cle.empty() ? std::string()
        : (ligne.remplace ? "Remplace " : "Sera écrite dans ") + lieu(ligne.cible);
    return {titre, ligne.etat, ligne.couleur, ligne.cochee,
        {{titre + " (" + ligne.site.idDresseur + ")", CYAN},
            {ligne.site.fichier + " · " + tempsDeJeu(ligne.site.tempsDeJeu) + " de jeu sur le site", GRIS},
            {destination, GRIS},
            {ligne.message, ligne.couleur}}};
}

void dessiner()
{
    bool envoi = mode == Mode::Envoi;
    int nombre = nombreDeLignes();
    int choixActuel = choixCourant(), debut = debutCourant();

    // --- Écran du haut : la liste ---
    consoleSelect(&haut);
    consoleClear();
    ecrireA(0, 0, versConsole(envoi ? "PokeBank Sync" : "PokeBank Sync : récupérer"), CYAN);
    std::string relie = compte.empty() ? versConsole("pas relié (touche Y)") : versConsole("compte : " + compte);
    if (static_cast<int>(relie.size()) > LARGEUR_HAUT - 27)
        relie = ajuster(relie, LARGEUR_HAUT - 27);
    ecrireA(0, LARGEUR_HAUT - static_cast<int>(relie.size()), relie, compte.empty() ? JAUNE : VERT);
    ecrireA(1, 0, ajuster(versConsole(config.site.empty() ? "aucun site" : config.site), LARGEUR_HAUT), GRIS);
    ecrireA(2, 0, std::string(LARGEUR_HAUT, '\xC4'));

    if (nombre == 0)
        ecrireA(4, 1, versConsole(envoi ? "Aucune sauvegarde Pokémon trouvée." : "Aucune sauvegarde du site à afficher."), JAUNE);
    for (int i = 0; i < LIGNES_LISTE && debut + i < nombre; i++)
    {
        int n = debut + i;
        auto ligne = affichage(n);
        std::string tete = std::string(n == choixActuel ? ">" : " ") + (ligne.cochee ? "[x] " : "[ ] ");
        std::string nom = ajuster(versConsole(ligne.nom), LARGEUR_NOM);
        ecrireA(3 + i, 0, tete + nom + " ", n == choixActuel ? INVERSE : NORMAL);
        ecrireA(3 + i, LARGEUR_HAUT - LARGEUR_ETAT, ajuster(versConsole(ligne.etat), LARGEUR_ETAT), ligne.couleur);
    }
    if (debut > 0)
        ecrireA(3, LARGEUR_HAUT - 1, "\x1E", GRIS); // il y en a au-dessus
    if (debut + LIGNES_LISTE < nombre)
        ecrireA(3 + LIGNES_LISTE - 1, LARGEUR_HAUT - 1, "\x1F", GRIS); // et en dessous

    int cochees = 0;
    for (int n = 0; n < nombre; n++)
        cochees += affichage(n).cochee;
    char resume[80];
    snprintf(resume, sizeof resume, envoi ? "%d sauvegarde(s), %d cochée(s)" : "%d sauvegarde(s) sur le site, %d cochée(s)",
        nombre, cochees);
    ecrireA(HAUTEUR - 2, 0, versConsole(resume), GRIS);

    // --- Écran du bas : détail, état, touches ---
    consoleSelect(&bas);
    consoleClear();
    int y = 0;
    if (choixActuel < nombre)
    {
        auto ligne = affichage(choixActuel);
        for (size_t i = 0; i < ligne.details.size(); i++)
        {
            const auto& [texte, couleur] = ligne.details[i];
            if (texte.empty())
                continue;
            bool message = i + 1 == ligne.details.size();
            if (message)
                y++;
            for (const auto& l : couper(versConsole(texte), LARGEUR_BAS))
                if (y < (message ? 15 : 9))
                    ecrireA(y++, 0, l, couleur);
        }
    }

    y = 16;
    ecrireA(y++, 0, std::string(LARGEUR_BAS, '\xC4'));
    for (const auto& l : couper(versConsole(statut), LARGEUR_BAS))
        if (y < 24)
            ecrireA(y++, 0, l, couleurStatut);

    ecrireA(25, 0, versConsole("A cocher   L/R tout décocher/cocher"), GRIS);
    ecrireA(26, 0, versConsole(envoi ? "X envoyer   B récupérer depuis le site" : "X récupérer les sauvegardes cochées"), GRIS);
    ecrireA(27, 0, versConsole(envoi ? "Y relier au site   SELECT rechercher" : "B retour à l'envoi   SELECT relire"), GRIS);
    ecrireA(28, 0, versConsole("START quitter"), GRIS);
    afficherMaintenant();
}

/// Montre tout de suite un message (avant une opération qui bloque l'écran).
void patienter(const std::string& texte)
{
    definirStatut(texte, CYAN);
    dessiner();
}

void rechercher()
{
    patienter("Recherche des sauvegardes Pokémon...");
    std::vector<Sauvegarde> trouvees;
    chercherJeux(trouvees);
    chercherCarteSD(config.dossiers, trouvees);

    lignes.clear();
    for (auto& sauvegarde : trouvees)
    {
        Ligne ligne;
        ligne.sauvegarde = sauvegarde;
        std::vector<u8> contenu;
        std::string erreur;
        if (lireSauvegarde(sauvegarde, contenu, erreur))
            ligne.empreinte = empreinte(contenu);
        else
            ligne.message = erreur;
        etatSelonEnvois(ligne);
        lignes.push_back(ligne);
    }
    choix = debutListe = 0;
    definirStatut(lignes.empty()
        ? "Aucune sauvegarde trouvée. Les dossiers parcourus sur la carte SD se règlent dans sdmc:/3ds/PokeBankSync/config.ini."
        : "Coche les sauvegardes à envoyer, puis appuie sur X.");
}

void verifier()
{
    if (!config.relie())
    {
        compte.clear();
        definirStatut("Pas encore relié au site : appuie sur Y.", JAUNE);
        return;
    }
    if (!wifiConnecte())
    {
        definirStatut("Pas de connexion Wi-Fi : active-la pour envoyer.", JAUNE);
        return;
    }
    patienter("Connexion au site...");
    auto resultat = verifierCompte(config.site, config.jeton);
    if (resultat.ok)
    {
        compte = resultat.compte;
        definirStatut("Coche les sauvegardes à envoyer, puis appuie sur X.");
    }
    else
    {
        if (resultat.jetonRefuse)
            compte.clear();
        definirStatut(resultat.message, ROUGE);
    }
}

bool saisir(const char* indice, const std::string& initial, int longueurMax, std::string& sortie)
{
    SwkbdState clavier;
    swkbdInit(&clavier, SWKBD_TYPE_WESTERN, 2, longueurMax);
    swkbdSetHintText(&clavier, indice);
    swkbdSetInitialText(&clavier, initial.c_str());
    swkbdSetButton(&clavier, SWKBD_BUTTON_LEFT, "Annuler", false);
    swkbdSetButton(&clavier, SWKBD_BUTTON_RIGHT, "OK", true);
    swkbdSetValidation(&clavier, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    std::vector<char> tampon(longueurMax * 4 + 1, 0);
    if (swkbdInputText(&clavier, tampon.data(), tampon.size()) != SWKBD_BUTTON_RIGHT)
        return false;
    sortie = tampon.data();
    return true;
}

/// Nom d'utilisateur de la console (Paramètres de la console), envoyé au site pour la reconnaître dans sa liste.
std::string nomDeLaConsole()
{
    u16 nom[0x1C / 2 + 1] = {};
    if (R_SUCCEEDED(CFGU_GetConfigInfoBlk2(0x1C, 0x000A0000, nom)))
    {
        u8 utf8[64] = {};
        utf16_to_utf8(utf8, nom, sizeof utf8 - 1);
        if (utf8[0] != 0)
            return "3DS de " + std::string(reinterpret_cast<char*>(utf8));
    }
    return "3DS";
}

void relierAuSite()
{
    if (!reseauPret || !wifiConnecte())
    {
        definirStatut("Pas de connexion Wi-Fi : active-la pour relier la console.", ROUGE);
        return;
    }
    std::string site;
    if (!saisir("Adresse du site PokeBank", config.site.empty() ? "https://" : config.site, 120, site))
        return;
    site = normaliserSite(site);
    std::string code;
    if (!saisir("Code de « Synchronisation 3DS » (ABCD-EFGH)", "", 12, code))
        return;

    patienter("Liaison au site...");
    auto resultat = relier(site, code, nomDeLaConsole());
    if (!resultat.ok)
    {
        definirStatut(resultat.message, ROUGE);
        return;
    }
    if (site != config.site || resultat.compte != compte)
    {
        envois.vider(); // autre site ou autre compte : rien n'y a encore été envoyé
        envois.enregistrer();
    }
    config.site = site;
    config.jeton = resultat.jeton;
    compte = resultat.compte;
    for (auto& ligne : lignes)
        if (!ligne.empreinte.empty())
            etatSelonEnvois(ligne);
    if (enregistrerConfig(config))
        definirStatut(resultat.message + " Coche les sauvegardes à envoyer, puis appuie sur X.", VERT);
    else
        definirStatut("Reliée, mais config.ini n'a pas pu être écrit sur la carte SD : il faudra la relier au prochain lancement.", JAUNE);
}

void envoyerCochees()
{
    if (!config.relie() || compte.empty())
    {
        definirStatut("Relie d'abord la console au site (touche Y).", JAUNE);
        return;
    }
    if (!wifiConnecte())
    {
        definirStatut("Pas de connexion Wi-Fi : active-la pour envoyer.", ROUGE);
        return;
    }
    int total = std::count_if(lignes.begin(), lignes.end(), [](const Ligne& l) { return l.cochee; });
    if (total == 0)
    {
        definirStatut("Aucune sauvegarde cochée (touche A).", JAUNE);
        return;
    }

    int numero = 0, reussies = 0, refusees = 0, erreurs = 0;
    bool jetonRefuse = false;
    std::string messageJeton;
    for (auto& ligne : lignes)
    {
        if (!ligne.cochee)
            continue;
        numero++;
        char progression[32];
        snprintf(progression, sizeof progression, "Envoi %d/%d : ", numero, total);
        patienter(progression + ligne.sauvegarde.nom + "...");

        std::vector<u8> contenu;
        std::string erreur;
        if (!lireSauvegarde(ligne.sauvegarde, contenu, erreur))
        {
            ligne.etat = "illisible", ligne.couleur = ROUGE, ligne.message = erreur;
            erreurs++;
            continue;
        }
        auto resultat = envoyer(config.site, config.jeton, ligne.sauvegarde.fichier, ligne.sauvegarde.cle, contenu);
        ligne.message = resultat.message;
        switch (resultat.statut)
        {
        case StatutEnvoi::Ajoutee:
        case StatutEnvoi::MiseAJour:
        case StatutEnvoi::DejaAJour:
            ligne.etat = resultat.statut == StatutEnvoi::Ajoutee ? "ajoutée"
                : resultat.statut == StatutEnvoi::MiseAJour ? "mise à jour" : "à jour";
            ligne.couleur = VERT;
            ligne.cochee = false;
            ligne.empreinte = empreinte(contenu);
            envois.noter(ligne.sauvegarde.cle, trace(ligne));
            reussies++;
            break;
        case StatutEnvoi::Refusee:
            ligne.etat = "refusée", ligne.couleur = ROUGE;
            refusees++;
            break;
        case StatutEnvoi::JetonRefuse:
            jetonRefuse = true;
            messageJeton = resultat.message;
            ligne.message.clear();
            break;
        case StatutEnvoi::Erreur:
            ligne.etat = "erreur", ligne.couleur = ROUGE;
            erreurs++;
            break;
        }
        if (jetonRefuse)
            break;
    }
    envois.enregistrer();

    if (jetonRefuse)
    {
        compte.clear();
        definirStatut(messageJeton, ROUGE);
        return;
    }
    char resume[160];
    snprintf(resume, sizeof resume, "%d envoyée(s) ou déjà à jour, %d refusée(s), %d erreur(s).%s", reussies, refusees, erreurs,
        refusees + erreurs > 0 ? " Choisis une ligne pour lire le message du site." : "");
    definirStatut(resume, refusees + erreurs > 0 ? JAUNE : VERT);
}

std::string nomDeFichier(const std::string& chemin)
{
    return chemin.substr(chemin.rfind('/') + 1);
}

/// Sauvegarde DS à cet emplacement de la carte SD (clé et nom de fichier comme à l'envoi).
Sauvegarde cibleSD(const std::string& chemin)
{
    Sauvegarde cible;
    cible.origine = Origine::CarteSD;
    cible.chemin = chemin;
    cible.cle = "sd:" + chemin;
    cible.fichier = nomDeFichier(chemin);
    return cible;
}

std::string nomDeConsole(const std::string& console)
{
    if (console == "3ds")
        return "3DS";
    if (console == "gba")
        return "GBA";
    if (console == "gb")
        return "Game Boy";
    return "d'autres consoles";
}

/// Où mettre une sauvegarde DS du site sur cette console : son emplacement connu (envoi ou récupération précédente),
/// sinon une sauvegarde de la carte SD du même nom, sinon à côté d'une ROM du même jeu qui n'a pas encore de sauvegarde
/// (de préférence celle du même nom). Vide si nulle part. `prises` : clés déjà données à d'autres lignes.
std::string destinationDs(const SauvegardeSite& site, const std::vector<RomDs>& roms, const std::set<std::string>& prises)
{
    for (const auto& cle : site.cles)
        if (cle.rfind("sd:", 0) == 0 && fichierExiste(cle.substr(3)) && !prises.count(cle))
            return cle.substr(3);
    for (const auto& ligne : lignes)
        if (ligne.sauvegarde.origine == Origine::CarteSD && ligne.sauvegarde.fichier == site.fichier
            && !prises.count(ligne.sauvegarde.cle))
            return ligne.sauvegarde.chemin;

    auto codes = codesPourFamille(site.famille);
    const RomDs* choisie = nullptr;
    for (const auto& rom : roms)
    {
        if (std::find(codes.begin(), codes.end(), rom.code) == codes.end() || fichierExiste(rom.sauvegarde)
            || prises.count("sd:" + rom.sauvegarde))
            continue;
        if (!choisie || nomDeFichier(rom.sauvegarde) == site.fichier)
            choisie = &rom;
    }
    return choisie ? choisie->sauvegarde : "";
}

/// Où mettre une sauvegarde Game Boy du site : le jeu où elle a déjà été envoyée ou récupérée (emplacement connu), sinon
/// un jeu compatible (même jeu, même région) qui contient la même partie (le site compare les dresseurs), sinon un jeu
/// compatible lancé mais sans partie Pokémon. Jamais par-dessus une autre partie. Faux si la console n'est plus reliée.
/// Jeu de la Console virtuelle d'où la sauvegarde du site a été envoyée (ou où elle a été récupérée) sur cette
/// console : son identifiant de titre, 0 si aucun.
u64 titreDOrigine(const SauvegardeSite& site)
{
    for (const auto& cle : site.cles)
        if (cle.rfind("titre:", 0) == 0)
            return std::strtoull(cle.substr(6, 16).c_str(), nullptr, 16);
    return 0;
}

/// Version exacte d'une sauvegarde Game Boy du site (« rouge », « bleu »...) : le site ne la connaît pas (Rouge et Bleu ont
/// le même format), mais le nom du fichier la dit (« Pokémon Rouge.sav », donné par la console à l'envoi depuis un jeu de la
/// Console virtuelle ; nom de l'émulateur sinon), ou à défaut le jeu noté par le site. Le nom d'abord : un jeu noté peut
/// venir d'une récupération dans le mauvais jeu. Vide si inconnue.
std::string versionGbDuSite(const SauvegardeSite& site)
{
    if (auto version = versionGbDansTexte(site.fichier); !version.empty())
        return version;
    u64 titre = titreDOrigine(site);
    return titre ? versionDuTitre(titre) : "";
}

bool placerGb(LigneSite& ligne, const std::vector<JeuGb>& jeux, std::set<std::string>& prises)
{
    std::vector<const JeuGb*> compatibles;
    for (const auto& jeu : jeux)
        if (familleGbCompatible(ligne.site.famille, jeu.famille) && !prises.count(jeu.cible.cle))
            compatibles.push_back(&jeu);

    // Les jeux notés par le site (envoi ou récupération) d'abord, mais vérifiés comme les autres : une récupération dans le
    // mauvais jeu, corrigée à la main depuis, y a laissé une autre partie.
    auto note = [&](const JeuGb* jeu) {
        return std::find(ligne.site.cles.begin(), ligne.site.cles.end(), jeu->cible.cle) != ligne.site.cles.end();
    };
    std::stable_partition(compatibles.begin(), compatibles.end(), note);

    const JeuGb* choisi = nullptr;
    for (const auto* jeu : compatibles)
    {
        if (choisi)
            break;
        if (!jeu->partie)
            continue;
        std::vector<u8> contenu;
        std::string erreur;
        if (!lireSauvegarde(jeu->cible, contenu, erreur))
            continue;
        if (empreinte(contenu) == ligne.site.empreinte)
        {
            choisi = jeu;
            break;
        }
        auto avis = comparer(config.site, config.jeton, ligne.site.id, contenu);
        if (avis.jetonRefuse)
        {
            compte.clear();
            definirStatut(avis.message, ROUGE);
            return false;
        }
        if (avis.verdict != Verdict::Erreur && avis.raison != "autre-partie")
            choisi = jeu; // la même partie (à jour, récupérable, ou plus avancée sur la console)
    }

    if (choisi)
    {
        ligne.cible = choisi->cible;
        ligne.remplace = true;
        ligne.possible = true;
        prises.insert(ligne.cible.cle);
        return true;
    }
    // Placement dans un jeu sans partie : la langue doit être la même (la sauvegarde contient des adresses propres à la
    // ROM de sa langue). Connue si la sauvegarde a été envoyée depuis un jeu de la Console virtuelle de cette console.
    // La version aussi (Rouge dans Rouge, Bleu dans Bleu), quand elle est connue : par le jeu d'origine ou le nom du fichier.
    u64 origine = titreDOrigine(ligne.site);
    std::string langue = origine ? langueDuTitre(origine) : "";
    std::string version = versionGbDuSite(ligne.site);
    const JeuGb* place = nullptr;
    bool autreLangue = false, autreVersion = false;
    for (const auto* jeu : compatibles)
    {
        if (jeu->partie)
            continue;
        if (!langue.empty() && !jeu->langue.empty() && langue != jeu->langue)
        {
            autreLangue = true;
            continue;
        }
        if (!version.empty() && !jeu->version.empty() && version != jeu->version)
        {
            autreVersion = true;
            continue;
        }
        // Le jeu d'origine d'abord, puis la même langue, puis la même version.
        auto rang = [&](const JeuGb* j) {
            return (j->cible.titre == origine ? 4 : 0) + (!langue.empty() && j->langue == langue ? 2 : 0)
                + (!version.empty() && j->version == version ? 1 : 0);
        };
        if (!place || rang(jeu) > rang(place))
            place = jeu;
    }
    if (place)
    {
        ligne.cible = place->cible;
        ligne.possible = true;
        prises.insert(ligne.cible.cle);
        ligne.etat = "à placer", ligne.couleur = CYAN;
        ligne.message = "Pas encore sur la console : ce jeu a été lancé mais n'a pas de partie. Coche-la pour l'y mettre.";
        if (langue.empty() || place->langue.empty())
            ligne.message += " Attention, langue de la sauvegarde inconnue (envoyée depuis un émulateur ?) : si ce n'est pas "
                             "celle du jeu" + (place->langue.empty() ? std::string() : " (" + place->langue + ")")
                + ", la partie sera inutilisable.";
        if (version.empty())
            ligne.message += " Version inconnue (ni le jeu d'origine ni le nom du fichier ne la disent) : elle va dans "
                + place->cible.nom + ".";
        return true;
    }
    if (autreVersion && !autreLangue)
    {
        ligne.etat = "pas de place", ligne.couleur = GRIS;
        ligne.message = "Aucun jeu Pokémon " + version + " sans partie sur la console (les autres jeux compatibles ont déjà "
                        "une partie, ou sont d'une autre version).";
    }
    else if (autreLangue)
    {
        ligne.etat = "autre lang.", ligne.couleur = GRIS; // 11 cases (LARGEUR_ETAT)
        ligne.message = "Cette sauvegarde vient d'un jeu en " + langue + " ; les jeux compatibles sans partie de la console "
                        "sont dans une autre langue, où elle serait inutilisable.";
    }
    else if (compatibles.empty())
    {
        ligne.etat = "pas de jeu", ligne.couleur = GRIS;
        ligne.message = "Aucun jeu Game Boy compatible (même jeu, même région) lancé au moins une fois sur la console : "
                        "installe-le et lance-le une fois, puis relis la liste (SELECT).";
    }
    else
    {
        ligne.etat = "pas de place", ligne.couleur = GRIS;
        ligne.message = "Les jeux compatibles de la console ont déjà une autre partie : elle ne sera pas écrasée.";
    }
    return true;
}

/// Liste « Récupérer » : sauvegardes du site, destination de chacune sur la console, et avis du site pour celles qui
/// remplaceraient une sauvegarde déjà là (même partie, moins de temps de jeu ou modifiée par le site).
void preparerRecuperation()
{
    lignesSite.clear();
    choixSite = debutSite = 0;
    if (!config.relie() || compte.empty())
    {
        definirStatut("Relie d'abord la console au site (touche Y).", JAUNE);
        return;
    }
    if (!wifiConnecte())
    {
        definirStatut("Pas de connexion Wi-Fi : active-la pour récupérer.", ROUGE);
        return;
    }
    patienter("Liste des sauvegardes du site...");
    auto liste = listerSauvegardesSite(config.site, config.jeton);
    if (!liste.ok)
    {
        if (liste.jetonRefuse)
            compte.clear();
        definirStatut(liste.message, ROUGE);
        return;
    }

    patienter("Recherche des ROM DS et des jeux Game Boy...");
    auto roms = chercherRomsDs(config.dossiers);
    auto jeuxGb = chercherJeuxGb();
    std::set<std::string> prises; // clés déjà données à une ligne
    for (const auto& site : liste.sauvegardes)
    {
        LigneSite ligne;
        ligne.site = site;
        if (site.console == "gb")
        {
            // placées ensuite : celles dont la version est connue d'abord
        }
        else if (site.console != "ds")
        {
            ligne.etat = "plus tard", ligne.couleur = GRIS;
            ligne.message = "La récupération des sauvegardes " + nomDeConsole(site.console)
                + " viendra plus tard : pour l'instant, seulement les sauvegardes DS et Game Boy.";
        }
        else if (auto chemin = destinationDs(site, roms, prises); chemin.empty())
        {
            ligne.etat = "pas de ROM", ligne.couleur = GRIS;
            ligne.message = "Aucune ROM de ce jeu sans sauvegarde sur la carte SD : ajoute-la (à la racine pour "
                            "TWiLight Menu++), puis relis la liste (SELECT).";
        }
        else
        {
            ligne.cible = cibleSD(chemin);
            prises.insert(ligne.cible.cle);
            ligne.remplace = fichierExiste(chemin);
            ligne.possible = true;
            if (!ligne.remplace)
                ligne.etat = "à placer", ligne.couleur = CYAN,
                ligne.message = "Pas encore sur la console : coche-la pour l'y mettre.";
        }
        lignesSite.push_back(ligne);
    }
    for (bool versionConnue : {true, false})
        for (auto& ligne : lignesSite)
        {
            if (ligne.site.console != "gb" || !versionGbDuSite(ligne.site).empty() != versionConnue)
                continue;
            patienter("Recherche de la place de " + ligne.site.jeu + " — " + ligne.site.dresseur + "...");
            if (!placerGb(ligne, jeuxGb, prises))
            {
                lignesSite.clear();
                return; // console plus reliée : message déjà affiché
            }
        }

    // Sauvegardes déjà sur la console : le site dit si elles peuvent être remplacées.
    int total = std::count_if(lignesSite.begin(), lignesSite.end(), [](const LigneSite& l) { return l.remplace; });
    int numero = 0;
    for (auto& ligne : lignesSite)
    {
        if (!ligne.remplace)
            continue;
        char progression[64];
        snprintf(progression, sizeof progression, "Comparaison %d/%d : ", ++numero, total);
        patienter(progression + ligne.site.jeu + " — " + ligne.site.dresseur + "...");

        std::vector<u8> contenu;
        std::string erreur;
        if (!lireSauvegarde(ligne.cible, contenu, erreur))
        {
            ligne.etat = "illisible", ligne.couleur = ROUGE, ligne.message = erreur, ligne.possible = false;
            continue;
        }
        if (empreinte(contenu) == ligne.site.empreinte)
        {
            ligne.etat = "à jour", ligne.couleur = GRIS, ligne.possible = false;
            ligne.message = "Identique à celle du site : rien à récupérer.";
            continue;
        }
        auto avis = comparer(config.site, config.jeton, ligne.site.id, contenu);
        if (avis.jetonRefuse)
        {
            compte.clear();
            lignesSite.clear();
            definirStatut(avis.message, ROUGE);
            return;
        }
        ligne.message = avis.message;
        switch (avis.verdict)
        {
        case Verdict::Recuperable:
            ligne.etat = "à récupérer", ligne.couleur = JAUNE;
            ligne.cochee = avis.modifieeSurSite; // cochées d'office : celles que le site a modifiées
            ligne.message += " Ta sauvegarde actuelle sera copiée avant d'être remplacée.";
            break;
        case Verdict::AJour:
            ligne.etat = "à jour", ligne.couleur = GRIS, ligne.possible = false;
            break;
        case Verdict::Refusee:
            ligne.etat = "refusée", ligne.couleur = ROUGE, ligne.possible = false;
            break;
        case Verdict::Erreur:
            ligne.etat = "erreur", ligne.couleur = ROUGE, ligne.possible = false;
            break;
        }
    }
    definirStatut(lignesSite.empty() ? "Aucune sauvegarde stockée sur le site."
                                     : "Coche les sauvegardes à récupérer, puis appuie sur X.");
}

void recupererCochees()
{
    if (!wifiConnecte())
    {
        definirStatut("Pas de connexion Wi-Fi : active-la pour récupérer.", ROUGE);
        return;
    }
    int total = std::count_if(lignesSite.begin(), lignesSite.end(), [](const LigneSite& l) { return l.cochee; });
    if (total == 0)
    {
        definirStatut("Aucune sauvegarde cochée (touche A).", JAUNE);
        return;
    }

    int numero = 0, reussies = 0, erreurs = 0;
    for (auto& ligne : lignesSite)
    {
        if (!ligne.cochee)
            continue;
        char progression[64];
        snprintf(progression, sizeof progression, "Récupération %d/%d : ", ++numero, total);
        patienter(progression + ligne.site.jeu + " — " + ligne.site.dresseur + "...");

        auto fichier = telecharger(config.site, config.jeton, ligne.site.id, ligne.cible.cle);
        if (fichier.jetonRefuse)
        {
            compte.clear();
            definirStatut(fichier.message, ROUGE);
            break;
        }
        std::string message;
        if (!fichier.ok)
            message = fichier.message;
        else if (empreinte(fichier.contenu) != ligne.site.empreinte)
            message = "Le fichier reçu n'est pas celui de la liste (modifié sur le site entre-temps ?) : relis la liste (SELECT).";
        else if (ligne.cible.origine == Origine::CarteSD ? ecrireSauvegardeSD(ligne.cible.chemin, fichier.contenu, message)
                                                          : ecrireSauvegardeTitre(ligne.cible, fichier.contenu, message))
        {
            ligne.etat = "récupérée", ligne.couleur = VERT, ligne.cochee = false, ligne.possible = false;
            ligne.message = "Écrite dans " + lieu(ligne.cible) + (message.empty() ? "." : ". " + message);
            // Pour l'envoi, c'est désormais la version du site : « envoyée » tant qu'on n'y rejoue pas. Empreinte de ce qui
            // a été écrit (une sauvegarde Game Boy d'émulateur est raccourcie à la mémoire du jeu, 32 Ko).
            std::vector<u8> ecrite;
            std::string erreur;
            envois.noter(ligne.cible.cle, (lireSauvegarde(ligne.cible, ecrite, erreur) ? empreinte(ecrite) : ligne.site.empreinte)
                + "|" + ligne.cible.fichier);
            rechercherAuRetour = true;
            reussies++;
            continue;
        }
        ligne.etat = "erreur", ligne.couleur = ROUGE, ligne.message = message;
        erreurs++;
    }
    envois.enregistrer();
    if (compte.empty())
        return; // console plus reliée : message déjà affiché

    char resume[160];
    snprintf(resume, sizeof resume, "%d récupérée(s), %d erreur(s).%s", reussies, erreurs,
        erreurs > 0 ? " Choisis une ligne pour lire le message." : " Les copies des anciennes sont dans sdmc:/3ds/PokeBankSync/copies.");
    definirStatut(resume, erreurs > 0 ? JAUNE : VERT);
}

void deplacer(int pas)
{
    int nombre = nombreDeLignes();
    if (nombre == 0)
        return;
    int& actuel = choixCourant();
    int& debut = debutCourant();
    actuel = std::clamp(actuel + pas, 0, nombre - 1);
    if (actuel < debut)
        debut = actuel;
    else if (actuel >= debut + LIGNES_LISTE)
        debut = actuel - LIGNES_LISTE + 1;
}

void cocher(bool tout, bool valeur)
{
    if (mode == Mode::Envoi)
    {
        if (!tout)
        {
            if (choix < static_cast<int>(lignes.size()) && !lignes[choix].empreinte.empty())
                lignes[choix].cochee = !lignes[choix].cochee;
            return;
        }
        for (auto& ligne : lignes)
            ligne.cochee = valeur && !ligne.empreinte.empty();
        return;
    }
    if (!tout)
    {
        if (choixSite < static_cast<int>(lignesSite.size()) && lignesSite[choixSite].possible)
            lignesSite[choixSite].cochee = !lignesSite[choixSite].cochee;
        return;
    }
    for (auto& ligne : lignesSite)
        ligne.cochee = valeur && ligne.possible;
}

} // namespace

int main()
{
    gfxInitDefault();
    consoleInit(GFX_TOP, &haut);
    consoleInit(GFX_BOTTOM, &bas);
    amInit();
    cfguInit();
    hidSetRepeatParameters(25, 6); // flèches maintenues : défilement

    config = chargerConfig();
    envois.charger();
    std::string erreurReseau;
    reseauPret = demarrerReseau(erreurReseau);

    rechercher();
    if (reseauPret)
        verifier();
    else
        definirStatut(erreurReseau, ROUGE);
    dessiner();

    while (aptMainLoop())
    {
        hidScanInput();
        u32 touches = hidKeysDown() | (hidKeysDownRepeat() & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT));
        if (touches & KEY_START)
            break;
        if (touches == 0)
        {
            gspWaitForVBlank();
            continue;
        }

        if (touches & KEY_DOWN)
            deplacer(1);
        if (touches & KEY_UP)
            deplacer(-1);
        if (touches & KEY_RIGHT)
            deplacer(LIGNES_LISTE);
        if (touches & KEY_LEFT)
            deplacer(-LIGNES_LISTE);
        if (touches & KEY_A)
            cocher(false, true);
        if (touches & KEY_R)
            cocher(true, true);
        if (touches & KEY_L)
            cocher(true, false);
        if (mode == Mode::Envoi)
        {
            if (touches & KEY_X)
                envoyerCochees();
            if (touches & KEY_Y)
                relierAuSite();
            if (touches & KEY_SELECT)
            {
                rechercher();
                if (reseauPret && config.relie())
                    verifier();
            }
            if (touches & KEY_B)
            {
                mode = Mode::Recuperation;
                preparerRecuperation();
            }
        }
        else
        {
            if (touches & KEY_X)
                recupererCochees();
            if (touches & KEY_SELECT)
                preparerRecuperation();
            if (touches & KEY_B)
            {
                mode = Mode::Envoi;
                if (rechercherAuRetour)
                    rechercher(); // les sauvegardes écrites apparaissent, « envoyées »
                else
                    definirStatut("Coche les sauvegardes à envoyer, puis appuie sur X.");
                rechercherAuRetour = false;
            }
        }
        dessiner();
    }

    terminerJeux();
    arreterReseau();
    cfguExit();
    amExit();
    gfxExit();
    return 0;
}
