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
| Battle Gear 4 Tuned Pro (même dossier, `[Game] CABINET = PRO`) | ✅ | ✅ | ⚠️ | ⚠️ | ✅ | ⚠️ | ✅ | ✅ |
| Chase H.Q. 2 (Taito Type X2) | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ✅ |
| GTI Club: Supermini Festa! (GTI Club 3, Konami PC) | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ✅ |
| Wasteland Racers 2071 (Trio Tech PC) | ✅ | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ |
| Mario Kart Arcade GP DX 1.18 (Namco ES3) | ⚠️ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |
| Battle Gear 4 (Taito Type X+, 2006) | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |
| D1GP (Taito Type X2) | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |
| Valve Limit R (Taito Type X) | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |

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
  Les réglages de linuxloader en plus (voir « Rapport d'aspect, recadrage et courbure » plus bas).
- **Menu F10** : pause, pages Jeu, Contrôles, Affichage, Retour de force, À propos ; boutons « Sauvegarder »
  et « Sauvegarder pour ce jeu ».
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
- Affichage ✅ : 800×600, vu à l'écran (recopie GDI) à 55 images/s. Le jeu refait son device à la taille de
  sa fenêtre (`GetClientRect` après `WM_SIZE`) : il voit sa fenêtre à la taille de son image, sinon un rapport
  d'aspect autre que le sien faisait grandir la fenêtre à chaque tour.
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

### Battle Gear 4 Tuned Pro (borne « Professional »)

Le même dossier et le même `game.exe` : la borne Pro est reconnue par le jeu lui-même, qui compte les cartes
JVS du bus (+0x60cb37) ; deux cartes, c'est une borne « WIDE MONITOR & CLUTCH TYPE » (écran large,
embrayage, levier en H). Dans windowsloader : `[Game] CABINET = PRO` (`--set Game.CABINET=PRO`, ou dans
`bg4.ini`), qui met une seconde carte sur le bus JVS (nœud 2 ; `jvs.cpp` en accepte maintenant deux).

- Lancement ✅ : au premier démarrage après un changement de `CABINET`, le jeu réécrit son `config.bin`
  (deux octets, 0x74 et 0x75, et sa somme) et se ferme seul après 3 s : relancer. Sans remise à zéro usine.
- Affichage ✅ : 1360×768 (la largeur 1366 d'OpenParrot, pour l'écran de la borne, n'est pas reprise :
  windowsloader met l'image à la taille de sa fenêtre).
- Commandes ✅ (mode agent) :
  - levier en H, actions `Gear1` à `Gear6` (point mort quand aucune n'est tenue) : le jeu (+0x48adf2) lit
    les rangées sur SHIFT UP (bit 8 : 2, 4, 6) et SHIFT DOWN (bit 7 : 1, 3, 5), actives à l'état bas sur
    cette borne, et les colonnes sur la seconde carte (entrées générales, commande 0x26 : 0x80 gauche pour
    1 et 2, 0x40 droite pour 5 et 6). Course en « 6速 MT » : rapports 1 à 5 vus au tableau de bord, 86 km/h ;
    la 6e position est la marche arrière pour une voiture à 5 rapports (point mort en roulant) ;
  - embrayage, action `Clutch` (`P1_Clutch` dans `controls.ini`) : voie analogique 6, lue seulement en course
    (+0x41e860) et étalonnée par ADJUSTMENT (accélérateur, frein, puis embrayage). Vérifié : pédale enfoncée,
    plein gaz en 1re, la voiture reste à 0 km/h ; relâchée, 24 puis 64 km/h ;
  - la grille du levier est motorisée (séquentielle pour AT et MT, en H pour 6速 MT) : le jeu la pousse par
    les sorties 0x80 et 0x40 de la seconde carte, 4 s chacune à tour de rôle (+0x41f098), jusqu'à ce que ses
    capteurs « SHIFT SEN1 » (0x20) ou « SEN2 » (0x10), actifs à l'état bas, donnent la position voulue
    (+0x41df70, +0x41e0a0). La grille émulée va où la dernière commande l'envoie : changement immédiat (sans
    cela, « シフト変更中 » pendant 40 s, puis « remettez le levier au centre ») ;
  - ShiftUp / ShiftDown donnent les rangées seules : AT et MT séquentielle, et la sélection du menu de
    service (« ITEM SELECT: SHIFT-SW ») ;
  - `[Game] HOLD_GEAR = true` : le rapport reste engagé levier relâché (« Hold Gear » d'OpenParrot, 10 octets
    à +0x8adff), pour un levier dont les positions ne tiennent pas ; vérifié en course (2 puis 3, levier lâché).
- La carte du nœud 1 reçoit en plus la commande Taito 0x66 (2 octets, sens inconnu, répondue).
- Retour de force ⚠️ : le même qu'en Tuned (330 changements envoyés pendant une course AT).
- Menu de service ✅ : test d'E/S (SHIFT LEFT/RIGHT, SEN1/SEN2, CLUTCH en plus), ADJUSTMENT avec l'embrayage.
- Restent : vJoy et le menu F10 non refaits en Pro (le même code qu'en Tuned) ; l'identité de la seconde
  carte est celle de la première (celle de la vraie borne inconnue) ; le côté de chaque capteur de la grille
  est supposé (le jeu s'en accommode : il pousse dans les deux sens).

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

## Rapport d'aspect, recadrage et courbure ; fichier de réglages par jeu

Les réglages de linuxloader, mêmes noms et même sens (le front-end les passe pareil aux deux) :

| Réglage | Recopie GDI (défaut) | `PRESENT = DIRECT`, D3D9 | `PRESENT = DIRECT`, D3D11 |
|---|---|---|---|
| `ASPECT_RATIO` ORIGINAL, 4:3, 16:9, 4:3_LETTERBOX | ✅ | ✅ | ✅ |
| `4:3_STRETCH`, `16:9_STRETCH` | ✅ | ✅ | ✅ |
| `4:3_CROP`, `16:9_CROP`, `CROP_POSITION` | ✅ | ✅ (partie du back buffer donnée au Present) | ⚠️ image entière entre bandes noires |
| `CROP_BARREL`, `BARREL_CENTER`, `BARREL_RADIUS` | ✅ | ⚠️ recadré sans courbure | ⚠️ comme ci-dessus |
| `<nom court>.ini` lu à la place de `windowsloader.ini` | ✅ | ✅ | ✅ |
| `-c fichier.ini`, `--set SECTION.CLE=VALEUR` (répétable) | ✅ | ✅ | ✅ |

- Menu F10, page Affichage : les sept choix de linuxloader, « Position du recadrage », « Courber les parties
  coupées sur les bords », « Milieu non déformé », « Rayon de courbure », appliqués à l'image suivante ; en bas
  du menu « Sauvegarder » (le fichier lu), « Sauvegarder pour ce jeu » (la première fois une copie du fichier lu
  en `<nom court>.ini`, puis ce fichier), « Fermer ». Le menu reste entier au-dessus d'une image recadrée ou
  courbée.
- Vérifié (captures de la fenêtre) sur GTI Club 3 (D3D9, 16:9), Battle Gear 4 (D3D9, 4:3) et DiRT Showdown
  (D3D11, 16:9) : les 16 cas de `crop_check.py` (bloc-notes) et le menu ; `--set`, `-c`, le fichier du jeu
  créé, relu au lancement suivant.
- Non fait : OpenGL (Initial D 6) garde sa fenêtre telle quelle, sans aucun de ces réglages.

## Menu F10 sur Battle Gear 4, Chase H.Q. 2 et DiRT Showdown

`f10_check.py` (bloc-notes) : menu ouvert par l'agent, pages Jeu, Contrôles, Affichage, Retour de force, À propos
vues sur chaque jeu ; le compteur d'images ne bouge pas tant que le menu est ouvert (882 → 882, 903 → 903,
1119 → 1119) et repart à la fermeture.

## GTI Club: Supermini Festa! (GTI Club 3, Konami PC, 2008)

`gti3.exe` (celui de `boot.bat` ; `gti3japkey.exe` n'en diffère que par 6 octets de texte), x86, Direct3D 9
(1360×768), son XACT 2.10, base fixe 0x400000. Nouveau module `src/hook/games/konami.cpp`. Copie locale le 22/10
(121 fichiers, 3,15 Go, 3 min 29, 16 Mo/s) ; plugin FFB (`dinput8.dll`), OutputBlaster, SDL2, dgVoodoo et
`teknoparrot.ini` mis de côté.

- Lancement ✅ :
  - la carte d'E/S Konami (« HBHI », protocole ACIO sur COM2) passe par `libacio.dll`, l'original : elle n'est
    pas émulée ; comme OpenParrot (`TypeX2/GTIClub3.cpp`, GPL v3), les quatre vérifications de la carte au
    démarrage sont déclarées réussies et les entrées écrites là où le jeu les garde, à chaque échange avec la
    carte (+0x409c0, une fois par image) ; sans cela : « ERROR CODE : 5 1500-0000, FATAL ERROR » ;
  - les écritures se font après l'échange, et le drapeau « entrées actives » (+0xa7e75b) est remis à chaque
    fois : la remise à zéro des E/S du menu de service (+0x440660) l'éteint ;
  - les lecteurs de mise à jour E:\UP et F:\UP sont les dossiers EUP et FUP de la copie (OpenParrot réécrit les
    chaînes ; ici une redirection de dossiers) ;
  - le moteur de son XACT est déjà enregistré sur ce PC (runtime DirectX) : `boot.bat` le fait par
    `regsvr32` sur la borne ; windowsloader ne touche pas au registre (à prévoir : activation COM sans
    registre si une borne ne l'a pas) ;
  - le jeu écoute sur le réseau (ports 50002 et 50006) : une boîte du pare-feu s'ouvre, laissée à l'utilisateur.
- Affichage ✅ : fenêtre 1360×768 (recopie GDI), 30 images/s.
- Commandes ✅ (mode agent, `scripts/agent/race_check.py gticlub3`) : jeu gratuit, « Vous jouez ? YES », choix du
  mode (Race battle), départ rapide, course « Traffic jam escape race » (jusqu'à 131 km/h, la voiture suit la
  route). Bits du mot des boutons trouvés par l'INPUT CHECK du menu de service : SERVICE 0x1, TEST 0x2, COIN MECH
  SWITCH 0x4, START 0x10, levier SHIFT DOWN 0x80 et SHIFT UP 0x100, ACTION 0x800, SELECT UP / DOWN / LEFT / RIGHT
  0x1000 à 0x8000 ; volant et pédales en flottants (et en boutons pour les menus : 0x200, 0x400, 0x20, 0x40).
  Nouvelles actions `Action`, `SelectUp`, `SelectDown`, `SelectLeft`, `SelectRight` dans `controls.ini`.
- Retour de force ⚠️ : l'octet que le jeu prépare pour le moteur (+0x518cbc, lu par FFBArcadePlugin) : 0 ou 0x80
  rien, 0x81 (le plus fort) à 0xFF d'un côté, 0x7F à 0x01 de l'autre ; rien en menu (+0x518e8e). Volant à gauche
  −0,65 en moyenne, à droite +0,67 (un ressort de centrage). vJoy ✅ : `--controllers`, course, forces envoyées au
  joueur 1. Sens à confirmer au volant.
- Menu F10 ✅ : les cinq pages, jeu en pause.
- Menu de service ✅ : bouton Test ; MAIN MENU, I/O CHECK (INPUT CHECK, CALIBRATION…), navigation SELECT UP /
  DOWN, START pour choisir. L'étalonnage du volant et des pédales y est marqué à faire (pas fait : le jeu tourne
  sans).
- Crédits ✅ : jeu gratuit (réglage de la copie) ; les pièces passent par `ac_io_hbhi_current_coinstock` de la
  carte, pas émulé.

## Wasteland Racers 2071 (Trio Tech PC, 2006)

`Arcade.exe`, x86, Direct3D 8 (2560×1440, `D3DLocalConfigData.txt`), son fmod, moteur Diesel de Grin. Nouveau module
`src/hook/games/triotech.cpp`. Copie locale le 22/10 (4 860 fichiers, 1,66 Go), interrompue par un redémarrage puis
terminée ; rien de la copie n'est modifié (le `dinput8.dll` de FFBArcadePlugin reste dans le dossier).

- Lancement ✅ :
  - la carte de la borne (volant à retour de force, pédales, boutons, monnayeur, sur COM3) passe par `SDC2.dll`,
    que le jeu charge et dont il demande les fonctions par GetProcAddress : le GetProcAddress du jeu donne celles
    de windowsloader, la carte n'est jamais ouverte (`Init` ne rend pas −1 : « Force feedback board disconnected »
    sinon) ;
  - le `dinput8.dll` du dossier est celui de FFBArcadePlugin (`FFBPlugin.ini` : l'appareil vJoy) : le lanceur
    réécrit l'import du jeu vers le `dinput8.dll` de Windows avant son démarrage (règle générale : un composant
    de Windows trouvé dans le dossier du jeu est le relais d'un autre loader) ;
  - le jeu ouvre la première manette DirectInput qu'il trouve (vJoy ici) : un volant « Trio Tech Force Feedback
    Wheel » de windowsloader est le seul listé ;
  - Direct3D 8 joué sur Direct3D 9 par d3d8to9 (voir plus bas) ; `Backup.bat` lancé par le jeu, refusé ;
  - les bornes élisent un serveur par diffusion sur UDP 2070 (« Finding Server... ») : ses diffusions vont à
    127.0.0.1, la borne s'entend elle-même et devient serveur ; rien ne sort du PC ;
  - le jeu écoute sur le réseau : une boîte du pare-feu s'ouvre, laissée à l'utilisateur ;
  - au démarrage, environ 90 s de vidéo d'introduction (DirectShow) avant la première image.
- Affichage ✅ : recopie GDI de l'image Direct3D 9 que d3d8to9 fait, fenêtre 2042×1149 sur ce portable,
  60 à 90 images/s. ⚠️ Les vidéos (DirectShow, son propre appareil Direct3D 9, laissé tel quel) ne se voient pas
  sur ce PC (un Present en mode blt n'y montre rien) : écran noir pendant l'attraction vidéo.
- Commandes ✅ (mode agent, `crop`/`observe` du bloc-notes) : Boost à l'écran titre → « Finding Server » → choix de la
  voiture (Destroyer) → choix du circuit (Airport Mayhem) → « Get Ready To Race » (environ 60 s de chargement) →
  course : 75 à 106 km/h, 6e à 8e, la voiture va à gauche volant à gauche, à droite volant à droite. Les boutons
  de `GetDigitalIn` sont actifs à l'état bas (le jeu inverse le mot, +0x136e7d) : 0x0000 lui faisait voir tous les
  boutons tenus. Bits (masques lus dans l'objet du jeu, d'après `Inputs.xml`) : BOOST 0x1, CAMERA 0x2, CHEAT1 à 4
  0x4 à 0x20, GEAR 0x80, COIN 0x100, MAINTENANCE 0x800, STOP 0x1000 (arrête le mouvement du siège ; `arcade.dll`
  plante quand il est tenu). Axes de `GetAxisData` en −1..1 : volant (gauche négatif) ; les pédales à l'envers,
  1 relâchée, 0 enfoncée (le jeu prend 1 − valeur, +0x13dcfc) : avec 0 au repos il voyait accélérateur et frein
  enfoncés ensemble (12 km/h au plus).
- Retour de force ✅ : le moteur est un asservissement en position, `SetForce` la raideur et `SetSteeringPos` la
  position tenue (secousses : ±30 464 en alternance) ; joué comme un ressort : volant à gauche 0,4 → −0,39 (pousse
  à droite), à droite 0,8 → +0,78 ; en course 176 changements en 25 s (secousses ±0,91). vJoy ✅ (`--controllers` :
  le joueur 1 est vJoy, les forces lui sont envoyées).
- Menu F10 ✅ : les cinq pages, jeu en pause (images 143 → 143), reprise à la fermeture.
- Menu de service ✅ : MAINTENANCE (bouton Test) : étalonnage (BOOST, lance `CalibrationStarter.exe`, refusé, et
  quitte le jeu), prix de la partie (VIEW), jeu gratuit, statistiques.
- Crédits ✅ : jeu gratuit (réglage de la copie) : le jeu met le prix d'une partie dans le compteur de la carte
  (`SetCoin(8, …)`, le compteur du bit COIN) puis le relit (`GetCoin(8)`) ; une pièce compte une fois par appui.

## Mario Kart Arcade GP DX 1.18 (Namco System ES3), commencé

`MK_AGP3_FINAL.exe`, x86 relogeable, Direct3D 10 et 9Ex. Module `src/hook/games/namco.cpp`. Le jeu démarre et dessine
(noir) à 60 images/s en fenêtre 1360×768. Fait : volant DirectInput de windowsloader (vJoy caché), client ShareK non
démarré (classe COM non enregistrée), lecteurs F:\ et G:\ vers les `F*`/`G*` de la copie. Voir NOTES.md pour les
garde-fous ajoutés (caméra, ports série, routes, WinHTTP).

Retrouvé dans l'exe (23/10), émulé, pas encore essayé (plus d'écran sur le PC) :
- la carte JVS est sur **COM3** à 115 200 bauds, ouverte par la bibliothèque JVS du jeu (son hôte « NBGI.;WinArc;Ver2.01;JPN »)
  une fois un chargement fini ; CTS bas tant qu'une carte n'a pas d'adresse ;
- **COM1** à 19 200 bauds est la carte du moteur du volant : elle annonce « C01 » (prête), le jeu lui envoie alors toutes
  les 8 ms une position visée sur 14 bits, un ressort, un amortisseur et une vibration ; joués ici en ressort vers la
  cible plus la vibration (sens à vérifier).

## Battle Gear 4 (Taito Type X+, 2006), D1GP (Taito Type X2) et Valve Limit R (Taito Type X)

Copiés le 23/10 ; pas encore lancés avec un écran (le PC n'en a plus eu à partir de 3 h).

- Battle Gear 4 de 2006 : un autre exe que Tuned ; taito.cpp le reconnaît par l'horodatage de l'exe et pose ses propres
  correctifs (barre de menu, IntroFix, aux adresses de cette édition ; repris d'OpenParrot, « BG4_Eng ») ; la borne Pro
  n'existe que pour Tuned. Les deux éditions ouvraient la première manette DirectInput en exclusif (vJoy) : elle leur est
  maintenant cachée.
- D1GP : JVS sur COM2 ; volant, pédales et boutons par DirectInput, réglés par un fichier `<nom du produit>.txt` ;
  retour de force par Immersion (effets DirectInput). Le volant DirectInput de windowsloader sait maintenant jouer des
  effets (ressorts, forces constantes, rampes, périodiques) : prévu pour lui.
- Valve Limit R : `launcher.exe`, Direct3D 9, COM1 et COM2.
- Garde-fous ajoutés pour eux : adresses IP du PC (AddIPAddress, DeleteIPAddress), suppression de clés du registre,
  HKEY_CLASSES_ROOT et HKEY_USERS en lecture seule comme HKLM.

## Direct3D 8 (d3d8to9)

windowsloader joue Direct3D 8 sur Direct3D 9 avec d3d8to9 de crosire (`src/d3d8to9`, licence BSD 2 clauses,
version 255338f du 23/09/2026), compilé dans la DLL 32 bits (Direct3D 8 n'existe qu'en 32 bits). Seule
modification : son point d'entrée reçoit l'IDirect3D9 que windowsloader crée, et n'affiche plus de boîte ni de page
web quand `d3dx9_43.dll` manque (le journal le dit). L'appareil prend alors le chemin Direct3D 9 de windowsloader
(recopie GDI, modes d'affichage, recadrage, F10, images de l'agent). `WINDOWSLOADER_D3D8=native` garde le Direct3D 8
de Windows (fenêtre gardée, images comptées, rien à l'écran sur ce PC).
