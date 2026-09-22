#include "Localization.h"

#include <cstring>

namespace
{
    AddonAPI_t* api = nullptr;

    struct TextEntry
    {
        const char* key;
        const char* en;
        const char* de;
        const char* es;
        const char* fr;
        const char* pt;
    };

    constexpr TextEntry entries[] =
    {
        { "HB_WINDOW_TITLE", "Home Browser", "Home Browser", "Home Browser", "Home Browser", "Home Browser" },
        { "HB_SEARCH", "Search homes, creators, maps, or tags...", "Häuser, Ersteller, Karten oder Tags suchen...", "Buscar hogares, creadores, mapas o etiquetas...", "Rechercher des demeures, créateurs, cartes ou tags...", "Pesquisar casas, criadores, mapas ou tags..." },
        { "HB_TOP_RATED", "Top Rated", "Bestbewertet", "Mejor valorados", "Les mieux notés", "Mais bem avaliados" },
        { "HB_NEWEST", "Newest", "Neueste", "Más recientes", "Plus récents", "Mais recentes" },
        { "HB_RANDOM", "Random", "Zufällig", "Aleatorio", "Aléatoire", "Aleatório" },
        { "HB_BY_TAG", "By Tag", "Nach Tag", "Por etiqueta", "Par tag", "Por tag" },
        { "HB_ALL_TYPES", "All Types", "Alle Typen", "Todos los tipos", "Tous les types", "Todos os tipos" },
        { "HB_HOMESTEADS", "Homesteads", "Heimstätten", "Hogares", "Domaines", "Casas" },
        { "HB_GUILD_HALLS", "Guild Halls", "Gildenhallen", "Salones de clan", "Halls de guilde", "Salões de guilda" },
        { "HB_ALL_MAPS", "All Maps", "Alle Karten", "Todos los mapas", "Toutes les cartes", "Todos os mapas" },
        { "HB_LIST_VIEW", "List", "Liste", "Lista", "Liste", "Lista" },
        { "HB_THUMBNAIL_VIEW", "Thumbnails", "Vorschaubilder", "Miniaturas", "Miniatures", "Miniaturas" },
        { "HB_REFRESH", "Refresh", "Aktualisieren", "Actualizar", "Actualiser", "Atualizar" },
        { "HB_SETTINGS", "Settings", "Einstellungen", "Ajustes", "Paramètres", "Configurações" },
        { "HB_NAME_CREATOR", "Name / Creator", "Name / Ersteller", "Nombre / Creador", "Nom / Créateur", "Nome / Criador" },
        { "HB_SOURCE", "Source", "Quelle", "Fuente", "Source", "Fonte" },
        { "HB_MAP", "Map", "Karte", "Mapa", "Carte", "Mapa" },
        { "HB_TAGS", "Tags", "Tags", "Etiquetas", "Tags", "Tags" },
        { "HB_DECORATIONS", "Decorations", "Dekorationen", "Decoraciones", "Décorations", "Decorações" },
        { "HB_VIEWS", "Views", "Aufrufe", "Visitas", "Vues", "Visualizações" },
        { "HB_LIKES", "Likes", "Likes", "Me gusta", "J'aime", "Curtidas" },
        { "HB_TYPE", "Type", "Typ", "Tipo", "Type", "Tipo" },
        { "HB_HOMESTEAD", "Homestead", "Heimstätte", "Hogar", "Domaine", "Casa" },
        { "HB_GUILD_HALL", "Guild Hall", "Gildenhalle", "Salón de clan", "Hall de guilde", "Salão de guilda" },
        { "HB_PREVIOUS", "Previous", "Zurück", "Anterior", "Précédent", "Anterior" },
        { "HB_NEXT", "Next", "Weiter", "Siguiente", "Suivant", "Próximo" },
        { "HB_PAGE", "Page %d of %d", "Seite %d von %d", "Página %d de %d", "Page %d sur %d", "Página %d de %d" },
        { "HB_RESULTS", "%d results", "%d Ergebnisse", "%d resultados", "%d résultats", "%d resultados" },
        { "HB_RESULTS_LOADED", "%d of %d loaded", "%d von %d geladen", "%d de %d cargados", "%d sur %d chargés", "%d de %d carregados" },
        { "HB_THUMBNAIL_PENDING", "THUMBNAIL\nCOMING SOON", "VORSCHAUBILD\nFOLGT", "MINIATURA\nPRÓXIMAMENTE", "MINIATURE\nBIENTÔT", "MINIATURA\nEM BREVE" },
        { "HB_THUMBNAIL_LOADING", "LOADING\nTHUMBNAIL", "VORSCHAUBILD\nWIRD GELADEN", "CARGANDO\nMINIATURA", "CHARGEMENT DE\nLA MINIATURE", "CARREGANDO\nMINIATURA" },
        { "HB_THUMBNAIL_UNAVAILABLE", "THUMBNAIL\nUNAVAILABLE", "VORSCHAUBILD\nNICHT VERFÜGBAR", "MINIATURA\nNO DISPONIBLE", "MINIATURE\nINDISPONIBLE", "MINIATURA\nINDISPONÍVEL" },
        { "HB_BY", "by %s", "von %s", "por %s", "par %s", "por %s" },
        { "HB_DETAILS_TITLE", "Home Browser Details", "Home-Browser-Details", "Detalles de Home Browser", "Détails de Home Browser", "Detalhes do Home Browser" },
        { "HB_DESCRIPTION", "Description", "Beschreibung", "Descripción", "Description", "Descrição" },
        { "HB_REQUIRED", "Required", "Benötigt", "Necesarias", "Requises", "Necessárias" },
        { "HB_MISSING", "Missing", "Fehlend", "Faltantes", "Manquantes", "Faltando" },
        { "HB_OPEN_ORIGINAL", "View on original website", "Auf Originalwebsite ansehen", "Ver en el sitio original", "Voir sur le site d'origine", "Ver no site original" },
        { "HB_DOWNLOAD_XML", "Download XML", "XML herunterladen", "Descargar XML", "Télécharger le XML", "Baixar XML" },
        { "HB_DOWNLOAD_FOLDER_REQUIRED", "Set this XML download folder in Settings first.", "Lege zuerst diesen XML-Downloadordner in den Einstellungen fest.", "Configura primero esta carpeta de descarga XML en Ajustes.", "Définissez d'abord ce dossier de téléchargement XML dans les paramètres.", "Defina primeiro esta pasta de download XML nas configurações." },
        { "HB_API_KEY", "Guild Wars 2 API Key", "Guild Wars 2 API-Schlüssel", "Clave de API de Guild Wars 2", "Clé API Guild Wars 2", "Chave de API do Guild Wars 2" },
        { "HB_API_HELP", "Optional. Used to compare required decorations with your account.", "Optional. Wird zum Vergleich mit deinem Account verwendet.", "Opcional. Se utiliza para comparar las decoraciones necesarias con tu cuenta.", "Facultatif. Utilisé pour comparer les décorations requises avec votre compte.", "Opcional. Usada para comparar as decorações necessárias com sua conta." },
        { "HB_XML_FOLDERS", "XML Download Folders", "XML-Downloadordner", "Carpetas de descarga XML", "Dossiers de téléchargement XML", "Pastas de download XML" },
        { "HB_HOMESTEAD_FOLDER", "Homestead folder", "Heimstättenordner", "Carpeta de hogares", "Dossier des domaines", "Pasta de casas" },
        { "HB_GUILD_HALL_FOLDER", "Guild Hall folder", "Gildenhallenordner", "Carpeta de salones", "Dossier des halls de guilde", "Pasta de salões" },
        { "HB_PREFERENCES", "Preferences", "Einstellungen", "Preferencias", "Préférences", "Preferências" },
        { "HB_REMEMBER_WINDOW", "Remember window state", "Fensterstatus merken", "Recordar estado de la ventana", "Mémoriser l'état de la fenêtre", "Lembrar estado da janela" },
        { "HB_DEFAULT_VIEW", "Default browser view", "Standardansicht", "Vista predeterminada", "Vue par défaut", "Visualização padrão" },
        { "HB_SHOW_XMLLESS", "Show listings without downloadable XML", "Einträge ohne herunterladbare XML anzeigen", "Mostrar anuncios sin XML descargable", "Afficher les publications sans XML téléchargeable", "Mostrar anúncios sem XML disponível" },
        { "HB_PROVIDER_STATUS", "Provider Status", "Anbieterstatus", "Estado de proveedores", "État des sources", "Status dos provedores" },
        { "HB_CONNECTING", "Connecting...", "Verbindung wird hergestellt...", "Conectando...", "Connexion...", "Conectando..." },
        { "HB_CONNECTED", "Connected", "Verbunden", "Conectado", "Connecté", "Conectado" },
        { "HB_UNAVAILABLE", "Unavailable", "Nicht verfügbar", "No disponible", "Indisponible", "Indisponível" },
        { "HB_CLOSE", "Close", "Schließen", "Cerrar", "Fermer", "Fechar" },
        { "HB_NO_RESULTS", "No homes match the current filters.", "Keine Häuser entsprechen den aktuellen Filtern.", "Ningún hogar coincide con los filtros.", "Aucune demeure ne correspond aux filtres.", "Nenhuma casa corresponde aos filtros." },
        { "HB_TAG_FILTERS", "Tag Filters", "Tag-Filter", "Filtros de etiquetas", "Filtres de tags", "Filtros de tags" },
        { "HB_CLEAR_TAGS", "Clear Tags", "Tags löschen", "Borrar etiquetas", "Effacer les tags", "Limpar tags" },
        { "HB_REQUIRED_ONLY", "Add a Guild Wars 2 API key in Settings to calculate missing decorations.", "Füge in den Einstellungen einen API-Schlüssel hinzu, um fehlende Dekorationen zu berechnen.", "Añade una clave API en Ajustes para calcular las decoraciones faltantes.", "Ajoutez une clé API dans les paramètres pour calculer les décorations manquantes.", "Adicione uma chave de API nas configurações para calcular as decorações ausentes." },
        { "HB_GUILD_MISSING_LATER", "Guild Hall inventory comparison requires selecting a guild and will be added in a later patch.", "Der Gildenhallen-Vergleich benötigt eine Gildenauswahl und folgt in einem späteren Patch.", "La comparación de salones requiere seleccionar un clan y se añadirá en un parche posterior.", "La comparaison des halls nécessite de sélectionner une guilde et sera ajoutée ultérieurement.", "A comparação de salões exige selecionar uma guilda e será adicionada depois." },
        { "HB_LOADING_ACCOUNT_DECORATIONS", "Checking your available decorations...", "Verfügbare Dekorationen werden geprüft...", "Comprobando tus decoraciones disponibles...", "Vérification de vos décorations disponibles...", "Verificando suas decorações disponíveis..." },
        { "HB_DECORATION_ROW", "%dx - %s", "%dx - %s", "%dx - %s", "%dx - %s", "%dx - %s" },
        { "HB_NONE_MISSING", "You have every required decoration.", "Du hast alle benötigten Dekorationen.", "Tienes todas las decoraciones necesarias.", "Vous avez toutes les décorations requises.", "Você possui todas as decorações necessárias." },
        { "HB_LOADING_DECORATIONS", "Loading decorations...", "Dekorationen werden geladen...", "Cargando decoraciones...", "Chargement des décorations...", "Carregando decorações..." },
        { "HB_RETRY", "Retry", "Erneut versuchen", "Reintentar", "Réessayer", "Tentar novamente" }
    };

    const char* EnglishFallback(const char* key)
    {
        for (const TextEntry& entry : entries)
        {
            if (std::strcmp(entry.key, key) == 0)
            {
                return entry.en;
            }
        }
        return key;
    }
}

void Localization::Initialize(AddonAPI_t* addonApi)
{
    api = addonApi;
    if (api == nullptr || api->Localization_Set == nullptr)
    {
        return;
    }

    for (const TextEntry& entry : entries)
    {
        api->Localization_Set(entry.key, "en", entry.en);
        api->Localization_Set(entry.key, "de", entry.de);
        api->Localization_Set(entry.key, "es", entry.es);
        api->Localization_Set(entry.key, "fr", entry.fr);
        api->Localization_Set(entry.key, "pt", entry.pt);
    }
}

void Localization::Shutdown()
{
    api = nullptr;
}

const char* Localization::Translate(const char* key)
{
    if (api != nullptr && api->Localization_Translate != nullptr)
    {
        const char* translated = api->Localization_Translate(key);
        if (translated != nullptr && std::strcmp(translated, key) != 0)
        {
            return translated;
        }
    }
    return EnglishFallback(key);
}
