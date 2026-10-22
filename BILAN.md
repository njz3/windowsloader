# Bilan des jeux sous windowsloader

Ce qui marche, jeu par jeu et fonction par fonction, rempli au fur et à mesure des jeux traités
(copie locale depuis racecab1, module, essais, copie effacée). Le détail des essais et des trouvailles est
dans [NOTES.md](NOTES.md) (journal).

Légende : ✅ fait et vérifié · ⚠️ fait, vérifié en partie (mode agent, sans volant physique…) · ❌ pas
possible ou absent du jeu · ⏳ pas encore fait · — sans objet.

Ordre de traitement : jeux Sega en 32 bits avec retour de force d'abord (liste de la session Image borne
dans NOTES.md, « Jeux des bornes »).

## Vue d'ensemble

| Jeu | Lancement | Commandes | vJoy | Retour de force | Affichage | Menu F10 | Menu de service | Crédits / Shell |
|---|---|---|---|---|---|---|---|---|
| Sega Rally 3 (Europa-R) | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ❌ | — |
| Daytona Championship USA | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ |
| Daytona Championship USA NSE | ✅ | ✅ | ⚠️ | ✅ | ⚠️ | ✅ | ❌ | ✅ |
| Ford Racing Full Blown | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ⚠️ | — |
| Initial D Arcade Stage 6 AA | ✅ | ✅ | ✅ | ⚠️ | ⚠️ | ❌ | ✅ | ✅ |
| GRID | ✅ | ✅ | ✅ | ✅ | ⚠️ | ✅ | ❌ | ✅ |
| DiRT Showdown | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ❌ | ✅ |
| K.O. Drive (RingWide) | ✅ | ✅ | ✅ | ⚠️ | ⚠️ | ✅ | ✅ | ✅ |
| Sonic & Sega All-Stars Racing (RingWide) | ✅ | ❌ | ⏳ | ⚠️ | ✅ | ⏳ | ✅ | ⚠️ |
| Battle Gear 4 Tuned (Taito Type X+) | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ✅ |
| Chase H.Q. 2 (Taito Type X2) | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ✅ |

Fonctions :

- **Lancement** : le jeu démarre par `windowsloader.exe`, sans patch ni fichier du jeu modifié, en fenêtre.
- **Commandes** : volant, pédales, boutons du jeu reçus de windowsloader (vérifié en mode agent :
  chaque commande pilotée et son effet vu dans le jeu).
- **vJoy** : manettes lues par SDL et retour de force envoyé au joueur 1 (vJoy sur le PC d'essai, aucun
  volant physique branché).
- **Retour de force** : la sortie du jeu décodée en force −1..1, sens vérifié (le ressort de centrage
  pousse contre le braquage).
- **Affichage** : `[Display]` de `windowsloader.ini` (aspect, mode, résolution interne), vérifié à l'écran
  par `scripts/agent/display_check.py`. Par défaut l'image est recopiée dans la fenêtre par GDI
  (`PRESENT = GDI`, voir « Affichage sur ce PC » plus bas) ; `PRESENT = DIRECT` garde le Present du jeu.
- **Menu F10** : pause, pages Jeu, Contrôles, Affichage, Retour de force, À propos.
- **Menu de service** : le menu de test de la borne (bouton Test).
- **Crédits / Shell** : pièces, crédits et ce que le Shell de la borne fournit au jeu.

## Sega Rally 3 (Sega Europa-R, 2008)

`Rally\Rally.exe`, x86, Direct3D 9. Module `src/hook/games/segarally3.cpp`. Copie locale effacée
(Corbeille) le 22/10.

- Commandes ✅ : carte d'E/S USB HID émulée (VID 0x1043), volant, pédales, Start, vue, frein à main,
  rapports + / − (`sr3_bits.py`, `sr3_pedals.py`, `sr3_race_bits.py`, `sr3_handbrake.py`).
- vJoy ⚠️ : ouvert par un lancement normal, retour de force vers lui ; pas repris en course depuis.
- Retour de force ✅ : port 0x1020 par inpout32 (émulé, la vraie DLL jamais chargée), sens vérifié
  (`sr3_ffb.py`).
- Affichage ✅ : 16 couples aspect × mode et fenêtres réduites justes à l'écran ; résolution interne
  (`config.ini`) vérifiée en 1280×720.
- Menu F10 ✅ (`sr3_menu.py`).
- Menu de service ❌ : il appartient au Shell Europa-R, absent des copies.

## Daytona Championship USA (2017) et New Season Edition (2019)

`Sega\Daytona\Daytona.exe`, x86, Direct3D 11. Module `src/hook/games/daytona.cpp`. Copies locales
effacées (Corbeille) le 22/10.

- Commandes ✅ : manette DirectInput « SEGA IO Controller » jouée par windowsloader ; volant (sens
  vérifié), accélérateur, frein (57 → 18 mph en 2 s), Start, rapports + / −, levier en H et vues câblés
  (`day_race.py`, les deux éditions).
- vJoy ✅ (originale) : `--controllers`, course pilotée par le script, effet de force constante lancé sur
  vJoy et mis à jour (`day_race.py wheel`). NSE ⚠️ : même code, non refait avec vJoy.
- Retour de force ✅ : InpWrapper.exe (port parallèle) joué par une fenêtre cachée, octet décodé, sens
  vérifié dans les menus et en course.
- Affichage ✅ (originale) : 16 couples et fenêtres réduites justes à l'écran (fenêtre enfant pour
  l'image Direct3D 11). NSE ⚠️ : même code, non vérifié à l'écran.
- Menu F10 ✅ (Direct3D 11).
- Menu de service ❌ : le menu de test est celui de `Shell.exe`, non émulé.
- Crédits / Shell ✅ : Shell émulé (TCP 127.0.0.1:18888) : crédits, défi de sécurité, SKU (la NSE
  l'attend).
- À part : réseau local refusé, caméra refusée, carte graphique dédiée choisie sur un portable.

## Ford Racing Full Blown (Sega, 2006)

`FordRacing\fordracing.exe`, x86, Direct3D 9 ; `GameShell\GameShell.exe`. Module
`src/hook/games/fordracing.cpp`. Copie locale le 22/10 (333 Mo), plugin FFB, `teknoparrot.ini`,
dgVoodoo et jconfig (JVSEmuFR.dll) mis de côté dans `_teknoparrot_off` ; effacée (Corbeille) le 22/10.

- Lancement ✅ : le jeu importe inpout32 statiquement ; le lanceur fait pointer cette importation vers
  la DLL de windowsloader (qui exporte Inp32 / Out32), le vrai pilote n'est jamais chargé. Jeu en
  free play sans son Shell ; réseau local refusé.
- Commandes ✅ : carte Happ UGCI émulée (USB HID VID 0x078B PID 0x0010, rapports de 8 octets) :
  volant, accélérateur, frein (étalonnage de `data\calibration.ini`), Start, rapports + / −, vue ;
  démo et course pilotées (`fr_bits.py`, `fr_race.py`).
- vJoy ✅ : `--controllers`, course complète, retour de force vers le joueur 1 (`fr_race.py wheel`).
- Retour de force ✅ : port parallèle 0x378 décodé (0x01–0x0F d'un côté, 0x10–0x1E de l'autre),
  sens vérifié en course (volant à droite : force moyenne vers la gauche).
- Affichage ✅ : jeu 4:3 ; 4:3 / 16:9 / STRETCH justes à l'écran (16:9 : bandes latérales,
  `display_check.py fr`) ; résolution interne par `gamesettings.ini` ; FPS et `[fordracing]` dans le titre.
- Menu F10 ✅ (pages Contrôles et Retour de force vues en course).
- Menu de service ⚠️ : il est dans GameShell.exe, lancé sous windowsloader avec le même module
  (`fr_shell.py`). Il démarre, trouve la carte émulée, reçoit le CRC que lui donnerait le lanceur de la
  borne (`/k=`), puis s'arrête sur « SECURITY FAILURE 4 » : il veut la clé iButton de la borne (DS1991,
  pilotes TMEX `IBFS32.DLL`), non émulée. Ses écritures dans HKLM (adresse IP de la carte réseau) et
  l'horloge du PC sont refusées par windowsloader.

## Initial D Arcade Stage 6 AA (Sega RingEdge, 2011)

`id6_dump_.exe` (l'exécutable déchiffré ; `InitialD6_GLW_RE_SBUU.exe` est chiffré), x86, OpenGL.
Module `src/hook/games/ringedge.cpp`. Copie locale le 22/10 (4,2 Go, 3 840 fichiers) ; plugin FFB,
OutputBlaster et `teknoparrot.ini` mis de côté dans `_teknoparrot_off`.

La plateforme RingEdge est émulée ; elle servira aussi aux RingWide (K.O. Drive, Sonic & Sega All-Stars
Racing) :
- les pilotes `\\.\mxsram` (SRAM dans `wl_sram.bin`), `mxsuperio`, `mxsmbus` et `columba`
  (une table DMI fabriquée : plateforme « AAL ») ;
- les services PCP sur 127.0.0.1:40100 et suivants : mxmaster, la clé (keychip), le réseau, gcatcher ;
- la clé répond à ses défis d'après `SBUU_Table.dat`, la table du jeu ;
- le JVS sur COM4 ;
- la carte de pilotage sur COM1.

- Lancement ✅ : démarrage, défis de la clé tenus, « Network error 8301 » (borne seule : le jeu passe
  hors ligne), réglage du volant, démo.
- Commandes ✅ : carte JVS Sega 837-14572 : pièces, Start, choix (marque, voiture, boîte), course :
  accélérateur (182 km/h), volant, vue (`id6_race.py`). Boîte automatique choisie : levier non essayé.
- vJoy ✅ : `--controllers`, course complète, couple envoyé au joueur 1 (`id6_race.py wheel`).
- Retour de force ⚠️ : carte de pilotage Sega sur COM1, couple (0x84) envoyé en course ; sens à
  confirmer au volant ; le test du menu de service est grisé (sa vérification fait bouger le volant et
  attend que la position suive).
- Affichage ⚠️ : OpenGL ; le lanceur fait passer le jeu sur la carte NVIDIA d'un portable Optimus (noir
  sur l'Intel) ; FPS et `[initiald6]` dans le titre ; captures pour l'agent. La fenêtre reste celle du
  jeu (1920×1080 sans cadre) : `[Display]` pas encore appliqué en OpenGL.
- Menu F10 ❌ : pas encore en OpenGL.
- Menu de service ✅ : le jeu lancé avec `-t` (comme `gametest.bat`) : GAME TEST MODE, tests d'entrées
  et de sorties, choix au bouton Service, validation au bouton Test (`id6_race.py test`).
- Crédits ✅ : pièces comptées par la carte JVS.

## GRID (Codemasters pour Sega, 2008, Europa-R)

`Sega\Grid\GRID.exe`, x86, Direct3D 9. Module `src/hook/games/codemasters.cpp` (avec DiRT Showdown). Copie locale le 22/10 de tout
`C:\Sega` de racecab1 (19 953 fichiers, 7 Go, 10 min) : `Grid`, la coquille Sega `shell` et `ShellData`.
Plugin FFB et `teknoparrot.ini` mis de côté.

- Lancement ✅ : le jeu veut `C:\Sega\Grid` et y crée `ShellData` : `C:\Sega` est redirigé vers la copie
  (nouvelle redirection de dossier de windowsloader), rien n'est écrit à la racine du disque. Deux corrections :
  une borne seule (`NumCabinets=1`), et le plantage volontaire d'OpenParrot (0x5AB9E5) retiré.
- Commandes ✅ : manette DirectInput « SEGA IO Controller » jouée par windowsloader (comme Daytona) : volant,
  accélérateur, frein, Start / validation des menus, vue, remise en piste ; étalonnage de `ShellData.ini`
  (`grid_race.py`). Les vraies manettes du PC (vJoy) sont cachées au jeu, qui sinon créait ses effets dessus et
  plantait.
- vJoy ✅ : `--controllers`, course, force vers le joueur 1 (`grid_race.py wheel`).
- Retour de force ✅ : port 0x1020 (Out32), codé comme celui de Sega Rally 3 ; volant tenu à gauche, force
  moyenne vers la droite.
- Affichage ⚠️ : le jeu prenait la taille du bureau (3072×1920 sur ce portable : mémoire épuisée en course) ;
  EnumDisplaySettings lui donne `[Display]` WIDTH × HEIGHT, 1920×1080 au plus en AUTO. Fenêtre, FPS et
  `[grid]` dans le titre ; non mesuré par `display_check.py`.
- Menu F10 ✅.
- Menu de service ❌ : il est dans la coquille `shell\Shell.exe` (Europa-R), qui ne démarre pas : elle
  importe `DK2win32.dll`, la bibliothèque d'une clé matérielle Dinkey, absente de la copie (installée dans le
  système de la borne). Il faudrait émuler cette clé.
- Crédits ✅ : free play (`ShellData.ini`).

## DiRT Showdown (Codemasters pour Sega Amusements, 2015)

`Showdown\Showdown.exe`, x86, Direct3D 11, même organisation que GRID (`C:\Sega` avec la coquille Sega
Europe). Module `src/hook/games/codemasters.cpp` (avec GRID). Copie locale le 22/10 (6 455 fichiers, 14,3 Go,
13 min) ; plugin FFB et `teknoparrot.ini` mis de côté.

- Lancement ✅ : `C:\Sega` redirigé ; la coquille Sega émulée (`segashell.cpp`, commune avec Daytona :
  trames « DSW », ancienne numérotation, accusé de GAME_MSG_LAUNCHED_OK sans lequel le jeu quitte) ; le pilote
  audio Rapture3D, qui plante sans ses réglages dans le registre du PC, est refusé (OpenAL prend son pilote
  DirectSound) ; image 10 bits capturée. Démo en boucle, « FREEPLAY / PRESS START ».
- Commandes ✅ (mode agent, `scripts/agent/race_check.py showdown`) : « PRESS START OR ENTER CODE », choix du mode (Challenge), initiales,
  épreuve (Demolition), véhicule, peinture, course (points marqués, position 1/8 puis 3/8). Deux causes :
  - le jeu ne garde une manette DirectInput que si raw input liste un périphérique HID de même VID/PID dont le
    nom n'a pas « IG_ » (son test « est-ce une manette XInput ? », +0x961df0, rend −1 sans périphérique) : la
    carte de windowsloader est maintenant aussi un périphérique HID de raw input (GetRawInputDeviceList,
    GetRawInputDeviceInfo, pour les appels du jeu seulement : `vdinput.cpp`) ;
  - en jeu gratuit, Start n'ouvre une partie que si la coquille annonce un crédit : la coquille émulée en
    annonce un pour Showdown (`freeplayCredits`), Daytona garde 0.
- Retour de force ⚠️ : par Out32 sur le port 0x378 (port parallèle ; GRID : 0x1020), même codage : volant à
  gauche −0,33 en moyenne, à droite +0,47 (un ressort de centrage), 512 changements par course. vJoy ✅ :
  `--controllers`, course, 421 changements envoyés au joueur 1. Sens à confirmer au volant.
- Menu F10 ✅ : les cinq pages, jeu en pause tant qu'il est ouvert (images comptées), reprise à la fermeture.
- Affichage ✅ : 1280×720, vu à l'écran à 60 images/s (« FREEPLAY / PRESS START » sur la démo). La fenêtre
  enfant Direct3D 11 restait blanche sur ce PC (23 images/s) : l'image est maintenant lue dans le back buffer
  et recopiée par GDI (voir « Affichage sur ce PC »).
- Menu de service ❌ : coquille Sega Europe (`shell\Shell.exe`, clé Dinkey `DK2WIN32.DLL`).
- Crédits ✅ : free play.

## K.O. Drive (Sega RingWide, 2013)

`exe\M-DriveR_RingWide.exe` (la version 1080P, celle de `game.bat`), x86, Direct3D 9 ; `exe\TestMode.exe -t`
est le menu de service (`gametest.bat`). Module `src/hook/games/ringedge.cpp`, devenu une table de jeux
RingEdge / RingWide (Initial D 6 et K.O. Drive). Copie locale le 22/10 (1 min 26) ; plugin FFB, OutputBlaster,
le `d3d9.dll` et le `dinput8.dll` de TeknoParrot mis de côté.

Ce que la RingWide ajoute à la plateforme émulée :
- l'amLib plus ancienne veut la version 1 des pilotes (0x100 sur RingEdge), la taille de secteur de la
  SRAM, et le Super I/O de la carte : identifiant de puce Fintek et adresse de son moniteur matériel ;
- le JVS passe par le pilote `\\.\mxjvs` (une requête et sa réponse par DeviceIoControl), et sur COM4
  pour `TestMode.exe` ;
- le SMBus (`mxsmbus`, trouvé par SetupDi : la recherche d'interfaces de windowsloader n'est plus réservée
  à la carte HID) : l'EEPROM des réglages système (`wl_eeprom.bin` à côté du jeu) et le PCA9535 des
  DIP switches et des boutons Test / Service de la carte (actifs à 0) ;
- la table de la clé (`fs\table\SBXA_Table.dat`) manque dans les copies, TeknoParrot compris : le jeu reçoit
  le mot « toolmode » à la place de son chemin, la façon de Sega de s'en passer (amDongleSetAuthConfig) ;
- plateforme « AAM », fabricant « Supermicro » dans la table DMI.

- Lancement ✅ : initialisation, test de la carte de pilotage, démo, jeu.
- Commandes ✅ : un seul bouton Start / arme (octet 0 bit 1, vu dans le test d'entrées du menu de service),
  volant, accélérateur, frein, Service, Test, pièces ; course complète : choix du circuit, de la piste, de la
  voiture, des gadgets, course (`kod_race.py`).
- vJoy ✅ : `--controllers`, course complète, forces vers le joueur 1 (`kod_race.py wheel`).
- Retour de force ⚠️ : même carte que Initial D (trames de 4 octets, réponse lue sur 4 octets). Au démarrage
  la carte fait tourner le volant de chaque côté et le jeu s'arrête sur « Error 1001 » si la position ne suit
  pas : windowsloader joue un volant libre pendant ce test. En course : couples 0x84 (chocs) et vibrations
  0x85 rendus ; l'amortisseur 0x86 n'est que journalisé. Sens à confirmer au volant.
- Affichage ⚠️ : fenêtre, FPS et `[kodrive]` dans le titre, modes calculés (back buffer multiéchantillonné :
  l'image remplit la fenêtre en 4:3). Pas mesurable à l'écran sur ce PC à ce moment-là (une fenêtre Direct3D 9
  y restait noire, même un petit programme d'essai sans windowsloader) ; corrigé depuis par la recopie GDI,
  pas revu sur K.O. Drive (copie effacée).
- Menu F10 ✅ : pause, pages Jeu, Contrôles, Affichage, Retour de force.
- Menu de service ✅ : `TestMode.exe -t` : GAME TEST MODE, test d'entrées (volant, pédales, START/WEAPON,
  TEST, SERVICE), navigation par les boutons de la carte (`kod_race.py test`).
- Crédits ✅ : pièces JVS, crédit de service.

## Sonic & Sega All-Stars Racing Arcade (Sega RingWide, 2011)

`game.exe` et `gametest.exe` (le menu de service), x86, Direct3D 9, 1280×720. Dans la table RingEdge / RingWide
de `ringedge.cpp` (gameid SBVH), reconnu par `SBVH_Table.dat` à côté (les noms d'exe sont génériques).
Copie locale le 22/10 (2 min 12) ; le plugin FFB (chargé sous les noms `winmm.dll` et `dinput8.dll`),
OutputBlaster, SDL2, dgVoodoo et `teknoparrot.ini` mis de côté.

- Lancement ✅ :
  - clé tenue (table présente) ;
  - la carte du volant (« SteeringMecha », cherchée de COM1 à COM4) : une réponse par trame annoncée par
    ClearCommError, l'état 1 (0x11) après les réglages 81 et 82, et son test « KICKBACK INITIALIZE » joué par
    le volant libre (butées gauche et droite, 0x80 d'écart au moins, puis centrage) ; sinon le jeu prenait le
    port JVS (COM4) pour la carte et les commandes s'arrêtaient ;
  - le réseau : le client DHCP démarré par le jeu passe à l'état 3, la carte réseau a son lien
    (`query_nic_status` 0) ; « NETWORK FAILURE - PLEASE CONTACT OPERATOR » disparaît ;
  - le jeu se donne l'adresse 192.168.88.1 (borne n° 1) et y ouvre ses sockets de liaison : elles sont ouvertes
    sur 127.0.0.1, et ce qu'il envoie à ce réseau (son broadcast) est jeté (borne seule) ;
  - démo en boucle.
- Commandes ❌ : le JVS est lu (test d'entrées : START octet 0 bit 7, ITEM octet 1 bit 0, volant, accélérateur,
  pédale DRIFT), mais une partie ne démarre pas : en mode pièces, les pièces JVS ne sont pas comptées (la
  comptabilité reste à 0) ; en jeu gratuit, Start est ignoré et « PRESS START » ne s'affiche pas. Cause pas
  encore trouvée.
- Crédits ⚠️ : le jeu gratuit est l'octet 0x0A de l'enregistrement AM_SYSDATA_CREDIT de l'EEPROM (offset 0x20,
  CRC32 sur les octets 4 à 0x1F, copie en 0x220) ; posé à la main dans `wl_eeprom.bin` pour l'essai,
  « FREE PLAY » s'affiche.
- Retour de force ⚠️ : le test de la carte passe ; en course, pas encore vu.
- Affichage ✅ : fenêtre 1280×720 (`Settings.ini`), FPS et `[ssasr]` dans le titre ; vu à l'écran à 61 images/s
  avec la recopie GDI (noir à l'écran avant, 47 images/s).
- Menu de service ✅ : `gametest.exe` : GAME TEST MENU, test d'entrées, réglages du jeu, comptabilité,
  navigation par les boutons Test et Service de la carte.

## Battle Gear 4 Tuned (Taito Type X+, 2006)

`game.exe _MTS_FULL_SCREEN_` (la ligne de la borne), x86, Direct3D 9 (moteur RenderWare), 800×600. Nouveau
module `src/hook/games/taito.cpp`. Copie locale le 22/10 (4 449 fichiers, 4,6 Go, 11 min 45) ; plugin FFB
(`winmm.dll`, `dinput8.dll`), OutputBlaster, SDL2, dgVoodoo, le dossier `OpenParrot` et `teknoparrot.ini` mis
de côté. « Professional Edition » est une option de TeknoParrot (correctifs du jeu), pas un autre exe :
`game.exe` et `game4.exe` ne diffèrent que par la DLL qu'ils chargent (`JVSEmuBG` et `ttx_monitor.dll`, absentes).

- Lancement ✅ :
  - D:\ (réglages `config.bin`, sauvegardes) et E:\ (`E:\data`) sont le dossier du jeu (la redirection
    de dossiers de windowsloader en accepte maintenant plusieurs) ; GetLogicalDrives, GetDriveType et
    GetCurrentDirectoryA les montrent comme sur la borne (d'après OpenParrot) ;
  - le jeu quittait juste après la création de sa fenêtre : la barre de menu (LoadMenu) rétrécit la zone
    client, et la caméra RenderWare échoue ; le chargement du menu est neutralisé (correctif d'OpenParrot) ;
    correctif « IntroFix » d'OpenParrot posé aussi ;
  - JVS sur COM2 : ligne de détection inversée par rapport à Sega (CTS seulement une fois la carte
    adressée), commandes 0x33 (sorties analogiques) et 0x65 (Taito) ajoutées ; le jeu interroge la carte en
    continu ;
  - l'image noire après le chargement des polices (aucun dessin) : le démarrage attendait le son, qui ne
    s'initialise que si la fenêtre du jeu est au premier plan (GetForegroundWindow comparé à sa fenêtre) ;
    le jeu reçoit sa fenêtre pour ses propres appels (sa table d'imports : le clavier et le menu de
    windowsloader voient toujours la vraie) ;
  - puis le son lui-même : le jeu demande des haut-parleurs 7.1 et bâtit son format primaire avec autant de
    voies que GetSpeakerConfig en donne, dans un WAVEFORMATEX simple que DirectSound refuse au-delà de 2
    (DSERR_ALLOCATED, sur un PC réglé en 5.1 ou 7.1 comme celui-ci) ; il reçoit « stéréo » (son mélangeur fait
    alors deux voies mono, côté gauche et côté droit), et SetSpeakerConfig, réglage du PC, est ignoré
    (OpenParrot répond 7.1 et force le format en 2 voies) ;
  - JVS : commande 0x34 (texte de l'afficheur de la borne, « WAIT FOR A WHILE » / « OUT OF SERVICE ») ajoutée ;
    sans elle, le jeu fermait COM2 et recommençait le bus toutes les 4 s (« I/O ERROR » puis TEST MODE) ;
  - la carte du volant sur COM1 n'est pas celle de Sega : commandes de 2 octets, chacune suivie de 2 octets
    de réponse (protocole d'OpenParrot : 0x20 → A0 00, 0x11 → la position, 0x1F → 1F 00, sinon 8C A0 ; la
    position, volant 0..255 → 1024..2044 en gros-boutiste) ; le jeu y envoie ses réglages du moteur
    (12 18, 13 01, 0b 1f, 08 0f, 07 7f, 01 80…), journalisés pour le retour de force ;
  - le drapeau de pause d'OpenParrot (BG4General) tenu une fois l'écran 8 atteint ;
  - Notice, TXSURROUND, NOW LOADING, démo en 3D en boucle.
- Affichage ✅ : 800×600, vu à l'écran (recopie GDI) à 55 images/s.
- Commandes ✅ (mode agent, `scripts/agent/race_check.py bg4`) : pièce (jeu gratuit affiché), Start, constructeur, voiture, boîte
  AT, mode course, circuit, grille, départ, course (0 → 97 km/h, passage en 2e) ; le volant tourne la voiture du
  bon côté avec le protocole d'OpenParrot (1024 à gauche, 2044 à droite). Trouvé par le test d'E/S du menu de
  service : START bit 15, SERVICE 14, VIEW 13, HAZARD 12, OVER TAKE 11, SIDE BRAKE 9, SHIFT UP 8, SHIFT DOWN 7,
  SEAT SW1 / SW2 2 et 1, TIME START 0 ; accélérateur voie analogique 3, frein voie 4. Le jeu lit 3 octets
  d'interrupteurs (un troisième octet par joueur ajouté au JVS, vide ici). Nouvelles actions `Hazard` et
  `Overtake` dans `controls.ini`.
- JVS : commandes Taito 0x6D, 0x6E, 0x6F, 0x70 (le lecteur de clé d'entrée, dans le test d'E/S) répondues ; sans
  elles le jeu fermait COM2 et recommençait le bus toutes les 4 s, et les appuis se perdaient.
- Retour de force ⚠️ : la commande 0x0D de la carte du volant est le couple (0x80 nul, au-dessus vers la
  gauche) : volant à droite → +0,14 en moyenne (vers la gauche), un ressort de centrage. vJoy ✅ : `--controllers`,
  course, 229 changements de force envoyés au joueur 1 (effet constant créé sans erreur). Sens à confirmer au volant.
- Menu de service ✅ : TEST MODE (bouton Test), navigation par SHIFT UP / SHIFT DOWN, START pour choisir, test
  d'E/S, réglage des pédales (ADJUSTMENT).
- Crédits ✅ : pièce JVS, jeu gratuit.
- La boîte du pare-feu de Windows s'ouvre au lancement (le jeu écoute sur le port 8821 pour la liaison des
  bornes) : la réponse est à l'utilisateur.

## Chase H.Q. 2 (Taito Type X2, 2007)

`game.exe` (avec `chq2.ini`), x86, Direct3D 9 (RenderWare, back buffer 10 bits A2R10G10B10), 800×600.
Dans `taito.cpp`, avec Battle Gear 4. Copie locale le 22/10 (641 fichiers, 908 Mo, 1 min 53) ; plugin FFB,
OutputBlaster, SDL2, dgVoodoo, `OpenParrot`, `TeknoParrot` et `teknoparrot.ini` mis de côté.

- Lancement ⚠️ : D:\ redirigé (sauvegardes `rtb.dat`, `rtc.dat`, `compe_on.dat`) ; JVS sur COM2 (même
  détection Taito que Battle Gear 4), interrogé en continu ; l'étalonnage du volant au démarrage est sauté
  (correctif d'OpenParrot) ; les captures lisent maintenant les back buffers Direct3D 9 en 10 bits.
  Le jeu tourne à 12 images/s, image noire, puis plante à 36 s sur un modèle que sa fonction de chargement
  (+0x67c50) ne trouve pas, juste après avoir créé ses objets DirectPlay (liaison des bornes).
  - Corrigé : le jeu ouvre ses données par `E:.\data\…` (relatif au dossier courant du lecteur E:) ; la
    redirection d'un lecteur accepte maintenant ces chemins (polices, modèles) ;
  - le son 3D : à chaque image le jeu déplace l'auditeur DirectSound 3D et valide (CommitDeferredSettings) ;
    sur ce PC DirectSound 3D passe par le service audio pour chaque tampon (0,3 s pour créer un tampon 3D,
    plus d'une seconde par validation avec ses quarante tampons) : 1 image/s. Une validation de plus de
    100 ms arrête les suivantes (les sons restent où ils sont, le jeu garde sa vitesse) ; une 3D logicielle
    serait la vraie réponse ;
  - « PLEASE CALIBRATE THE I/O FIRST / PRESS TEST-SW » : les octets 12 et 13 de `rtc.dat` (réglages, chargé
    en 0x170bf28) disent l'étalonnage fait ; ceux de la borne (`OpenParrot
tc.dat` de TeknoParrot) le sont,
    le `rtc.dat` créé par le jeu sous windowsloader ne l'était pas. Fait dans le menu de service (ci-dessous).
- Affichage ✅ : 800×600, vu à l'écran (recopie GDI) à 60 images/s.
- Commandes ✅ (mode agent, `scripts/agent/race_check.py chq2`) : pièce (CREDIT(S) 1/1), Start, choix de la voiture, poursuite
  (jusqu'à 139 mph, « USE NITRO », HIGH/LOW). Test d'E/S : volant voie analogique 2, accélérateur 3, frein 4
  (le jeu lit 10 voies : le JVS en donne maintenant 10), START bit 3, SERVICE 14, levier SHIFT bit 13 (mis =
  LOW ; GearDown le met en LOW, GearUp en HIGH, il reste où on l'a mis), PATO-NITRO bit 2 (action `Nitro`),
  SHIFT-NITRO bit 12 (`ShiftNitro`).
- Retour de force ⚠️ : le moteur du volant est dans les sorties générales (0x32) : couple 0..15 sur quatre bits
  (octet 0 0x10, octet 1 0x40, octet 0 0x08, octet 1 0x20) et le sens (octet 0 0x20), trouvé par le test du
  moteur du menu de service (les deux boutons nitro tenus : 0 à 15 et retour). En course : volant à gauche
  −13,7 en moyenne, à droite +14,8 (un ressort de centrage), et des impulsions 15/0 (vibrations). vJoy ✅ :
  `--controllers`, course, 219 changements envoyés au joueur 1. Sens à confirmer au volant.
- Menu de service ✅ : TEST MODE, navigation SHIFT (haut) et PATO-NITRO (bas), START ; test d'E/S ;
  étalonnage du volant (deux phases minutées, LEFT puis RIGHT : la position est lue, le moteur n'est pas
  commandé, on tourne le volant soi-même) et des pédales (« PRESS AND HOLD… », « RELEASE… »), SAVE AND EXIT :
  « CALIBRATION SUCCESS », puis le jeu démarre normalement (NOTICE, démo).
- Crédits ✅ : pièces JVS.
- Pas de carte de volant sur un port série : son retour de force passe sans doute par les sorties
  analogiques du JVS (commande 0x33, 6 voies).

## Affichage sur ce PC (Direct3D 9 et 11)

Sur ce portable (Windows 11, Intel et NVIDIA), un Present Direct3D en mode « blt » dans une fenêtre ne montre
rien à l'écran : D3D9 DISCARD ou COPY, avec ou sans MSAA, sur l'une ou l'autre carte, et DXGI / D3D11 de même
(la fenêtre enfant de DiRT Showdown restait blanche). Un petit programme d'essai sans windowsloader fait pareil
(`d3d9red`, modes gdi, windowed, copy, ex) ; seuls le modèle FLIPEX de D3D9Ex et le dessin GDI s'affichent.

windowsloader recopie donc l'image par défaut (`[Display] PRESENT = GDI`) : le back buffer est lu à chaque
image (résolution du MSAA, formats 32 bits, 10 bits A2R10G10B10 convertis, 16 bits), puis dessiné par
StretchDIBits à sa place dans la fenêtre du jeu (aspect et mode compris) ; le Present du jeu part vers une
fenêtre cachée (D3D9) ou vers la fenêtre enfant, cachée (D3D11), et la cadence suit DwmFlush, une fois par
intervalle demandé. `PRESENT = DIRECT` revient au Present du jeu. Vérifié à l'écran : DiRT Showdown (60 i/s),
Sonic (61), Battle Gear 4 (55), Chase H.Q. 2 (60).

## Menu F10 sur Battle Gear 4, Chase H.Q. 2 et DiRT Showdown

`f10_check.py` (bloc-notes) : menu ouvert par l'agent, pages Jeu, Contrôles, Affichage, Retour de force, À propos
vues sur chaque jeu ; le compteur d'images ne bouge pas tant que le menu est ouvert (882 → 882, 903 → 903,
1119 → 1119) et repart à la fermeture.
