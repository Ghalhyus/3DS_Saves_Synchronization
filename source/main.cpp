// PokeBank Sync : cherche les sauvegardes Pokémon de la 3DS (jeux 3DS, Console virtuelle, fichiers .sav de la carte
// SD) et les envoie au site PokeBank, où elles deviennent des sauvegardes stockées (ajout ou mise à jour).
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

PrintConsole haut, bas;
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

/// État d'une ligne d'après l'empreinte de son dernier envoi.
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
    else if (dernier != ligne.empreinte)
        ligne.etat = "modifiée", ligne.couleur = JAUNE;
    else
        ligne.etat = "envoyée", ligne.couleur = GRIS;
    ligne.cochee = dernier != ligne.empreinte;
}

void dessiner()
{
    // --- Écran du haut : la liste ---
    consoleSelect(&haut);
    consoleClear();
    ecrireA(0, 0, versConsole("PokeBank Sync"), CYAN);
    std::string relie = compte.empty() ? versConsole("pas relié (touche Y)") : versConsole("compte : " + compte);
    if (static_cast<int>(relie.size()) > LARGEUR_HAUT - 15)
        relie = ajuster(relie, LARGEUR_HAUT - 15);
    ecrireA(0, LARGEUR_HAUT - static_cast<int>(relie.size()), relie, compte.empty() ? JAUNE : VERT);
    ecrireA(1, 0, ajuster(versConsole(config.site.empty() ? "aucun site" : config.site), LARGEUR_HAUT), GRIS);
    ecrireA(2, 0, std::string(LARGEUR_HAUT, '\xC4'));

    if (lignes.empty())
        ecrireA(4, 1, versConsole("Aucune sauvegarde Pokémon trouvée."), JAUNE);
    for (int i = 0; i < LIGNES_LISTE && debutListe + i < static_cast<int>(lignes.size()); i++)
    {
        int n = debutListe + i;
        const auto& ligne = lignes[n];
        std::string debut = std::string(n == choix ? ">" : " ") + (ligne.cochee ? "[x] " : "[ ] ");
        std::string nom = ajuster(versConsole(ligne.sauvegarde.nom), LARGEUR_NOM);
        ecrireA(3 + i, 0, debut + nom + " ", n == choix ? INVERSE : NORMAL);
        ecrireA(3 + i, LARGEUR_HAUT - LARGEUR_ETAT, ajuster(versConsole(ligne.etat), LARGEUR_ETAT), ligne.couleur);
    }
    if (debutListe > 0)
        ecrireA(3, LARGEUR_HAUT - 1, "\x1E", GRIS); // il y en a au-dessus
    if (debutListe + LIGNES_LISTE < static_cast<int>(lignes.size()))
        ecrireA(3 + LIGNES_LISTE - 1, LARGEUR_HAUT - 1, "\x1F", GRIS); // et en dessous

    int cochees = std::count_if(lignes.begin(), lignes.end(), [](const Ligne& l) { return l.cochee; });
    char resume[64];
    snprintf(resume, sizeof resume, "%d sauvegarde(s), %d cochée(s)", static_cast<int>(lignes.size()), cochees);
    ecrireA(HAUTEUR - 2, 0, versConsole(resume), GRIS);

    // --- Écran du bas : détail, état, touches ---
    consoleSelect(&bas);
    consoleClear();
    int y = 0;
    if (choix < static_cast<int>(lignes.size()))
    {
        const auto& ligne = lignes[choix];
        for (const auto& l : couper(versConsole(ligne.sauvegarde.nom), LARGEUR_BAS))
            ecrireA(y++, 0, l, CYAN);
        for (const auto& l : couper(versConsole(ligne.sauvegarde.detail), LARGEUR_BAS))
            if (y < 8)
                ecrireA(y++, 0, l, GRIS);
        if (!ligne.message.empty())
        {
            y++;
            for (const auto& l : couper(versConsole(ligne.message), LARGEUR_BAS))
                if (y < 15)
                    ecrireA(y++, 0, l, ligne.couleur);
        }
    }

    y = 16;
    ecrireA(y++, 0, std::string(LARGEUR_BAS, '\xC4'));
    for (const auto& l : couper(versConsole(statut), LARGEUR_BAS))
        if (y < 24)
            ecrireA(y++, 0, l, couleurStatut);

    ecrireA(25, 0, versConsole("A cocher   L/R tout décocher/cocher"), GRIS);
    ecrireA(26, 0, versConsole("X envoyer les sauvegardes cochées"), GRIS);
    ecrireA(27, 0, versConsole("Y relier au site   SELECT rechercher"), GRIS);
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
    if (!saisir("Code de « Mes sauvegardes » (ABCD-EFGH)", "", 12, code))
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
        auto resultat = envoyer(config.site, config.jeton, ligne.sauvegarde.fichier, contenu);
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
            envois.noter(ligne.sauvegarde.cle, ligne.empreinte);
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

void deplacer(int pas)
{
    if (lignes.empty())
        return;
    choix = std::clamp(choix + pas, 0, static_cast<int>(lignes.size()) - 1);
    if (choix < debutListe)
        debutListe = choix;
    else if (choix >= debutListe + LIGNES_LISTE)
        debutListe = choix - LIGNES_LISTE + 1;
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
        if ((touches & KEY_A) && choix < static_cast<int>(lignes.size()) && !lignes[choix].empreinte.empty())
            lignes[choix].cochee = !lignes[choix].cochee;
        if (touches & KEY_R)
            for (auto& ligne : lignes)
                ligne.cochee = !ligne.empreinte.empty();
        if (touches & KEY_L)
            for (auto& ligne : lignes)
                ligne.cochee = false;
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
        dessiner();
    }

    arreterReseau();
    cfguExit();
    amExit();
    gfxExit();
    return 0;
}
