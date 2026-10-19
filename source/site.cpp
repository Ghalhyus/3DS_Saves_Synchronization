#include "site.hpp"

#include "config.hpp"
#include "racines_pem.h" // data/racines.pem, intégré par le Makefile

#include <cstdio>
#include <cstdlib>
#include <curl/curl.h>
#include <jansson.h>
#include <malloc.h>

namespace {

constexpr u32 TAILLE_SOC = 0x100000;
u32* tamponSoc = nullptr;
bool acDemarre = false;
bool sslcDemarre = false;

/// Racines de confiance au format PEM : celles du programme, puis celles ajoutées par l'utilisateur.
std::string certificats;

struct Reponse
{
    long code = 0;      // statut HTTP, 0 si la requête n'a pas abouti
    std::string corps;
    std::string erreur; // message prêt à afficher si la requête n'a pas abouti
};

size_t recevoir(char* donnees, size_t taille, size_t nombre, void* corps)
{
    static_cast<std::string*>(corps)->append(donnees, taille * nombre);
    return taille * nombre;
}

std::string messageReseau(CURLcode code, const char* detail)
{
    std::string precision = detail && detail[0] ? std::string(" (") + detail + ")" : "";
    switch (code)
    {
    case CURLE_COULDNT_RESOLVE_HOST:
        return "Adresse du site introuvable : vérifie-la (touche Y) et la connexion Wi-Fi.";
    case CURLE_COULDNT_CONNECT:
    case CURLE_OPERATION_TIMEDOUT:
        return "Le site ne répond pas : vérifie la connexion Wi-Fi, ou réessaie plus tard.";
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CACERT_BADFILE:
        return "Certificat du site non reconnu" + precision + " : vérifie la date de la console, ou ajoute la racine "
               "du site dans sdmc:/3ds/PokeBankSync/certificats.pem.";
    case CURLE_SSL_CONNECT_ERROR:
        return "Connexion sécurisée impossible" + precision + ".";
    default:
        return std::string("Erreur réseau : ") + curl_easy_strerror(code) + precision + ".";
    }
}

Reponse requete(const std::string& url, const std::vector<std::string>& entetes, const void* corps = nullptr,
    size_t taille = 0, bool post = false)
{
    Reponse reponse;
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        reponse.erreur = "Erreur réseau : libcurl n'a pas pu démarrer.";
        return reponse;
    }
    char detail[CURL_ERROR_SIZE] = {};
    curl_blob racines{const_cast<char*>(certificats.data()), certificats.size(), CURL_BLOB_NOCOPY};
    curl_slist* liste = nullptr;
    for (const auto& entete : entetes)
        liste = curl_slist_append(liste, entete.c_str());
    liste = curl_slist_append(liste, "Expect:"); // pas d'attente « 100-continue » avant d'envoyer le fichier

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "PokeBankSync/1.0 (Nintendo 3DS)");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, detail);
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &racines);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, liste);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, recevoir);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reponse.corps);
    if (post)
    {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(taille));
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, corps);
    }

    CURLcode resultat = curl_easy_perform(curl);
    if (resultat == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &reponse.code);
    else
        reponse.erreur = messageReseau(resultat, detail);

    curl_slist_free_all(liste);
    curl_easy_cleanup(curl);
    return reponse;
}

/// Réponse JSON du site (null si ce n'en est pas une) ; à libérer avec json_decref.
json_t* lireJson(const Reponse& reponse)
{
    json_error_t erreur;
    json_t* racine = json_loadb(reponse.corps.data(), reponse.corps.size(), 0, &erreur);
    if (racine && !json_is_object(racine))
    {
        json_decref(racine);
        return nullptr;
    }
    return racine;
}

std::string champ(json_t* racine, const char* nom)
{
    json_t* valeur = racine ? json_object_get(racine, nom) : nullptr;
    return json_is_string(valeur) ? json_string_value(valeur) : "";
}

/// Message d'une réponse : celui du site s'il y en a un, sinon une explication selon le statut HTTP.
std::string messageDe(const Reponse& reponse, json_t* json)
{
    if (!reponse.erreur.empty())
        return reponse.erreur;
    auto message = champ(json, "message");
    if (!message.empty())
        return message;
    char texte[160];
    if (reponse.code == 404 || !json)
        snprintf(texte, sizeof texte, "Ce site ne répond pas comme PokeBank (HTTP %ld) : vérifie son adresse (touche Y).", reponse.code);
    else
        snprintf(texte, sizeof texte, "Réponse inattendue du site (HTTP %ld).", reponse.code);
    return texte;
}

std::string autorisation(const std::string& jeton) { return "Authorization: Bearer " + jeton; }

} // namespace

bool demarrerReseau(std::string& erreur)
{
    certificats.assign(reinterpret_cast<const char*>(racines_pem), racines_pem_size);
    std::string supplementaires = std::string(DOSSIER_APPLI) + "/certificats.pem";
    if (FILE* f = fopen(supplementaires.c_str(), "rb"))
    {
        char tampon[4096];
        size_t lus;
        certificats += "\n";
        while ((lus = fread(tampon, 1, sizeof tampon, f)) > 0)
            certificats.append(tampon, lus);
        fclose(f);
    }

    acDemarre = R_SUCCEEDED(acInit());
    // L'aléa de mbedTLS (clés de session TLS) vient du service ssl:C (sslcGenerateRandomData, 3ds-mbedtls).
    sslcDemarre = R_SUCCEEDED(sslcInit(0));
    if (!sslcDemarre)
    {
        erreur = "Le service ssl:C de la console n'a pas pu démarrer : connexion sécurisée impossible.";
        return false;
    }
    tamponSoc = static_cast<u32*>(memalign(0x1000, TAILLE_SOC));
    if (!tamponSoc || R_FAILED(socInit(tamponSoc, TAILLE_SOC)))
    {
        free(tamponSoc);
        tamponSoc = nullptr;
        erreur = "Le réseau de la console n'a pas pu démarrer.";
        return false;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        erreur = "libcurl n'a pas pu démarrer.";
        return false;
    }
    return true;
}

void arreterReseau()
{
    if (tamponSoc)
    {
        curl_global_cleanup();
        socExit();
        free(tamponSoc);
        tamponSoc = nullptr;
    }
    if (sslcDemarre)
        sslcExit();
    sslcDemarre = false;
    if (acDemarre)
        acExit();
    acDemarre = false;
}

bool wifiConnecte()
{
    u32 etat = 0;
    return acDemarre && R_SUCCEEDED(ACU_GetWifiStatus(&etat)) && etat != 0;
}

ResultatLiaison relier(const std::string& site, const std::string& code, const std::string& nomConsole)
{
    ResultatLiaison resultat;
    json_t* demande = json_pack("{s:s, s:s}", "code", code.c_str(), "nom", nomConsole.c_str());
    char* texte = json_dumps(demande, JSON_COMPACT);
    json_decref(demande);
    std::string corps = texte ? texte : "{}";
    free(texte);

    auto reponse = requete(site + "/api/sync/liaison", {"Content-Type: application/json"}, corps.data(), corps.size(), true);
    json_t* json = lireJson(reponse);
    resultat.jeton = champ(json, "jeton");
    resultat.compte = champ(json, "compte");
    resultat.ok = reponse.code == 200 && !resultat.jeton.empty();
    resultat.message = messageDe(reponse, json);
    if (json)
        json_decref(json);
    return resultat;
}

ResultatCompte verifierCompte(const std::string& site, const std::string& jeton)
{
    ResultatCompte resultat;
    auto reponse = requete(site + "/api/sync/compte", {autorisation(jeton)});
    json_t* json = lireJson(reponse);
    resultat.compte = champ(json, "compte");
    resultat.ok = reponse.code == 200 && !resultat.compte.empty();
    resultat.jetonRefuse = reponse.code == 401;
    resultat.message = resultat.ok ? "" : messageDe(reponse, json);
    if (json)
        json_decref(json);
    return resultat;
}

ResultatEnvoi envoyer(const std::string& site, const std::string& jeton, const std::string& fichier, const std::vector<u8>& contenu)
{
    ResultatEnvoi resultat;
    char* nom = curl_easy_escape(nullptr, fichier.c_str(), static_cast<int>(fichier.size()));
    std::string url = site + "/api/sync/sauvegardes?fichier=" + (nom ? nom : "sauvegarde.sav");
    curl_free(nom);

    auto reponse = requete(url, {autorisation(jeton), "Content-Type: application/octet-stream"},
        contenu.data(), contenu.size(), true);
    json_t* json = lireJson(reponse);
    auto statut = champ(json, "statut");
    if (reponse.code == 200 && statut == "ajoutee")
        resultat.statut = StatutEnvoi::Ajoutee;
    else if (reponse.code == 200 && statut == "mise-a-jour")
        resultat.statut = StatutEnvoi::MiseAJour;
    else if (reponse.code == 200 && statut == "inchangee")
        resultat.statut = StatutEnvoi::DejaAJour;
    else if (reponse.code == 200 && statut == "refusee")
        resultat.statut = StatutEnvoi::Refusee;
    else if (reponse.code == 401)
        resultat.statut = StatutEnvoi::JetonRefuse;
    resultat.message = messageDe(reponse, json);
    if (json)
        json_decref(json);
    return resultat;
}
