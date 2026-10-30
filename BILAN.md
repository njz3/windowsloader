# Bilan des jeux sous windowsloader

Ce qui marche, jeu par jeu et fonction par fonction, rempli au fur et à mesure des jeux traités
(copie locale depuis racecab1, module, essais, copie effacée). Le détail des essais et des trouvailles est
dans [NOTES.md](NOTES.md) (journal).

Légende : ✅ fait et vérifié · ⚠️ fait, vérifié en partie (mode agent, sans volant physique…) · ❌ pas
possible ou absent du jeu · ⏳ pas encore fait · — sans objet.

Ordre de traitement : jeux Sega en 32 bits avec retour de force d'abord (liste de la session Image borne
dans NOTES.md, « Jeux des bornes »).

## Vue d'ensemble

| Jeu | Lancement | Commandes | vJoy | Retour de force | Affichage | Menu Tab | Menu de service | Crédits / Shell |
|---|---|---|---|---|---|---|---|---|
| Sega Rally 3 (Europa-R) | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ❌ | — |
| Daytona Championship USA | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ |
| Daytona Championship USA NSE | ✅ | ✅ | ⚠️ | ✅ | ⚠️ | ✅ | ❌ | ✅ |
| Ford Racing Full Blown | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ⚠️ | — |
| Initial D Arcade Stage 6 AA | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ✅ |
| Initial D Arcade Stage 7 AAX | ✅ | ✅ | ⏳ | ⚠️ | ⏳ | ⏳ | ✅ | ✅ |
| Initial D Arcade Stage 8 Infinity | ✅ | ✅ | ⏳ | ⚠️ | ⏳ | ⏳ | ⏳ | ✅ |
| GRID | ✅ | ✅ | ✅ | ✅ | ⚠️ | ✅ | ❌ | ✅ |
| DiRT Showdown | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ❌ | ✅ |
| K.O. Drive (RingWide) | ✅ | ✅ | ✅ | ⚠️ | ⚠️ | ✅ | ✅ | ✅ |
| Sonic & Sega All-Stars Racing (RingWide) | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ✅ |
| Battle Gear 4 Tuned (Taito Type X+) | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ✅ |
| Battle Gear 4 Tuned Pro (même dossier, `[Game.bg4] CABINET = PRO`) | ✅ | ✅ | ⚠️ | ⚠️ | ✅ | ⚠️ | ✅ | ✅ |
| Chase H.Q. 2 (Taito Type X2) | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ✅ |
| GTI Club: Supermini Festa! (GTI Club 3, Konami PC) | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ✅ |
| Wasteland Racers 2071 (Trio Tech PC) | ✅ | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ |
| Mario Kart Arcade GP DX 1.18 (Namco ES3) | ✅ | ✅ | ⏳ | ⚠️ | ✅ | ✅ | ✅ | ✅ |
| Mario Kart Arcade GP DX 1.10 (Namco ES3, mod français) | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ✅ |
| Battle Gear 4 (Taito Type X+, 2006) | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ⚠️ |
| D1GP Arcade (Taito Type X2) | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ⚠️ |
| Valve Limit R (Taito Type X) | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ✅ |
| Crazy Ride (UNIS PC, 64 bits) | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ✅ |
| Wangan Midnight Maximum Tune 5 (Namco ES3X, 64 bits) | ✅ | ✅ | ⏳ | ✅ | ⏳ | ⏳ | ✅ | ⚠️ |
| Wangan Midnight Maximum Tune 5 DX+ (Namco ES3X, 64 bits) | ✅ | ✅ | ⏳ | ✅ | ⏳ | ⏳ | ✅ | ⚠️ |
| Wangan Midnight Maximum Tune 6 (Namco ES3X, 64 bits) | ✅ | ✅ | ⏳ | ✅ | ⏳ | ✅ | ✅ | ⚠️ |
| Sega World Drivers Championship (ALLS, 64 bits) | ✅ | ⚠️ | ⚠️ | ⚠️ | ⚠️ | ✅ | ⚠️ | ⚠️ |
| Sega Racing Classic (RingEdge) | ✅ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ✅ | ✅ |
| Storm Racer G (Sega Arcade PC) | ✅ | ⚠️ | ⏳ | ⚠️ | ⚠️ | ✅ | ⏳ | ✅ |
| Wacky Races (Taito Type X2) | ✅ | ✅ | ⏳ | ⚠️ | ⚠️ | ✅ | ✅ | ⚠️ |
| Let's Go Safari (RingWide) | ✅ | ✅ | ⏳ | ⚠️ | ✅ | ✅ | ✅ | ✅ |

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
- **Menu Tab** (F10 jusqu'au 24/10/2026, Tab comme Flycast, linuxloader et Supermodel) : pause, pages Jeu, Contrôles, Affichage, Retour de force, À propos ; boutons « Sauvegarder »
  et « Sauvegarder pour ce jeu ». En français ou en anglais (28/10, `[Launcher] LANGUAGE`, catalogues
  `lang\<code>.po`), comme le sélecteur ; vu en anglais sur Sega Rally 3 et Mario Kart DX 1.18.
- **Menu de service** : le menu de test de la borne (bouton Test).
- **Crédits / Shell** : pièces, crédits et ce que le Shell de la borne fournit au jeu.

## Sega Rally 3 (Sega Europa-R, 2008)

`Rally\Rally.exe`, x86, Direct3D 9. Module `src/hook/games/segarally3.cpp`. Copie locale effacée
(Corbeille) le 22/10 ; l'édition Standard recopiée de racecab1 le 27/10 (`games/segarally3`).

- Commandes ✅ : carte d'E/S USB HID émulée (VID 0x1043), volant, pédales, Start, vue, frein à main,
  rapports + / − (`sr3_bits.py`, `sr3_pedals.py`, `sr3_race_bits.py`, `sr3_handbrake.py`).
- vJoy ⚠️ : ouvert par un lancement normal, retour de force vers lui ; pas repris en course depuis.
- Pédales par vJoy (28/10, le controls.ini des bornes : accélérateur sur Y-, frein sur Z-, du milieu relâché à 0
  enfoncé) : la carte reçoit 40 au repos, 124 à mi-course, 210 à fond (l'étalonnage de `ShellData.ini`). En course,
  boîte AT : à demi-accélérateur la voiture reste en 1re (65 km/h au rupteur), à fond elle passe jusqu'en 4e ; le frein
  ralentit jusqu'à l'arrêt, puis, gardé, passe en R et la voiture recule (la logique du jeu : le rapport de la carte
  passe par le même code qu'une vraie, octet 3 frein, octet 4 accélérateur). Sur les bornes, la voiture reculait au
  départ : le jeu y reçoit un frein enfoncé, l'axe Z n'y est pas au milieu au repos (à vérifier, page Contrôles).
- Retour de force ✅ : port 0x1020 par inpout32 (émulé, la vraie DLL jamais chargée), sens vérifié
  (`sr3_ffb.py`).
- Affichage ✅ : 16 couples aspect × mode et fenêtres réduites justes à l'écran ; résolution interne
  (`config.ini`) vérifiée en 1280×720.
- Menu Tab ✅ (`sr3_menu.py`).
- Menu de service ❌ : il appartient au Shell Europa-R, absent des copies.

## Daytona Championship USA (2017) et New Season Edition (2019)

`Sega\Daytona\Daytona.exe`, x86, Direct3D 11. Module `src/hook/games/daytona.cpp`. Copies locales
effacées (Corbeille) le 22/10. Noms courts `daytona3` et `daytona3nse`
(ceux des bornes).

- Commandes ✅ : manette DirectInput « SEGA IO Controller » jouée par windowsloader ; volant (sens
  vérifié), accélérateur, frein (57 → 18 mph en 2 s), Start, rapports + / −, levier en H et vues câblés
  (`day_race.py`, les deux éditions).
- vJoy ✅ (originale) : `--controllers`, course pilotée par le script, effet de force constante lancé sur
  vJoy et mis à jour (`day_race.py wheel`). NSE ⚠️ : même code, non refait avec vJoy.
- Retour de force ⚠️ : InpWrapper.exe (port parallèle) joué par une fenêtre cachée, octet décodé, sens
  vérifié dans les menus et en course. Sur la borne, le volant oscillait de −90° à +90° vers 1 Hz : le
  centrage du jeu, fait de forces constantes, monte à 0,86 en 14 % de la course (config.ini de la
  borne). Désormais (`[Game.daytona3] FFB = BOARD`, défaut) la carte moteur de Daytona USA est jouée
  comme sm2-emu la joue, d'après la commande Model 2 lue dans le jeu et la position du volant, avec le
  ressort du panneau (options `DRIVE_DIP`, `PANEL_SPRING`) ; `PORT` pour l'ancien octet. Adresses
  vérifiées dans les deux exécutables des bornes ; pas encore essayé à la borne. Sorties réseau :
  `RawDrive` (la commande) et les lampes Model 2.
- Affichage ✅ (originale) : 16 couples et fenêtres réduites justes à l'écran (fenêtre enfant pour
  l'image Direct3D 11). NSE ⚠️ : même code, non vérifié à l'écran.
- Menu Tab ✅ (Direct3D 11).
- Menu de service ❌ : le menu de test est celui de `Shell.exe`, non émulé.
- Crédits / Shell ✅ : Shell émulé (TCP 127.0.0.1:18888) : crédits, défi de sécurité, SKU (la NSE
  l'attend).
- Link entre bornes 🔄 (30/10, `[Game.daytona3] LINK` et `CABINET`, voir le README) : le jeu cherche ses bornes
  sur 192.168.1.0/24, en dur (`src\soc\ping.cpp`) : son adresse est le dernier octet de
  `gethostbyname(gethostname())`, la borne 1 (`CabinetID`) est le maître, elle essaie 192.168.1.1 à 8 (sauf la
  sienne) en TCP 26000, chaque autre borne y répond avec son `CabinetID`, puis le maître envoie la liste des
  adresses ; la course passe ensuite par un battement de cœur (TCP), le classement (TCP 5888), une diffusion UDP sur
  192.168.1.255 et les photos de la caméra (ImageComm ; sans caméra, la capture est coupée sans erreur).
  windowsloader : la borne N est 192.168.1.N pour le jeu, ses échanges vont aux PC de `LINK` (gethostbyname, bind,
  connect, sendto, recvfrom, accept, getpeername, getsockname de sa table d'importation) ; NumCabinets et CabinetID
  lus d'après `LINK` et `CABINET`. Essayé sur ce PC avec un faux Daytona.exe qui fait ces appels (deux jeux,
  127.0.0.1 et 127.0.0.2) : chaque borne a son adresse, le maître trouve la borne 2 et reçoit son CabinetID, les
  diffusions passent dans les deux sens. Sur les bornes (30/10, Image borne, racecab1 et 2) : la découverte marche,
  « POWER-ON TEST NETWORK » montre LINK ID 1 et 2 [GOOD], TOTAL MACHINE 2, puis chaque borne perd l'autre : la
  caméra refusée au jeu (le propriétaire avait le même blocage sous TeknoParrot sans webcam ; les deux bornes en
  ont une). Désormais le jeu en link a la caméra du PC (`[Emulation] CAMERA = true` pour les autres jeux), `TOTAL
  CABINETS` de `GameSettings.ini` suit `LINK`, et le journal compte le trafic avec chaque borne toutes les 10 s
  (paquets UDP, octets TCP, connexions TCP fermées). Deuxième essai : même écran. Les journaux montrent que la borne 1
  charge bien plus lentement (25 à 40 images/s, figée 5,8 s ; la borne 2 à 60) : la borne 2 attend son battement de
  cœur (TCP 25000) dès 40 s, la borne 1 seulement à 57 s ; reliées, la borne 1 n'envoie presque rien pendant 12 s et
  chacune coupe vers 70 s, sans jamais rouvrir ; ensuite les données de course passent dans les deux sens (~60
  paquets/s), la traduction des adresses tient. Les images de la caméra partent en diffusion vers 192.168.1.255:3167 +
  numéro de borne. Les messages du jeu étaient perdus (2000 lignes de débogage prises par ses chargements `.nrfc`,
  désormais écartées).
- À part : réseau local refusé hors link, caméra refusée, carte graphique dédiée choisie sur un portable.

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
- Menu Tab ✅ (pages Contrôles et Retour de force vues en course).
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
- les pilotes `\\.\mxsram` (SRAM dans `wl_sram.bin`), `mxsuperio`, `mxsmbus` (EEPROM dans `wl_eeprom.bin`)
  et `columba` (une table DMI fabriquée : plateforme « AAL ») ;
- l'enregistrement système AM_SYSDATA_STATIC de l'EEPROM (région, numéro de la carte), écrit par windowsloader
  quand il est vide ou abîmé (depuis Sonic, le 23/10 ; Initial D 6 et K.O. Drive pas relancés depuis, leurs
  copies effacées) ;
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
- Affichage ✅ (27/10) : OpenGL ; le lanceur fait passer le jeu sur la carte NVIDIA d'un portable Optimus
  (noir sur l'Intel) ; FPS et `[initiald6]` dans le titre ; captures pour l'agent. `[Display]` appliqué comme
  pour Direct3D : le jeu dessine dans un framebuffer de windowsloader à la taille de sa fenêtre (1920×1080),
  recopié à chaque image (`glBlitFramebuffer`) là où va l'image, bandes noires autour ; la fenêtre réglée selon
  `DISPLAY_MODE`, F11 et Alt+Entrée. Vérifié en mode agent : ORIGINAL, 4:3, 4:3_CROP, 4:3_STRETCH en fenêtre,
  plein écran fenêtré et plein écran ; la course rendue comme avant (lumières, phares, HUD).
- Menu Tab ✅ (27/10) : dessiné dans `SwapBuffers` avec le backend OpenGL 2 de Dear ImGui (le pipeline fixe),
  l'image du jeu relue dans une texture et redessinée dessous. Le jeu laisse ses programmes Cg, ses tampons et
  d'autres états actifs : sans un état neutre, le menu sortait noir, retourné et décalé. windowsloader garde
  l'état du jeu (`glPushAttrib`, et ce que la pile ne garde pas : programme GLSL, tampons, framebuffers, unité de
  texture active), pose le sien, puis rend celui du jeu. Vérifié en mode agent : pages, reprise du jeu.
- Menu de service ✅ : le jeu lancé avec `-t` (comme `gametest.bat`) : GAME TEST MODE, tests d'entrées
  et de sorties, choix au bouton Service, validation au bouton Test (`id6_race.py test`).
- Crédits ✅ : pièces comptées par la carte JVS.
- Erreur 5101 (26/10, sur les bornes : « pas de réponse du lecteur-graveur de cartes » à « avez-vous une
  licence ? ») : comme pour les 7 et 8, le `game.bat` du 6 lance `picodaemon.exe` avant le jeu ; windowsloader ne
  le lançait que pour 7 et 8. Le lanceur le démarre maintenant aussi avec `id6_dump_.exe` et
  `InitialD6_GLW_RE_SBUU.exe`, et `cardrw.cpp` met ses écritures dans `WRITE_DIR\initiald6`. Vérifié sur la copie
  locale (reprise de racecab1 le 26/10) : nettoyage du lecteur, « licence ? » passé sans carte, achat de la voiture.

## Initial D Arcade Stage 7 AAX et 8 Infinity (Sega RingEdge, 2012 et 2014)

La même borne qu'Initial D 6, dans la même table de `ringedge.cpp` (gameid SBYD et SBZZ, la clé par
`SBYD_Table.dat` et `SBZZ_Table.dat`), x86, OpenGL.
- Exécutables : `InitialD7_GLW_RE_SBYD_dumped_.exe` et `InitialD8_GLW_RE_SBZZ_redumped_.exe`, ceux que lance
  TeknoParrot. Le `InitialD8_GLW_RE_SBZZ_dumped_.exe` n'a pas le manifeste du runtime VC 2008 : il s'arrête sur
  0xC0000135 (DLL introuvable).
- Copies locales le 24/10 depuis racecab1 (4,9 et 5,4 Go). Le crochet `dinput8.dll` de TeknoParrot (le plugin
  FFB), OutputBlaster et `SDL2.dll` mis de côté (`_teknoparrot_off`).

Ce qui diffère d'Initial D 6 :
- la carte JVS passe par le pilote `\\.\mxjvs` (sinon erreur 6401 « JVS I/O board is not connected ») ;
- le lecteur de cartes. `game.bat` lance `picodaemon.exe` juste avant le jeu ; le jeu lui parle par une
  mémoire partagée (`zmShare00000532`). Sans lui : erreur 5101, « pas de réponse du lecteur-graveur de
  cartes », après la pièce. windowsloader lance aussi `picodaemon.exe` (le lanceur, comme amdaemon pour SWDC).
  Dans `picodaemon.exe`, `cardrw.cpp` joue le lecteur sur COM2 (son protocole lu dans picodaemon) :
  - aucune carte n'est jamais insérée ; une lecture attend une carte jusqu'à ce que le jeu l'annule ;
  - après une éjection, la carte reste 2 s à la fente, puis elle est retirée ;
  - au démarrage, le jeu éjecte (« prenez votre permis ») puis fait un nettoyage du lecteur, qui se termine
    tout seul.
- une fois, un défi de la clé absent de la table, auquel windowsloader répondait par des zéros, a fini en
  erreur 0949 « Keychip Not Found ». Ces défis reçoivent maintenant des octets non nuls tirés du défi ;
  pas revu depuis.

- Lancement ✅ : réglage du volant (START), « Network error 8301 » (hors ligne), démo.
- Commandes ✅ : comme Initial D 6. Partie jouée :
  - pièces, « permis : en avez-vous un ? », START pour jouer sans ;
  - « faire un permis ? » (3 pièces) : ne pas en faire (volant puis START) ;
  - Initial D 7 : achat de la voiture (AE86), boîte AT, mode, histoire, course (132 km/h) ;
  - Initial D 8 : personnage, voiture, boîte, mode, course (98 km/h).
- Retour de force ⚠️ : la carte de pilotage sur COM1, couple (0x84) envoyé en course (363 et 559 trames),
  comme Initial D 6 ; sens à confirmer au volant.
- Affichage ⏳, Menu Tab ⏳ : le même code qu'Initial D 6 (OpenGL), pas essayé (pas de copie sur ce PC).
- Menu de service : Initial D 7 ✅ (`-t` : GAME TEST MODE, GAME ASSIGNMENTS, l'option « CARD R/W USE »).
  Initial D 8 ⏳.
- Crédits ✅ : pièces par la carte JVS.

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
- Menu Tab ✅.
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
- Menu Tab ✅ : les cinq pages, jeu en pause tant qu'il est ouvert (images comptées), reprise à la fermeture.
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
- Menu Tab ✅ : pause, pages Jeu, Contrôles, Affichage, Retour de force.
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
  - démo en boucle ;
  - l'EEPROM (`wl_eeprom.bin`) : son premier enregistrement, AM_SYSDATA_STATIC, est écrit par le système de la
    borne, pas par le jeu ; vide, le jeu ne démarre pas ses crédits (amCredit) et ignore pièces et Start.
    windowsloader l'écrit quand il est vide ou abîmé : région export (4, celle de la clé), un numéro de série
    inventé ;
  - la sauvegarde : le jeu écrit `Y:\ssr_save.bin` (un disque de la borne) ; `Y:` est le dossier du jeu, où la
    copie a déjà ce fichier. Sans lui, le jeu se bloquait au choix du pilote (plus aucune image, le fil
    principal dans l'interpréteur des écrans).
- Commandes ✅ : START octet 0 bit 7, ITEM octet 1 bit 0, volant, accélérateur, pédale DRIFT ; partie complète :
  choix du mode, du pilote, du circuit, chargement, course (temps, tours, objets, turbo, retour en piste après
  une chute dans l'eau). Le sens du volant vu en course (+0,8 à gauche).
- Crédits ✅ : option du jeu `FREE_PLAY` ([Game.ssasr], menu Tab « Options du jeu », « Jeu gratuit », cochée par
  défaut) : l'octet 0x0A de l'enregistrement AM_SYSDATA_CREDIT (offset 0x20, CRC32 sur les octets 4 à 0x1F,
  copie en 0x220), réglé au lancement ; le menu de test du jeu n'a pas les réglages de pièces (ils sont dans
  celui du système de la borne). Décochée : les pièces sont comptées (CREDIT(S) 1, 2, 3) et Start en prend une.
- Retour de force ⚠️ : le test de la carte passe ; en course, le couple (84) de −16 à +20 sur 127, une trentaine
  de changements par seconde, envoyé au joueur 1 (vJoy) ; l'amortisseur (86) est noté sans être joué. Sens pas
  vérifié au volant.
- vJoy ✅ : course avec les manettes ouvertes, retour de force sur le joueur 1.
- Menu Tab ✅ : toutes les pages, dont « Options du jeu ».
- Affichage ✅ : fenêtre 1280×720 (`Settings.ini`), FPS et `[ssasr]` dans le titre ; vu à l'écran à 61 images/s
  avec la recopie GDI (noir à l'écran avant, 47 images/s).
- Menu de service ✅ : `gametest.exe` : GAME TEST MENU, test d'entrées, réglages du jeu, comptabilité,
  navigation par les boutons Test et Service de la carte.

## Sega Racing Classic (Sega RingEdge, 2009)

`d1a.exe` (`d1a.exe -t` : le menu de service), x86, Direct3D 9, 1280×720. Sur les bornes dans `C:\Sega Racing
Classic`, hors du dossier des jeux. Dans la table RingEdge / RingWide de `ringedge.cpp` (gameid SBTN, la clé par
`rom\SBTN_Table.dat`). Copie locale le 24/10 depuis racecab2 (0,67 Go) ; le crochet `dinput8.dll` de
TeknoParrot (le jeu l'aurait chargé à la place de celui de Windows), le plugin FFB, OutputBlaster, SDL2 et
`teknoparrot.ini` mis de côté. Sous TeknoParrot, sur les bornes, le jeu ne se lançait plus.

- Lancement ✅ : JVS sur COM4, « SYSTEM STARTUP », initialisation de la carte moteur, logo Sega, démo.
- Commandes ✅ : vues dans son test d'entrées (INPUT TEST) et lues dans le code (+0xdb690) :
  - joueur 1, octet 0 : bit 7 START, 6 SERVICE, 5 à 2 les vues VR SW1 à VR SW4 ;
  - le levier en H à 4 rapports sur le joueur 2, octet 0 : bit 5 haut, 4 bas, 3 gauche ; SHIFT1 gauche et haut,
    SHIFT2 gauche et bas, SHIFT3 haut, SHIFT4 bas. Un vrai levier reste en place : le rapport est tenu par
    windowsloader, choisi par `Gear1` à `Gear4` ou passé par `GearUp` / `GearDown` ;
  - volant (00H à gauche), accélérateur, frein ; partie : pièces, choix du circuit et de la boîte (automatique
    ou manuelle 4) à l'accélérateur (« STEP TO CHOOSE »), course.
- Retour de force ⚠️ : la carte moteur des Model 2 et 3, une commande d'un octet que le jeu garde dans ses
  données (VA 0x834C19, l'adresse que lit FFBArcadePlugin) et n'envoie nulle part ici (ni JVS, ni port série,
  ni E/S). Lue à chaque échange JVS et jouée en force constante d'après la position du volant : 9x roulis à
  droite, Ax à gauche, Bx secousse, Cx ressort de centrage, Dx frottement (le chiffre bas, 0 le plus fort).
  En course : centrage C4, volant à 60 % à gauche → −0,30 (vers la droite), à droite → +0,30.
- Menu Tab ✅ : la vraie touche Tab, la fenêtre au premier plan : ouvert, puis refermé.
- Menu de service ✅ : `d1a.exe -t` : GAME TEST MODE, comptabilité, test d'entrées, navigation par Service et
  Test. Au démarrage du menu, un premier appui sur Service peut se perdre.
- vJoy ✅ : course avec les manettes ouvertes (vJoy en joueur 1), retour de force sur le joueur 1 (accord de
  linuxloader).

## Storm Racer G (Sega, « Arcade PC Based »)

`bin\racing.exe` (copie `games\srg`, 6,3 Go depuis racecab2), x86, Direct3D 9, OpenAL. Module `stormracer.cpp`.
Toute la borne passe par deux DLL que le jeu importe : `usb_io.dll` (une carte USB EasyUSB214x) et `usb_cam.dll` (la
caméra des photos de joueurs). Leurs fonctions sont remplacées dans la table d'import, la carte n'est jamais ouverte.
Le crochet `dinput8.dll` de TeknoParrot, `xinput1_3.dll`, le plugin FFB et `teknoparrot.ini` mis de côté
(`bin\_teknoparrot_off`).

- Lancement ✅ : titre, démo. Le jeu est relogeable : les adresses sont relatives à sa base. Corrections :
  - l'anticrénelage (MSAA) coupé (+0xA7412, +0xA7448) ;
  - la fonction du tableau de bord des points de passage (+0x1BB20) sautée quand ses textures manquent (plantage
    sinon) ;
  - `NetDbSync.dll` (classement en ligne) répond « hors ligne » ;
  - les `printf` de la console vont au journal, et `_getch` répond « c ».
- Commandes ⚠️ : vérifiés jusqu'en course (pièce, Start, menus, l'étape photo sans caméra, course) :
  - volant (centre 0x80), accélérateur, frein, levier de boost ;
  - pièce, par le compteur de la carte ;
  - Start, au bit 2 du second mot.

  Les autres boutons gardent des noms provisoires (`Button1` à `Button200`), à identifier dans le menu de test du jeu.
- Retour de force ⚠️ : le moteur est un servo de position (`SetSteeringStatu` : force, fenêtre 0..255, mode). Il est
  joué en ressort vers le milieu de la fenêtre, et coupé quand la puissance du moteur est à 0. En course : volant à
  gauche → −0,155, à droite → +0,19.
- Affichage ⚠️ : les vidéos (WMV3 dans des AVI) restent noires, faute du décodeur Vidéo pour Windows `wmv9vcm`.
  Il n'est pas installé ici, et l'installer serait un téléchargement à décider. Des images noires sont servies à la
  place.
- Menu Tab ✅ (mode agent). À voir : le menu de service, l'étalonnage du volant, vJoy.

## Wacky Races (Taito Type X2)

`Launcher.exe`, avec `wr.ini` à côté (copie `games\wacky`), x86, Direct3D 9, 1920×1080 d'après `wr.ini`. Dans
`taito.cpp`, reconnu par `launcher.exe` + `wr.ini`. Le `d3d9.dll` et le dossier `OpenParrot`, le plugin FFB et
`teknoparrot.ini` mis de côté (`_teknoparrot_off`).

- Lancement ✅ : JVS sur COM2. `LINKID` (le numéro de la borne en réseau) manque dans `wr.ini` : il vaut 0 par
  défaut, par un crochet sur la lecture des réglages (+0x3390), sans toucher au fichier.
- Commandes ✅ : d'après son I/O CHECK :
  - START bit 2, VIEW bit 3 (le SELECT du menu de test), SERVICE bit 14 ;
  - le levier de la machine à sous (`Lever`, l'objet) bit 12 ;
  - comme Chase H.Q. 2, le volant sur la voie 2, l'accélérateur et le frein sur 3 et 4.

  Partie jouée : choix de la voiture et du circuit, course.
- Retour de force ⚠️ : le moteur passe par les sorties générales de la carte JVS (couple 0..15 et sens, le décodeur
  de Chase H.Q. 2). En course : volant à gauche → −0,93, à droite → +0,93.
- Affichage ⚠️ : la course remplit l'image. Les fonds des écrans 2D (titre, choix de la voiture et du circuit) sont
  des images de 800×600, dessinées à leur taille en haut à gauche, quelle que soit la résolution de `wr.ini` :
  1360×768 donne la même chose, et 800×600 les remplit, mais en 4:3.
- Menu de service ✅ (I/O CHECK). Le jeu affiche « STEERING HAS NOT CALIBRATED » : l'étalonnage du volant dans son
  menu de test reste à faire.
- Menu Tab ✅ (mode agent). À voir : vJoy, les pièces (le jeu est en FREE PLAY).

## Let's Go Safari (Sega RingWide, 2014)

`GameSampR_RingWide.exe` (copie `games\lgs`), x86, Direct3D 9, 1280×720. Son menu de service est un programme à part,
`GameTestR_RingWide.exe`, lui aussi lancé par windowsloader. Dans la table RingEdge / RingWide de `ringedge.cpp`
(gameid SDAX). `teknoparrot.ini` mis de côté.

- Lancement ✅ :
  - les copies n'ont pas de table de clé (`SDAX_Table.dat`). Le jeu construit son chemin (`%s\%s_Table.dat`) : ce
    format est remplacé par le mot « toolmode », la voie de Sega pour tourner sans table (comme K.O. Drive) ;
    tous les défis de la clé y sont hors table et reçoivent des zéros. Les releases 20261025 et 20261025-2 leur
    donnaient des octets fabriqués (faits pour Initial D 8) : « Error 0949 / Keychip Not Found » sur racecab2,
    corrigé dans la 20261025-3 ;
  - le service PCP du port 40114 (l'état des stockages) est servi ; sans lui, le démarrage attend une minute ;
  - la carte JVS doit être une 837-14895 en « FE MODE » (l'identité que lui donne TeknoParrot), avec un joueur,
    14 boutons et au moins 16 sorties. Sinon : erreur 6402 « JVS I/O board does not fulfill the game spec »
    (+0x17100).
- Commandes ✅ : vues dans son INPUT TEST. Le jeu lit trois octets de boutons :
  - octet 1, bits 7 à 4 : les boutons de parcours 1 à 4 (`Course1` à `Course4`) ;
  - octet 1, bits 0 à 3 : vue, essuie-glace, klaxon, feu d'artifice ;
  - octet 2, bits 2 et 3 : le levier (haut, bas) ; bits 6 et 7 : les contacts du volant (HANDLE L, R).

  Le jeu ne lit aucune voie analogique : le volant tourné d'un quart ferme un contact. Partie jouée : pièce, choix du
  parcours, conduite, quiz, zone suivante.
- Retour de force ⚠️ : pas de moteur. La sortie VIBRATION de la carte (octet 0 bit 2, allumée pendant la conduite)
  est jouée en secousse légère à 10 Hz. Les autres sorties (son OUTPUT TEST) : le bloqueur de pièces et les lampes
  des boutons.
- Menu de service ✅ : GAME TEST MENU, INPUT TEST, OUTPUT TEST, navigation par Service et Test (le curseur part de
  EXIT).
- Menu Tab ✅ (mode agent). À voir : vJoy.

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
embrayage, levier en H). Dans windowsloader : `[Game.bg4] CABINET = PRO` (page « Options du jeu » du menu Tab,
`--set Game.CABINET=PRO`, ou à la main dans le fichier de réglages), qui met une seconde carte sur le bus JVS (nœud 2 ; `jvs.cpp` en accepte maintenant deux).

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
  - `[Game.bg4] HOLD_GEAR = true` : le rapport reste engagé levier relâché (« Hold Gear » d'OpenParrot, 10 octets
    à +0x8adff), pour un levier dont les positions ne tiennent pas ; vérifié en course (2 puis 3, levier lâché).
- La carte du nœud 1 reçoit en plus la commande Taito 0x66 (2 octets, sens inconnu, répondue).
- Retour de force ⚠️ : le même qu'en Tuned (330 changements envoyés pendant une course AT).
- Menu de service ✅ : test d'E/S (SHIFT LEFT/RIGHT, SEN1/SEN2, CLUTCH en plus), ADJUSTMENT avec l'embrayage.
- Restent : vJoy et le menu Tab non refaits en Pro (le même code qu'en Tuned) ; l'identité de la seconde
  carte est celle de la première (celle de la vraie borne inconnue) ; le côté de chaque capteur de la grille
  est supposé (le jeu s'en accommode : il pousse dans les deux sens).

## Battle Gear 4 (Taito Type X+, 2006, première édition)

`game.exe _MTS_FULL_SCREEN_` dans `games/bg4`, v2.03 GBR (« BATTLE GEAR 4 V2.03 GBR, May 18 2006 »), x86,
Direct3D 9, 800×600. Le module de Tuned (`taito.cpp`), qui reconnaît cette édition par l'horodatage de l'exe
(OpenParrot l'appelle « BG4_Eng ») et pose ses correctifs aux adresses de celle-ci (barre de menu, IntroFix).

- Lancement ✅ : Notice, « NETWORK: DISABLE », TXSURROUND, la vidéo de démo, l'écran titre, démo en 3D.
  - Le jeu plantait à 50 s, dans la démo (lecture à l'adresse 0, +0xeb838) : contrairement à Tuned, qui n'a pas le
    fichier, cette édition lit `E:/data/Scene/demo/attract/attract_demo.wmv` par DirectShow (un rendu en texture, celui
    de l'exemple Texture3D9 de Microsoft). Le fichier est ouvert par les DLL de Windows elles-mêmes, que les crochets
    de fichiers ne voient pas : E:\ n'existe pas, AddSourceFilter et RenderFile échouent (0x80070003), le graphe
    n'est pas fait et le jeu appelle quand même IMediaControl::Stop sur un pointeur nul.
  - windowsloader redirige maintenant les chemins donnés au graphe de DirectShow (IGraphBuilder::RenderFile et
    AddSourceFilter, posés à la création du FilterGraph dans `system.cpp`), comme ceux des fichiers du jeu : la vidéo
    (WMV3, 400×256, décodée par le « WMVideo Decoder DMO » de Windows, en RGB24 vers le rendu du jeu) passe, plus de
    plantage (vérifié 90 s, puis en course).
  - « $WAIT FOR A WHILE » / « $ OUT OF SERVICE » en alternance sur l'afficheur JVS pendant le démarrage (5 à 9 s) :
    normal, le jeu attend la carte du volant, puis continue.
- Commandes ✅ (mode agent, `scripts/agent/race_check.py bg4eng`) : Start, constructeur, voiture, boîte AT, mode
  course, circuit, grille, « GO! », course. Test d'E/S du menu de service : HANDLE (volant à ±0,9), ACCEL, BRAKE (0
  à 1), VIEW, HAZARD, SIDE BRAKE ; les mêmes bits que Tuned.
- vJoy ✅ : `--controllers`, course, 34 changements de force envoyés au joueur 1.
- Retour de force ⚠️ : la même carte du volant que Tuned (COM1), commande 0x0D ; sens à confirmer au volant.
- Affichage ✅ : 800×600 à 30 images/s (le jeu se règle à 30), recopie GDI.
- Menu Tab ✅ : pause, pages Jeu (cartes : JVS sur COM2, carte du volant Taito sur COM1 ; pas de seconde carte, la
  borne Pro n'existe que pour Tuned), Contrôles, Retour de force.
- Menu de service ✅ : TEST MODE (bouton Test), SHIFT UP descend, SHIFT DOWN remonte, START choisit ; EXIT ferme le
  jeu (la borne redémarre).
- Crédits ⚠️ : le jeu est en « FREE PLAY » dans cette copie ; la pièce n'a pas été vue créditer.

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

Un jeu qui n'attend pas l'écran (D3D9 `D3DPRESENT_INTERVAL_IMMEDIATE`, D3D11/DXGI `Present(0, …)`) ne voit son
image recopiée qu'une fois par rafraîchissement (`wl_display_gdi_due`, `display.cpp`, 3/4 de la période de
l'écran au moins entre deux recopies) ; ses autres images vont à la fenêtre cachée seulement. Avant (26/10), chaque
image était lue et dessinée, la recopie tenait le jeu à la cadence de GDI : Wasteland Racers chargeait sa course en
141 s au lieu de 12. Programmes d'essai sur ce PC (avant → après) : D3D9 76 → 3 387 images/s ; D3D11 60 → 176
(486 sans windowsloader) ; les jeux qui attendent l'écran ne changent pas.

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

- Menu Tab, page Affichage : les sept choix de linuxloader, « Position du recadrage », « Courber les parties
  coupées sur les bords », « Milieu non déformé », « Rayon de courbure », appliqués à l'image suivante ; en bas
  du menu « Sauvegarder » (le fichier lu), « Sauvegarder pour ce jeu » (la première fois une copie du fichier lu
  en `<nom court>.ini`, puis ce fichier), « Fermer ». Le menu reste entier au-dessus d'une image recadrée ou
  courbée.
- Vérifié (captures de la fenêtre) sur GTI Club 3 (D3D9, 16:9), Battle Gear 4 (D3D9, 4:3) et DiRT Showdown
  (D3D11, 16:9) : les 16 cas de `crop_check.py` (bloc-notes) et le menu ; `--set`, `-c`, le fichier du jeu
  créé, relu au lancement suivant.
- OpenGL (Initial D 6 à 8, Frenzy Express, depuis le 27/10) : comme la recopie GDI, sauf la courbure (recadré
  sans courbure) : l'image du jeu est recopiée par OpenGL dans la fenêtre.
- Modes d'affichage (`DISPLAY_MODE`, mêmes noms que linuxloader) : `WINDOW`, `WINDOW_STRETCH` ; `FULLSCREEN`,
  `FULLSCREEN_STRETCH`, l'écran passé à son plus petit mode qui contient l'image du jeu, comme SDL chez linuxloader (repris en revenant à une fenêtre, et par Windows à
  la fin du jeu ; à la résolution du bureau si l'écran refuse cette taille) ; `BORDERLESS`, `BORDERLESS_STRETCH`
  (« plein écran fenêtré », 23/10), une fenêtre sans bordure qui couvre l'écran à la résolution du bureau, ce que
  faisait `FULLSCREEN` jusque-là. F11 / Alt+Entrée : la fenêtre ou le plein écran choisi. Vérifié sur Battle Gear 4 :
  `BORDERLESS` (image entre bandes) et `BORDERLESS_STRETCH` ; `FULLSCREEN` pas essayé à l'écran de ce portable.

## Les cartes de la borne dans le menu Tab

La page « Jeu » du menu Tab dit, sous le nom du jeu, par où passent ses contrôles et quelle carte pilote le moteur
du volant ; chaque module le déclare (`wl_game_set_boards`) :

| Jeu | Contrôles | Carte moteur |
|---|---|---|
| Sega Rally 3 | USB HID, E/S Sega Europa-R | pilote sur port d'E/S 0x1020 (Sega PC) |
| Daytona Championship USA, GRID, DiRT Showdown | DirectInput « SEGA IO Controller » | pilote sur port parallèle (Sega PC) |
| Ford Racing Full Blown | USB HID, Happ UGCI | pilote sur port parallèle (Sega PC) |
| Initial D 6 AA, K.O. Drive, Sonic & Sega All-Stars Racing | JVS, Sega 837-14572 | « Sega-like servoboard » sur COM1 |
| Initial D 7 et 8 | JVS, Sega 837-14572 (pilote `mxjvs`) ; lecteur de cartes par `picodaemon.exe` | « Sega-like servoboard » sur COM1 |
| Sega Racing Classic | JVS, Sega 837-14572 | carte moteur Model 2/3, sa commande lue dans les données du jeu |
| Let's Go Safari | JVS, Sega 837-14895 (« FE MODE ») | aucune : la sortie « vibration » de la carte JVS, jouée en secousse |
| Battle Gear 4 | JVS (identité Sega 837-14572), une seconde carte sur la borne Pro | volant Taito, protocole Battle Gear, COM1 |
| Chase H.Q. 2 | JVS | sorties de la carte JVS |
| GTI Club 3 | ACIO de Konami « HBHI » | ACIO « HBHI » |
| Mario Kart Arcade GP DX | JVS, Namco NA-JV | carte moteur Namco, COM1 |
| Wasteland Racers 2071 | SDC2 de Trio Tech | SDC2 (servo en position) |
| Crazy Ride | E/S UNIS | volant UNIS |
| Storm Racer G | carte USB, par `usb_io.dll` | la même carte (servo de position : force et fenêtre) |
| Wacky Races | JVS (identité Sega 837-14572), COM2 | sorties de la carte JVS (couple 0..15, sens) |

## Options propres à chaque jeu (menu Tab, « Options du jeu »)

Comme linuxloader (forme convenue avec sa session le 23/10) : section `[Game.<nom court>]` du fichier de réglages (le
commun ou le `<nom court>.ini` du jeu), clés en majuscules, valeurs de liste en majuscules, `true`/`false`, nombres ;
`--set Game.CLE=VALEUR` vise la section du jeu lancé. Le module du jeu déclare ses options (case, liste ou curseur, une
aide, « au prochain lancement » ou non) ; l'onglet « Options du jeu » les montre (sinon « Aucune option pour ce jeu. »),
avec la mention « (au prochain lancement) » et un rappel si l'une a changé ; « Sauvegarder » et « Sauvegarder pour ce
jeu » les écrivent avec le reste. Premier jeu : Battle Gear 4 Tuned (borne Standard ou Pro, garder le rapport).
Vérifié : `--set Game.CABINET=PRO` et `game.hold_gear=true` lus dans `[Game.bg4]`, borne Pro et patch posés ; la page
vue à l'écran sur Battle Gear 4 (liste à 45 % de la largeur, libellés « Standard » / « Pro », le détail dans l'aide).

## Vraies cartes sur un port série (passthrough)

Comme linuxloader, mêmes clés : `[Emulation] EMULATE_JVS = false` avec `[Paths] JVS_PATH = COM5` envoie le port JVS du
jeu à une vraie carte ; `EMULATE_DRIVEBOARD = false` avec `SERIAL_<n>_PATH` (n : le COM du jeu) ses autres ports émulés
(carte du volant, du moteur). Le jeu ouvre alors lui-même le port du PC (`\\.\COMx`), avec ses propres réglages
(vitesse, délais, lectures en attente) ; le refus des ports non émulés est levé pour ce port-là. Les mêmes choix sont
dans la partie « Cartes » de « Options du jeu » : une liste par port émulé du jeu, « Émulée » ou « Vraie carte sur
COMn » (les ports que Windows connaît), au prochain lancement.

Vérifié (23/10) avec Battle Gear 4 sur une paire de ports virtuels (COM10 vers COM11, un script Python jouant la carte
sur COM11) :
- carte du volant (COM1 du jeu) : la même séquence de démarrage que la carte émulée, puis l'interrogation, le jeu
  jusqu'à l'attract ;
- carte JVS (COM2 du jeu) : ligne de détection lue sur les lignes de modem du vrai port, carte adressée et identifiée,
  1 766 interrogations en 60 s sans reprise, attract normal.
Pas encore essayé avec une vraie carte (ce PC n'en a pas).

## Sélecteur de jeux (windowsloader.exe sans argument)

Comme celui de linuxloader : `windowsloader.exe` lancé seul (double clic) ouvre une fenêtre, à gauche la liste des
jeux du dossier des jeux, à droite la fiche du jeu choisi (image si le dossier en a une, état, plateforme, programme,
arguments, ce qui est émulé). Entrée, double clic ou la manette lancent le jeu par `windowsloader.exe` lui-même ; le
sélecteur se cache pendant la partie et revient à la fin. Un jeu qui s'arrête aussitôt affiche ce que le lanceur a dit.
Dossier des jeux : `[Picker] GAMES_DIR`, sinon `games` à côté du lanceur ou un ou deux niveaux au-dessus ; Tab en
choisit un autre. Les jeux sont reconnus comme le fait la DLL (nom du programme, fichiers à côté, date du programme
pour les deux Battle Gear 4) : la table est `src/launcher/gamelist.cpp`, à tenir à jour avec les modules.

Vérifié (25/10) : dossier d'essai (deux éditions de Sega Rally 3, Chase H.Q. 2, Wangan Midnight 5, un dossier sans
jeu écarté), flèches, image, message d'erreur du lanceur, Échap ; puis le dossier `games` du dépôt, Wangan Midnight 5
lancé depuis le sélecteur (windowsloader.exe → windowsloader64.exe → wmn5r.exe), le sélecteur revenu à l'arrêt du jeu.

## Dossier d'écriture, sorties réseau, menu Tab désactivable (25/10)

Les trois fonctions de linuxloader qui manquaient, avec ses clés :
- `[Paths] WRITE_DIR` ou `--write-dir` : les dossiers du lanceur et des jeux ne sont plus que lus.
  `windowsloader.ini`, `controls.ini` (copiés la première fois) et le journal vont à sa racine ; ce que le jeu
  écrit va dans `WRITE_DIR\<jeu>\<lecteur>\<chemin complet>`, copié d'abord quand il est modifié ; une lecture
  y prend le fichier s'il y est, sinon à sa place ; les listes de dossiers joignent les deux. Les dossiers du
  système ont chacun le leur dans `WRITE_DIR\<jeu>` : `appdata` (Roaming, Local, LocalLow), `temp`,
  `programdata`, `windows` ; un disque sous filtre d'écriture (UWF) sauf le dossier d'écriture garde ainsi tout
  ce que le jeu écrit. Un seul réglage, `WRITE_DIR` : vide, tout s'écrit à sa place habituelle. Le sélecteur
  suit.
- `[Outputs] OUTPUTS_NETWORK` : serveur TCP (8000) au protocole de MAME pour BackForceFeeder, annonce UDP
  (8001) ; les lampes de la carte JVS, `RawDrive` (les octets de la carte moteur Sega, ou pour les autres
  cartes les trames de la carte servo Sega faites de la force), `mame_stop` à la fin.
- `[Launcher] SETTINGS_MENU = false` : Tab n'ouvre plus le menu (borne publique) ; l'agent l'ouvre toujours.

Vérifié (25/10) :
- Wangan Midnight 5 avec les trois : le dossier du jeu intact, ses 20 fichiers écrits dans le dossier
  d'écriture (chemins de plus de 260 caractères, forme `\\?\`), démonstration normale ; annonce UDP
  `mame_start = wmmt5`, lampes `RawLamps`/`lamp0..15`, `mame_stop = 1` à la fin ; « menu : Tab n'ouvre pas »
  au journal, `menu on` de l'agent toujours possible ;
- un programme d'essai en 32 et 64 bits, appels A et W : lecture, fichier modifié (copié d'abord), créé,
  `CREATE_NEW` sur un fichier existant, liste fusionnée, .ini écrit et relu, renommage, dossier, suppression
  (16 contrôles chacun, tous bons, le dossier du « jeu » inchangé) ;
- Wangan Midnight 5 sans dossier d'écriture : comme avant ;
- les dossiers du système (25/10, après la demande sur UWF) : le programme d'essai écrit son rapport dans
  `%TEMP%` et un fichier dans AppData\Local ; ils arrivent dans `temp` et `appdata\Local` du dossier d'écriture,
  ni le vrai dossier temporaire ni le vrai AppData touchés (17 contrôles, 32 et 64 bits, A et W). Wangan
  Midnight 5 : le fichier que le pilote NVIDIA écrit dans ProgramData pour le jeu rangé dans `programdata`,
  démonstration normale.

Les compagnons (25/10) : `picodaemon.exe` (Initial D 7 et 8) et `amdaemon.exe` (SWDC), que le lanceur démarre
avant le jeu, écrivent dans le `WRITE_DIR\<jeu>` de leur jeu (le lanceur leur dit lequel ; picodaemon prend
initiald7 ou initiald8 d'après le programme du jeu) et n'ouvrent pas le serveur des sorties réseau (il aurait pris
le port 8000 avant le jeu). Vérifié avec SWDC : le port 8000 au jeu, un seul sous-dossier `swdc` pour les deux,
les fichiers d'amdaemon (AMFS, Y:\SDDS, son journal) copiés dedans, le dossier du jeu intact. picodaemon pas
essayé ici (plus de copie d'Initial D 7 ou 8).

`RawDrive` vu en jeu le 25/10 : Wangan Midnight 5 en course, son retour de force mis à ON (148 trames de la
carte servo Sega faites de la force, avec les lampes et `mame_start = wmmt5`).

Le 26/10 (demandes 143 à 145) :
- Onglet « Sorties et réseau » du menu (page 6 de l'agent) : l'état du serveur (port, clients), puis la liste
  défilante de ce que le jeu peut envoyer, avec son type, sa dernière valeur et ce que c'est ; les cartes
  déclarent leurs sorties en s'enregistrant, et les valeurs sont gardées même sans `OUTPUTS_NETWORK`. Vu dans
  Mario Kart DX et SWDC.
- Les lampes des cartes qui ne sont pas JVS, envoyées comme celles d'une carte JVS (`RawLamps`, `lamp<n>`), et
  des valeurs nommées (« nom = valeur ») :
  - SWDC ✅ : les 14 sorties de la carte IO4 (commande 04, dans amdaemon), passées au jeu par la mémoire
    partagée des entrées ; `lamp0` (Start, semble-t-il) s'allume en attract et s'éteint à l'appui sur START,
    vu par un client TCP.
  - Mario Kart DX ✅ : les LED de la borne, commandes Namco 0x78, 0x79, 0x7A, 9 LED RVB chacune, 4 bits par
    couleur → `led0_r` … `led26_b` (0-15, ordre des couleurs supposé) ; 81 valeurs vues dans le flux en attract.
  - Wasteland Racers 2071 ⚠️ : `SetDigitalOut` → `lamp0..15`, nommées d'après l'`Inputs.xml` du jeu (1 BOOST,
    2 CAMERA, 3 CHEAT1, 4 ACTIVEBOOST, 10 BRAKES sur la copie de racecab1).
  - Storm Racer G ⚠️ (lu dans `usb_io.dll`) : `UsbIO_SetLedLightStatu`, octet 6 = 6 lampes → `lamp0..5` ;
    `UsbIO_SetPWMLightStatu`, 6 intensités → `pwm0..5` (0-255).
  - Crazy Ride ⚠️ (lu dans `UsbPluginsDll_R.dll`) : `BIND_SetBoxLight`, `SetEdgeLight`, `SetSideLight`,
    `SetMachineSideLight` (3 bits chacune) → `…_r`, `_g`, `_b` ; `SetSpeedLight`, `SetStopLight`,
    `SetBrakeLight` (0-3), `SetGameStateLight`, `SetStartLightState` ; la fonction de la DLL appelée ensuite.
  - GTI Club 3 ⚠️ (lu dans `libacio.dll`) : `ac_io_hbhi_control_lamp_on` / `_off` (8 lampes) → `lamp0..7`,
    `_bright` (0-31) → `bright0..7`, `_mode` suivi ; libacio appelée ensuite.
  - ⚠️ = pas essayé en jeu : ces jeux ne sont pas sur ce PC (bibliothèques copiées de racecab1 pour les lire).
  - SWDC en mode Cabinet ✅ (27/10) : la carte LED 837-15070 (`led0_r` à `led8_b`, `fet0` à `fet2`) et le tableau de
    bord (`speed`, `gear`, `rpm`, `dash_lamp0` à `dash_lamp7`) ; voir sa section. Sega Rally 3, Daytona, GRID,
    Showdown, Ford Racing : le jeu n'envoie rien à sa carte d'E/S.

Pas encore vu : la touche Tab elle-même avec `SETTINGS_MENU = false`, `CopyFile`, `SetFileAttributes` et
`RemoveDirectory` à travers le dossier d'écriture.

## Menu Tab sur Battle Gear 4, Chase H.Q. 2 et DiRT Showdown

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
- Affichage ✅ : fenêtre 1360×768 (recopie GDI), 30 images/s. Depuis le 25/10 (contribution de Legired, pull
  request n° 1, `2fe629e`), `[Display] WIDTH x HEIGHT` donne la résolution de rendu : les deux valeurs
  1360×768 du code remplacées, les 84 endroits qui en font des flottants gardant l'écran de 1360×768 pour la
  vue et le 2D projeté, le 2D déjà transformé (XYZRHW) et les rectangles de découpe remis à l'échelle par
  `wl_d3d9_scale_2d` ; `AUTO` laisse le jeu comme avant. Pas vérifié ici (plus de copie du jeu) : release
  20261025-5.
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
- Menu Tab ✅ : les cinq pages, jeu en pause.
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
  60 à 90 images/s. Sur les bornes (Image borne, 25/10), « Get Ready To Race » durait 150 à 170 s : le jeu
  charge une texture de la carte par image, sans attendre l'écran (intervalle IMMEDIATE), et la recopie de son
  image (2560×1440 multiéchantillonnée, relue puis mise à l'échelle par GDI) le tenait à 60 images/s : 141 s de
  chargement, 12 s avec `PRESENT = DIRECT` (938 images/s). Corrigé dans `d3d9hook.cpp` : pour un jeu qui
  n'attend pas l'écran, l'image n'est recopiée qu'une fois par rafraîchissement, ses autres images présentées à la
  fenêtre cachée seulement. Programme d'essai (même appareil) : 76 images/s avant, 3 387 après ; pas encore revu
  sur borne. ⚠️ Les vidéos (DirectShow, son propre appareil Direct3D 9, laissé tel quel) ne se voient pas
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
- Menu Tab ✅ : les cinq pages, jeu en pause (images 143 → 143), reprise à la fermeture.
- Menu de service ✅ : MAINTENANCE (bouton Test) : étalonnage (BOOST, lance `CalibrationStarter.exe`, refusé, et
  quitte le jeu), prix de la partie (VIEW), jeu gratuit, statistiques.
- Crédits ✅ : jeu gratuit (réglage de la copie) : le jeu met le prix d'une partie dans le compteur de la carte
  (`SetCoin(8, …)`, le compteur du bit COIN) puis le relit (`GetCoin(8)`) ; une pièce compte une fois par appui.

## Mario Kart Arcade GP DX 1.18 (Namco System ES3)

`MK_AGP3_FINAL.exe`, x86 relogeable, Direct3D 10 (le 9Ex qu'il crée ne dessine rien). Module
`src/hook/games/namco.cpp`. Il dessinait du noir : il restait dans sa vérification du démarrage (`clBootCheck`, douze
étapes) sans rien charger, et la recopie de son image lisait du noir. Ce qu'il a fallu (23/10), à l'écran de
vérification ensuite :

- Lancement ✅ :
  - carte JVS sur **COM3** (« namco ltd.;NA-JV;Ver6.01 », voies analogiques de 10 bits), ses commandes à elle
    répondues comme la bibliothèque JVS du jeu les lit (`70 18 PL` : rapport et un octet ; un lien série dans la
    carte, `70 05`/`70 15`/`70 16`, vide) → « OK JVI/O connected » ;
  - clé de sécurité de la borne (périphérique USB signé) : la fonction du jeu qui la lit répond « présente,
    vérifiée », numéro « 271011-020001 » (2710 le jeu) → borne de conduite n° 1 ;
  - lecteur de cartes Banapass (`bngrw.dll`) : ses requêtes rappellent leur fonction, aucune carte → « OK » (jouer
    avec une carte : non) ;
  - réseau des bornes, pour les appels du jeu seulement : une interface 192.168.112.11, le routeur 192.168.112.254
    qui répond au ping, la route par défaut « changée » sans rien toucher ; diffusions de la liaison sur 127.0.0.1 →
    « リンクOK » ; `ipconfig` et routes du PC refusés ;
  - sans serveur : l'authentification réseau prise pour récente, la limite de 36 h hors ligne pour non atteinte →
    « オフライン », le jeu tourne (bandeau « connectez la borne au réseau »).
  - Puis chargement, avertissement, attraction, partie (Mario, 50cc, coupe, manche, course).
- Affichage ✅ : la recopie GDI d'une chaîne d'échange Direct3D 10 lit maintenant par l'appareil Direct3D 10 (par un
  contexte Direct3D 11, elle passait avant les commandes du jeu : noir) ; 1360×768, 60 images/s.
- Commandes ✅ (menu de test, « スイッチテスト ») : Test est un interrupteur (le menu dure tant qu'il est levé),
  Service, sélecteur haut / bas, Entrée, objet, Mario, pièce ; volant (gauche en bas), accélérateur, frein sur les
  voies 0, 1, 2, contre l'étalonnage du menu (« アナログボリュームの初期化 », fait dans la copie). En course : accélère,
  tourne.
- Retour de force ⚠️ : carte moteur sur **COM1** (« C01 »), le jeu n'envoie rien tant que « ハンドル反力の使用 » est
  OFF dans ses réglages (mis à ON dans la copie, « ステアリングチェック OK ») ; en course, visée au centre (0x200),
  raideur et amortissement jusqu'à 50, vibration sur un choc ; jouée en ressort vers la visée. Sens par le
  raisonnement (volant à gauche → poussé à droite), à vérifier au volant. vJoy pas essayé.
- Menu Tab ✅ : dessiné par l'appareil Direct3D 10 du jeu (backend DX10 de Dear ImGui, de sm2-emu) ; les pages vues,
  jeu en pause.
- Menu de service ✅ : menu de test, test des entrées-sorties, étalonnage, réglages du jeu ; enregistrés (le
  renommage de `F:\TestMode\…bin.tmp` passe maintenant par la redirection de F:\).
- Crédits ✅ : la pièce compte (test des contacts) ; la copie est en jeu gratuit.
- Link entre bornes ✅ (26/10, `[Game.mkdx] LINK` et `CABINET`, voir le README ; vérifié sur les deux bornes le 27/10) :
  - le protocole, lu dans le jeu (+0x318c40) : chaque borne diffuse toutes les 10 s environ sur UDP 20199 huit
    octets « MK3 » 0x12 0x10, son mode, son groupe et son PCB-ID − 1 ; une borne entendue depuis le réseau des
    bornes (l'adresse de l'envoyeur) entre dans le link, ses paquets (155 octets, puis l'état de toutes) lui
    sont envoyés sur UDP 20100 + groupe ; un seul sendto et un seul recvfrom dans le jeu pour tout cela ;
  - le PCB-ID (1 à 4) est un réglage du jeu (menu de test, ゲーム設定 → 基板ID), pas l'adresse : deux bornes au
    même, erreur E24-03 « 基板ID重複エラー » (vue avec deux jeux au PCB-ID 1). `NET_PCB_ID` de
    `NetConfig.csv` n'est pas lu par la 1.18 ;
  - windowsloader : la borne N a l'adresse 192.168.112.(10+N) pour le jeu, la clé 27101102000N ; ses envois aux
    bornes (diffusion comprise) vont aux PC de `LINK`, ce qu'elle reçoit d'eux vient de leurs adresses de borne ;
    un bind sur l'adresse d'une autre borne est refusé (comme sur borne) ; rien d'autre ne sort du PC. Ses
    sockets sont liées à l'adresse de la borne dans `LINK` (pas à 0.0.0.0) et ce qui vient d'ailleurs n'est pas
    entendu : l'adaptateur VPN de TeamViewer des bornes (7.x.x.x/8) n'y entre pas ;
  - essai sur ce PC (deux jeux, 127.0.0.1 et 127.0.0.2, chacun son dossier d'écriture et son mutex) : PCB-ID 2
    réglé dans le menu de test du second ; chacun entend l'autre, l'état de l'autre (310 octets) reçu toutes les
    0,18 s ; pièce et Start sur les deux : choix du personnage et du circuit ensemble, puis une course à quatre
    (les deux Mario des bornes, Yoshi et Luigi en COM), Kingdom Way, le même classement sur les deux écrans, le
    kart de l'autre borne visible à côté ;
  - sur les deux bornes (27/10, Image borne, la -2 en mode agent, BFF arrêté) : `--set Game.LINK=192.168.1.4,192.168.1.5`
    et `--set Game.CABINET=1` / `2`, PCB-ID 1 sur racecab1 et 2 sur racecab2 (en 1.18, la ligne 基板ID de その他
    ne fait qu'afficher : le réglage est sous ゲーム設定) ; les paquets dans les deux sens (UDP 20100,
    192.168.112.11 et .12), la démo synchronisée « 2人で対戦できます », Start des deux côtés, Kingdom Way, une
    course à quatre (les deux Mario des bornes, Yoshi et Luigi en COM) ; sur racecab2, le Mario de racecab1 porte
    l'objet qu'il vient de prendre. Le bandeau hors ligne s'affiche en jeu, sans effet sur la partie.

## Mario Kart Arcade GP DX 1.10 (Namco System ES3, « MKDX french Mod V 1.h »)

`MK_AGP3_FINAL.exe` de 2015 (« MK3 Rev.1.10.22/JP »), traduit en français par un mod ; le même module que la 1.18,
qui les distingue par `bngrw.dll` (absente de la 1.10). Copie locale le 23/10 (16 497 fichiers, 5,8 Go, 76 min).

- Lancement ✅ : la carte JVS, la carte moteur, le réseau des bornes comme pour la 1.18 ; les trois fonctions de la
  1.18 retrouvées ailleurs dans cet exe (la clé de sécurité par le GUID de son interface, l'âge de l'authentification
  par « last_auth.bin » qu'elle lit, la limite hors ligne par ses octets, identiques) ; le lecteur de cartes, que la
  1.10 parle elle-même sur **COM4** (38400 bauds) : des trames NFC du PN532 de NXP (le RC-S956 de Sony des lecteurs
  Banapass), chaque commande acquittée puis répondue (`D5` et la commande + 1), sans carte posée ; il a fallu un octet
  d'état aux réponses d'écriture de registre, aucun à celles de lecture → « IC card R/W test OK ». Puis attraction en
  français, partie (Mario, mode national, avatar), course (Château de Peach).
- Commandes ✅ : pièce, Start, objet (valider), accélérateur, volant ; Test est un interrupteur à tenir levé.
- Retour de force ⚠️ : « Reaction force » était OFF dans les réglages de la copie (« Game options » du menu de test) :
  mis à ON (pris en compte à la sortie du menu) ; en course, raideur 22 à 28, amortissement 14 à 15 ; le volant
  braqué à +0,6, la force le repousse (−0,19, −0,24). Sens à vérifier au volant.
- vJoy ✅ : course avec les manettes ouvertes, forces sur le joueur 1.
- Affichage ✅, menu Tab ✅ (jeu en pause, les pages, le lecteur de cartes listé sur COM4).
- Menu de service ✅ : menu de test, réglages du jeu modifiés et enregistrés (« E05-11 ERROR DEVICE » en tête du menu,
  sans effet vu).
- Crédits ✅ : la pièce compte.
- Link entre bornes 🔄 (26/10) : le même code que la 1.18 (mêmes imports de ws2_32 par numéro, mêmes ports) ; le
  PCB-ID dans Game options → Board ID. Ses diffusions disent « MK3 » 0x00 0x01 (la 1.18 : 0x12 0x10) : une 1.10 et
  une 1.18 ne se lient pas, les bornes du link doivent avoir la même version. Pas de mutex « une seule fois » dans
  la 1.10. Essai sur ce PC comme pour la 1.18 (Board ID 2 réglé sur le second) : chacune entend l'autre, deux
  pièces et Start sur les deux, « Deux Joueurs VERSUS / CO-OP » proposé, course ensemble au Château de Peach, le
  même chrono sur les deux écrans, le kart de l'autre borne marqué « Mario ». Sur les deux bornes (27/10, Image
  borne, Board ID 1 et 2) : « Deux Joueurs VERSUS / CO-OP » proposé sur les deux, la course pas encore lancée.

## Crazy Ride (UNIS PC, Unreal Engine 4, 64 bits)

`SysStart\Game\CrazyCab\CrazyTaxiGame\Binaries\Win64\CrazyTaxiGame-Win64-Shipping.exe` (Unreal Engine 4.19), lancé par
`windowsloader.exe`, qui le passe à `windowsloader64.exe`. Direct3D 11, 2048×1280. Module `src/hook/games/unis.cpp` :
le jeu atteint sa borne par `Plugins\UsbPluginsDll_R.dll`, dont il demande les fonctions `BIND_*` par GetProcAddress ;
windowsloader donne les siennes à la place (les autres, fichiers ini et conversions, restent celles de la DLL).

- Lancement ✅ : « Initializing steering wheel », classements, démo, « Free to Play ». Le chargement prend de 10 s à
  2 min 30 selon les lancements (le dossier fait 4,85 Go : le cache du disque) ; les images de l'agent échouent tant
  qu'il charge.
  - Un lancement restait bloqué : le fil de rendu de l'Unreal Engine dans le Present de windowsloader pendant que le
    fil principal l'attendait (échantillonneur de fils 64 bits). Present tourne ici sur le fil de rendu, pas sur celui
    qui possède les fenêtres ; `SetWindowPos` ou un changement de style depuis là attend les messages du propriétaire.
    `display.cpp` applique maintenant le mode par la procédure de fenêtre (message posté) et place la fenêtre fille en
    `SWP_ASYNCWINDOWPOS` ; 17 lancements de suite sans blocage ensuite.
  - Clé : BIND_InitDongle puis BIND_IsDongleVerify ; le jeu ne garde qu'un booléen : « présente, vérifiée ».
  - Refusés : BIND_SetLocalTime, BIND_ResetMachine, BIND_SetSystemVolume, BIND_SHDeleteFolder (le jeu veut supprimer
    `AppData\Local\FB` et `AppData\Local\Unreal Engine`), la caméra (« Camera Error », Start passe), les connexions
    vers des serveurs en 443 ; BIND_ICMPPing échoue.
- Commandes ✅ (mode agent, scratch `cr_play.py`) : jeu gratuit ou pièce, Start (avertissement, ceinture, consignes),
  choix de la voiture au volant, course (passagers pris et déposés, Nitro). Trouvé par des bits de sonde, le code qui
  décode la trame (+0x1b3b90) et l'Input Test du menu de test :
  - octet 2 : bit 0 le bouton rouge (valider), bit 1 le noir « D » (ligne suivante), bit 2 le vert (ouvre le menu de
    test) ; octet 3 : bit 0 Start (puis le Nitro), bit 1 « Pause Seat Motion » ; les bits 2 et 7 de l'octet 3 sont lus
    sans usage vu ;
  - octet 6 le frein, octet 7 l'accélérateur (0..255 ; étalonnés à 1 et 255) ; le volant (BIND_GetSteeringWheelAngle)
    entre 1 à gauche et 127 à droite, 64 au milieu, l'étalonnage déjà enregistré dans `Setting_Test_Input.ini`.
- Retour de force ⚠️ : BIND_SetSteeringWheelData reçoit « 3 64 0 0 » à l'ouverture, « 0 108 0 0 » puis « 3 108 0 0 »,
  et « 3 108 5 0 » un instant aux chocs ; joué comme un ressort de centrage (3, force 108) et une secousse (5). Le sens
  de ces valeurs est supposé.
- vJoy ✅ : `--controllers`, course, 512 changements de force envoyés au joueur 1.
- Affichage ✅ : 2048×1280, recopie GDI.
- Menu Tab ✅ : pause, pages Jeu, Retour de force.
- Menu de service ✅ : bouton vert, Main Menu, Hardware Test, Input Test (pièce, Start, siège, étalonnage du volant et des
  pédales).
- Crédits ✅ : jeu gratuit (`Setting_Pay.ini`), pièce comptée (Input Test).

## Wangan Midnight Maximum Tune 5 (Namco System ES3X, 64 bits)

`wmn5r.exe` (copie `games\wmmt5`, 18,5 Go depuis racecab1), x64, Direct3D 11. Dans `namco.cpp`, à côté de Mario
Kart DX. Les deux crochets de TeknoParrot (`d3d11.dll` et `dinput8.dll`, le plugin FFB), OutputBlaster et `SDL2.dll`
mis de côté (`_teknoparrot_off`). Le jeu écrit ses erreurs dans le journal (« [WMN5Err] »).

Fait :
- la clé Sentinel HASP : ses cinq fonctions (login, logout, get_size, read, decrypt, importées par numéro)
  remplacées dans la table d'import. Sa mémoire : 0xD40 octets, l'identifiant de la borne à 0xD00 suivi de sa
  somme, deux enregistrements à 0x10 et 0x20 (valeurs d'OpenParrot) ;
- E1911 « USB DONGLE error2 » : l'identifiant de la clé est comparé à celui que donne le composant COM « auth »
  de la borne, absent ici (« auth: can't get instance(80040154) »). La comparaison est sautée, comme dans
  OpenParrot ;
- les lecteurs F: et G: : les fichiers `F*` et `G*` du dossier ;
- le JVS sur COM3, celui de Mario Kart DX : « I/O CHECK OK » (la carte du volant sur COM1 a son propre
  protocole, voir le retour de force plus bas) ;
- E0505 « network: interface not found » : une carte réseau de borne, 192.168.92.11. Ajouts dans `netenv.cpp`
  (`GetIpAddrTable`, `GetInterfaceInfo`, `GetIpForwardTable`) et `system.cpp` (un `connect` UDP vers 8.8.8.8
  suivi de `getsockname` répond l'adresse de cette carte) : « LINK OK » ;
- le terminal de la salle (25/10) : la borne écoute sur le port UDP 50765 la machine des cartes, 192.168.92.20 ;
  muet, il donne E0505 « Network disconnection error / Disconnected from local network » 35 s après le
  démarrage. windowsloader en joue un : les six paquets « free play » de l'émulateur de terminal d'OpenParrot
  (GPL-3.0), envoyés toutes les 8 ms vers 127.0.0.1:50765 seulement (rien ne sort du PC), et montrés au jeu
  comme venant de 192.168.92.20 (l'adresse de l'expéditeur réécrite à la réception). Le lien avec le terminal
  est tenu pour établi (un saut, comme OpenParrot pour WMMT6R) : sans lui, « TERMINAL UNIT LINK NG » et arrêt
  sur « GAME SERVER CONNECT CHECK ». Les trois sauts de la recherche de terminal posés avant ne servent plus,
  retirés. Le jeu arrive en démonstration, « FREE PLAY ».

- Commandes ✅ (25/10) : la table propre au jeu, relevée dans le test des contacts du menu de test
  (スイッチテスト) : Test, Service, haut, bas, Entrée, vue (0x0002), interruption (0x0001) ; le levier en H sur
  quatre contacts (0x80 haut, 0x40 bas, 0x20 gauche, 0x10 droite), rapports 1 à 6 par Gear1..Gear6, ou montés
  et descendus par ShiftUp / ShiftDown ; l'option « Garder le rapport » (`HOLD_GEAR`) pour un levier dont les
  positions ne tiennent pas ; volant, accélérateur et frein sur les voies 0, 1 et 2. Contacts d'essai Bit0..Bit15
  si la variable `WL_WMMT5_PROBE` est définie (`wmmt5_named.py`, `wmmt5_shift.py`).
- Course ✅ : pas de bouton Start, l'accélérateur valide et le volant choisit : « Do you have a BANAPASSPORT
  CARD? » (non), marque, voiture, couleur, boîte AT ou MT, mode Story, épisode, puis la course à Yokohama
  (Minato Mirai), 120 km/h et plus ; le volant tourne la voiture du bon côté (`wmmt5_play.py`).
- Menu de service ✅ : test des contacts, étalonnage du volant (アナログボリュームの初期化, ハンドル 000 après
  Entrée), réglages du jeu ; tout est enregistré dans le dossier d'écriture quand il y en a un.
- Retour de force ✅ (25/10) : c'est un réglage du jeu, « ステアリング反力の使用 » dans ゲーム設定, à OFF dans la
  copie ; à mettre à ON dans le menu de test. Le jeu ouvre alors COM1 à 115200 bauds par boost::asio et le rattache
  à un port de complétion, ce qu'un handle émulé (un événement) ne permettait pas (« [STR3] initialize: open:
  Descripteur non valide ») : `vdev.cpp` garde le port et sa clé, et chaque lecture ou écriture se termine par un
  paquet posté (`NtSetIoCompletion`, pour qu'une lecture annulée échoue comme celle d'un fichier). La carte
  « STR3 » (classe `FFBIo`) répond en 3 caractères : « E00 » à l'ouverture ; « C06 » ou « C01 » quand la sortie
  JVS 0x80 du premier octet (l'alimentation du moteur) tombe ou monte, ce que le jeu fait au démarrage puis en
  début de partie ; et à chaque commande de 10 octets (celles de Mario Kart DX : cible, raideur, amortissement,
  vibration), « H » suivi de la position du volant sur 10 bits. Sans l'une de ces réponses : E2212 « STR PCB
  Link error ». Le ressort pousse vers le centre (volant à gauche : force vers la droite, et l'inverse), la
  vibration passe ; `RawDrive` sort sur le serveur réseau en course (148 trames).

Reste :
- le lecteur de cartes (« BANAPASSPORT CARDs cannot currently be used »), la caméra ;
- le mode avec pièces : les paquets « coin » du terminal, à proposer en option ;
- vJoy, l'affichage, le menu Tab.

Sur les bornes (Image borne, 25/10) : la -4 puis la -5 posées, le réglage à ON sur les deux ; en course sur
racecab2 le ressort ramène vers le centre (±0,24 à ±0,6 de braquage), pas d'E2212 en 5 minutes. Sur racecab1, la
page des réglages affiche en rouge sous 基板ID = 1 « 同一の基板IDの筐体が存在します » (une borne de même
identifiant existe). La cause : le Wangan d'essai de ce PC, sur le même réseau local (192.168.1.134), avec le même
identifiant, au même moment (20:28) ; il s'est arrêté lui-même sur E2403 « Duplicate Board ID error ». Le jeu
annonce sa borne au groupe 225.0.0.1:50765 par `WSASendTo` (boost::asio), que les refus de `system.cpp` (`sendto`,
`connect`) ne voient pas : ses envois sortaient du PC. Corrigé le 25/10 : ses envois vers une autre machine vont à
127.0.0.1:50766 (le socket du terminal, qui ne lit rien), et ses paquets reçus d'une autre machine sur le port 50765
lui arrivent vides (il les écarte). Vérifié : une « autre borne » simulée depuis 192.168.1.134 ignorée, le 5 et DX+
en démonstration normalement.

## Wangan Midnight Maximum Tune 5 DX+ (Namco System ES3X, 64 bits, 2016)

`wmn5r.exe` comme le 5 (copie `games\wmmt5dxp`, 15,2 Go depuis racecab1), reconnu à sa bibliothèque de clé
`hasp_windows_x64_106482.dll` ; ses fichiers `F*`/`G*` en `W5P`. Le crochet FFB de TeknoParrot (`d3d11.dll`) et
`SDL2.dll` mis de côté (`_teknoparrot_off`). Tout le 5 repris (carte JVS et commandes, carte du volant « STR3 »,
terminal joué, dossier d'écriture), avec ce qui change (25/10) :
- la clé : `hasp_write` (n° 17) importé en plus ; l'identifiant de la borne doit commencer par 2783 (comparé à
  0xadf, +0x89db0d : sinon E1911 « USB DONGLE error2 ») : `278313042069`, celui d'OpenParrot pour DX+ ;
- E1911 aussi par la comparaison avec le composant « auth » : le même saut que pour le 5, `mov sil, 1` à
  +0x706354 (la place trouvée par le motif d'OpenParrot) ;
- le terminal : le numéro de série du 5 dans ses paquets donnait E2403 « Duplicate Board ID error » puis
  « TERMINAL UNIT S/N » en rouge ; les six paquets sont maintenant faits au démarrage avec le numéro du jeu
  (`278311042069` pour DX+), leur CRC recalculé ;
- le lien avec le terminal : les champs de l'objet sont 0xf0 plus loin (état +0x750) ; l'étape d'attente
  (+0x708685) passe à « complete » (+0x708809), comme au 5 ; démonstration « FREE PLAY ».
- Course ✅ : les mêmes écrans que le 5 (pas de carte, marque, voiture, couleur, boîte, Story, épisode), course à
  Yokohama, près de 200 km/h ; le volant tourne la voiture du bon côté.
- Menu de service ✅ : en anglais (MENU (DRIVE UNIT)) ; GAME OPTIONS → STEERING POWER, OFF dans la copie, à mettre
  à ON (« becomes usable when test switch is off »).
- Retour de force ✅ : la même carte du volant (E00, C06/C01, « H » et la position) ; volant à gauche force
  −0,25, à droite force positive, vibrations (`wmmt5_play.py` avec `WL_GAME`).

Reste : le lecteur de cartes, la caméra, le mode avec pièces ; vJoy, l'affichage, le menu Tab. Dans la release
20261025-6, posée sur les deux bornes (STEERING POWER à ON sur racecab1).

## Wangan Midnight Maximum Tune 6 (Namco System ES3X, 64 bits, 2018)

`wmn6r.exe` (copie `games\wmmt6`, 16,3 Go depuis racecab1). Le crochet FFB de TeknoParrot (`d3d11.dll`) et
`SDL2.dll` mis de côté. Les mêmes cartes et le même terminal que le 5 ; le code des trois Wangan réuni dans une table
de `namco.cpp` (ce qui change d'une version à l'autre). Ce qui est propre au 6 (25/10) :
- la clé : `hasp_windows_x64_28756.dll`, identifiant `280813990002` (2808 comparé à +0x8bbbc2) ; le terminal
  `280811990002` ;
- les lecteurs F:/ et G:/ (`F:/BK_WM6_DriveOpe.bin`…) dans le dossier `TP\` de la copie, comme OpenParrot ;
- le lecteur Banapass par `bngrw.dll`, comme Mario Kart DX 1.18 (sans carte), ses fonctions en 64 bits par leur nom,
  plus `BngRwGetVersion` et `BngRwReqAction` ;
- le démarrage : le saut du contrôle de la clé (+0x69e928) et du lien avec le terminal (+0x6a0c86 → +0x6a0d5c), comme
  au 5 ; « CERTIFICATE CHECK » restait vide : l'état comparé à 2 par le lien terminal (+0x718) mis à 2 quand
  l'authentification n'en a aucun (le NOP d'OpenParrot, +0x690876), avec son autre NOP de borne (+0xa131b3) ;
- « GAME SERVER CONNECT CHECK » restait sur CHECKING : le 6 attend la réponse du serveur ALL.Net (Mucha, par le démon
  `AMAuthd.exe` que TeknoParrot lance avant le jeu et que le jeu joint par COM, absent ici : E0516 « ALL.Net AUTH
  ERROR » au journal) ; l'étape passe sans elle (+0x6a0df3) ;
- E0552 « OFFLINE UPTIME ERROR » ensuite : 36 h depuis la dernière authentification, jamais faite ; la limite tenue
  pour non atteinte (+0x6916bd), comme pour Mario Kart DX. Son avertissement une heure avant (35 h, +0x68f6a0 :
  « Please connect this unit to the network. The Game will shutdown shortly. » sur tout le jeu) n'est plus montré
  (26/10).
- Menu Tab ✅ (26/10) : les cinq pages, le jeu en pause (images 2403 → 2403), reprise à la fermeture.
- Course ✅ : les mêmes écrans que le 5 (pas de carte, Story, épisode 1, Yokohama).
- Menu de service ✅ : en anglais ; GAME OPTIONS → STEERING POWER, OFF dans la copie, à mettre à ON.
- Retour de force ✅ : la même carte du volant ; volant à gauche force −0,25, à droite force positive, vibrations.

Reste : les cartes (le lecteur Banapass est joué sans carte : on joue sans), la caméra (refusée, le jeu s'en passe),
le mode avec pièces (le terminal joué est en jeu gratuit) ; vJoy et l'affichage à voir sur borne. Dans la release
20261025-7 (pas encore posée : Defender).

## Sega World Drivers Championship (Sega ALLS, Unreal Engine 4, 64 bits) — jouable en invité

`App\Todoroki\WindowsNoEditor\Todoroki\Binaries\Win64\Todoroki-Win64-Shipping.exe -windowed` ; le lanceur y ajoute
`-launch=Cabinet` (borne complète) ou `-launch=MiniCabinet` (petite borne, comme TeknoParrot) selon l'option
`[Game.swdc] CABINET` (`CABINET` par défaut, `MINI`), et démarre 5 s avant lui `AMDaemon\amdaemon.exe`, le service de
la plateforme (programme compagnon), avec le `config.json` du jeu et les réglages de borne seule de windowsloader
(`%TEMP%\windowsloader-swdc-amdaemon.json`, écrit par le module dans amdaemon). Modules `src/hook/games/alls.cpp`,
`src/hook/games/allnet.cpp` (les services réseau), `src/hook/netenv.cpp`. Copie locale le 23/10 (12,5 Go, 95 min).

État au 26/10 (deux parties invité en mode agent sur ce PC, avec les réglages par défaut du lanceur) :
- Lancement ✅ : test de démarrage, ALL.Net, Aime, UserDB et NetBattle « Online », avertissement, attract.
- Commandes ⚠️ (agent) : pièce, Start, gauche (はい de la connexion invité), accélérateur, volant.
- Retour de force ⚠️ (agent, sans volant physique) : commandes 84 (couple) de la carte du volant sur COM1, 5944
  changements de force en une course, de −0,96 à +0,96 ; volant à −0,5 force moyenne +0,16, à +0,5 −0,18, au centre
  +0,02 : vers le centre, comme Wangan 6 ; 0 aux résultats.
- Crédits ⚠️ : pièces comptées (« CREDIT(S) »), la partie invité en prend une.
- Lecteurs (27/10) : sur racecab2 (graveur DVD HP en E:), « Error 0919 DVD Drive Remain Error » au bout de 14 s :
  amdaemon cherche un lecteur de CD/DVD parmi les volumes (`FindFirstVolumeW`, `QueryDosDeviceW` → `\Device\CdRom`,
  `GetDriveTypeW`) ; 0917 pour une clé USB. windowsloader lui cache (et au jeu) les lecteurs de CD/DVD et les disques
  amovibles ; D:, E: et Y: (les dossiers de la copie) sont des disques fixes. Reproduit sur le PC d'essai avec une
  image ISO montée en E: (0919 en 10 s), plus d'erreur avec le correctif ; pas encore rejoué sur la borne.
- Langue : `[Game.swdc] LANGUAGE` (`ENGLISH` par défaut, `JAPANESE`, `CHINESE`, les noms anglais dans le menu) pour
  les messages d'amdaemon seulement ; les textes du jeu restent en japonais (son pak n'a que la locale « en » du moteur).
- Mode Cabinet (27/10) ✅ : avec les services réseau joués sur le PC et l'installation LAN coupée, plus de 8404 ni de
  4103 ; une partie invité complète (course, résultats, continue, game over), le retour de force comme en MiniCabinet.
  C'est maintenant le mode par défaut (option `CABINET`, prise au lancement : le lanceur donne `-launch=`, sauf si la
  ligne de commande en a un). Le jeu n'ouvre COM2 (carte LED) et COM20 (tableau de bord) qu'en mode Cabinet.
- Boutons du volant ✅ (test d'entrées) : en mode Cabinet, par le tableau de bord (838-15415, COM20) : le jeu envoie
  « pr » toutes les 17 ms environ, la réponse « ps » et six chiffres 0/1 : jaune, rouge, vert, bleu, palette droite,
  palette gauche. En MiniCabinet, le jeu les lit par amdaemon (mot du joueur 2 de la carte IO4, `config.json`), mais
  amdaemon ne transmet pas ce mot (essayé : FFFF, octets 34 à 63, max_player 2) : les boutons du volant n'y marchent pas.
- Sorties réseau ✅ (mode Cabinet seulement, déclarées selon le `-launch=` du jeu) : le tableau de bord reçoit
  « sp1*0*2*:gr2:le11110000:pr » toutes les 35 ms en course (vitesse sur trois chiffres suivis de « * », rapport,
  8 lampes en barre, « rp » toujours vide) → `speed`, `gear`, `rpm`, `dash_lamp0` à `dash_lamp7` ; la carte LED
  (837-15070, COM2) 9 LED RVB (commandes 31, 32 clignotement, 33 fondu, appliquées par 3C) et 3 sorties de
  puissance (39) → `led0_r` à `led8_b` (0-255), `fet0` à `fet2`. Course enregistrée sur le port 8000 : `speed` 116
  valeurs (74 à 116 km/h), `gear` 2, 3, 4, `dash_lamp0` 0/1, les LED en rouge, orange, jaune, `fet0` 0/1. Le
  clignotement et le fondu ne sont pas rejoués : leur couleur est envoyée telle quelle.
- Menu Tab ✅ : le jeu en pause, les sept pages (options du jeu : `CABINET`, `LANGUAGE`, les cartes).
- Menu de service ⚠️ : Test ouvre le « GAME TEST MENU » (Service pour choisir, Test pour valider) ; test d'entrées
  ✅ (volant ±128, accélérateur 255, frein, boutons ; Test et Service ensemble pour sortir) ; le test du retour de
  force reste sur « CHECKING... » (le jeu interroge la carte par FD, notre réponse 00 ne lui suffit pas ; ce test
  est dans les Blueprints, segatools n'y répond pas non plus).
- vJoy ⚠️ (`--controllers`, une course de 30 s) : vJoy ouvert en joueur 1, retour de force à 100 % ; le nombre
  d'effets envoyés pas compté (le script s'arrêtait en pleine course). Images par seconde en course : 19 en moyenne
  avec les manettes, 26 à 29 sans, sur trop peu de course pour conclure.
- Pas encore : le serveur de matching (« GAME SERVER BAD » au démarrage, pas de course en réseau), un essai au vrai
  volant, le test du retour de force.

Avant (23/10) :
- Lancement ⚠️ : amdaemon démarre sans erreur (registre `SystemProperty` inventé en mémoire, clé `\??\FddDriver`,
  moniteur matériel, pilote d'affichage `$amvideo`, lecteurs `D:`/`E:`/`Y:` dans la copie, carte d'E/S USB « IO4 »,
  lecteur de cartes Aime sur COM3, afficheur VFD sur COM4, carte réseau de borne 192.168.172.11) ; le jeu passe
  « Core Setup », « Aime Setup », la carte d'E/S, le tableau de bord (838-15415, texte sur COM20 : `pr` → `ps` et
  l'état de ses boutons ; commandes `:sp`, `:rp`, `:gr`, `:le`), puis le test réseau de démarrage en borne
  principale (`config_LanServer.json` ; `Y:\SDDS\LanServer.dat` dans la copie). Il s'arrête vers 70 s sur « Error 8404 »
  (serveur d'installation LAN introuvable ; amdaemon lit `E:\ICF1`, absent de la copie) ou « Error 4103 » sans
  installation LAN, juste après une requête HTTP sans adresse (serveur de jeu donné par ALL.Net, coupé). Pour l'essai,
  ALL.Net (authentification, comptabilité) et la monnaie électronique sont coupés par une config de plus passée à
  amdaemon.
- Affichage ⚠️ : seulement avec `-windowed` (sinon le jeu se réduit, notre refus du plein écran répondant S_OK).
- Le reste pas encore vu.

Repris le 26/10 (« avec segatools ») :
- Sans réglage de plus, amdaemon tente la livraison réseau et s'arrête (`amGfetcherInit(). ErrCode -1`) : le jeu
  affiche Error 4105 après « Aime Setup ». Avec ALL.Net (authentification et comptabilité), la livraison réseau,
  la monnaie électronique, les journaux ABAAS et l'installation LAN coupés par une config de plus
  (`WINDOWSLOADER_COMPANION_ARGS=-c <fichier>` pour l'essai), il passe « Aime Setup », « Device Setup », le test
  réseau (IP, passerelle bonnes ; DNS mauvais), « CONNECT MAIN CABINET », puis Error 4103.
- 4103 : à la fin de sa séquence `InitializeTest`, le jeu interroge son serveur de jeu (`FDBAccessor::CheckServer`,
  `/api/server/alive`) à l'adresse que l'ALL.Net lui donne (`gameServerUri`) ; coupé, l'adresse est vide
  (« libcurl error: 1 (Unsupported protocol) »). Ce serveur est un serveur de jeu Sega « ABaaS » (API REST
  `/api/user/load`, `/api/user/save`, `/api/ranking/...`, `/api/data/...`, réponses à `control.seq_id`).
- segatools (`swdchook`, domaine public) émule la plateforme, comme windowsloader le fait déjà (IO4, Aime, VFD,
  moteur, réseau de borne), et renvoie l'ALL.Net vers un serveur qu'on fait tourner soi-même ; il demande aussi les
  ICF du jeu (absents de la copie). Aucun serveur libre connu ne sert SWDC (ARTEMiS : non) ; TeknoParrot le fait
  tourner sur ses propres serveurs en ligne (`Offline Mode=0`, `OnlineID` dans sa config).
- Reste : soit un serveur de jeu ABaaS minimal (son protocole à retrouver dans le jeu), soit un jeu hors ligne
  obtenu en neutralisant ses contrôles réseau ; l'un comme l'autre est un chantier à part.

Les services réseau joués sur ce PC (26/10, `allnet.cpp`) :
- ALL.Net, dans amdaemon : les noms de naominet.jp → 192.0.2.1 (la bibliothèque refuse 127/8), ses connexions
  (port 80) → un serveur sur 127.0.0.1:40180 ; PowerOn et DownloadOrder répondus.
- Aime (AiMeDB, port 22345 → 40182, dans amdaemon) : en-tête de 32 octets, AES-128-ECB ; chaque commande reçoit
  la suivante (statut 1), la recherche d'un joueur -1, « au revoir » (66) ferme. Le jeu le voit « Online » ; la
  partie invité n'en demande pas plus.
- Le serveur de titre (ABaaS GS, dans le jeu, 127.0.0.1:40181) : réponses = en-tête de 16 octets (01 01, statut, CRC32
  et longueur du JSON, 4 octets) puis AES-256-CBC(zlib(JSON)), la clé prise à `BCryptGenerateSymmetricKey` du jeu ;
  free_buckets : réglages de saison, titre de saison, réglages recommandés (`update_time`, sinon division par zéro
  dans DecideTimeSale), événement (manches), en chaînes JSON aux valeurs en chaînes, heures en secondes Unix.
- Le mode MiniCabinet sans 8404 ni 4103 ; « CONNECT MAIN CABINET » passé par la diffusion UDP en boucle locale
  (40257 vers 192.168.172.255).

## Initial D Arcade Stage Zero Ver.2 et Ver.1.31 (Sega Nu, 64 bits, 2017)

`InitialD0_DX11_Nu.exe` (copie `games/idz230`, 14,1 Go depuis racecab1), Direct3D 11, 2560x1440 (son
`initialD.ini`). Module `alls.cpp`, ouvert à une seconde plateforme d'amdaemon : la Nu. Trois programmes
démarrés par le lanceur dans le dossier du jeu : `amdaemon.exe` (les réglages export de la première borne,
`configDHCP_Final_Common/EX/EX_ST1.json`, puis ceux de windowsloader pour une borne seule), le server box du
magasin `ServerBoxD8_Nu_x64.exe` (sa fenêtre cachée), puis le jeu. Ce que la plateforme trouve (segatools,
leurs clés et valeurs) :
- le registre `SystemProperty` (jeu SDDF, plateforme AAV2), la clé `FddDriver`, le moniteur ;
- la carte AMEX par ses interfaces de périphérique : la SRAM (512 Ko, `wl_sram.bin`), l'EEPROM (8 Ko,
  `wl_eeprom.bin` ; les deux dans le dossier d'écriture quand il y en a un, depuis le 27/10), la puce DS (l'identité de la carte, région export), le GPIO (DIP 1 : la première borne),
  le pilote JVS (salut, ligne de détection, échange : `jvs.cpp`) ;
- la carte d'E/S JVS 837-14572 : boutons des menus et de vue, levier en H à 6 vitesses (Gear1 à Gear6,
  GearUp/GearDown), volant, accélérateur, frein ; le lecteur Aime 837-15286 sur COM10 ;
- les lecteurs E: et Y: dans `TeknoParrot` de la copie ; ALL.Net et Aime joués sur ce PC (`allnet.cpp`).

- Lancement ✅ : réglage du volant (Start), « Initializing », attract (ALL.Net, Xeen, titre, démo). Sans le
  server box : ERROR 8306 ; avec lui (diffusions UDP en boucle locale), l'attract.
- Commandes ✅ (agent, INPUT TEST) : volant −128 à 127, accélérateur 0 à 255, boutons, levier N, 1 à 6. Les
  pédales sur 0 à 0x3000 (27/10) : le jeu montre 0 à 255 sur cette course et reste à 255 au-delà (les
  potentiomètres d'une borne ne tournent pas plus loin) ; avant, elles saturaient à 19 % de la course.
- Partie invité ✅ : deux pièces, Start, « sans données », choix de la voiture, AT/MT, Story, STAGE 0,
  course (100 à 145 km/h).
- Retour de force ✅ : la carte du moteur Sega sur COM1 (`servoboard.cpp`), en statut long après FA 00 10
  (DRIVEBOARD.md de BackForceFeeder) ; FORCE FEEDBACK TEST : LEFT +0,19, RIGHT −0,19 ; en course, couple et
  sinus (environ 580 changements par segment). Le même changement sert Initial D 6 à 8.
  Vérifiée au protocole (27/10, DRIVEBOARD.md) : 80 (mode court : la force retombe 240 ms après la dernière
  trame ; l'activation), 83 (gain), 84 (couple), 85 (sinus : l'octet 1 la fréquence en demi-hertz, l'octet 2
  l'amplitude, joué par un fil toutes les 10 ms), FF (valeurs par défaut). Une seule émulation dans
  `servoboard.cpp` (`driveboard.cpp` jusqu'au 27/10) pour les jeux Sega à servoboard série : Initial D 6 à 8,
  K.O. Drive, Sonic, SWDC, Initial D Zero. Les cartes moteur (« drive boards » : Sega Rally 3 par le port
  0x1020, Daytona, Ford Racing, GRID, DiRT par le port parallèle, Sega Racing Classic) gardent leur décodeur
  dans le module de leur jeu. Revérifié en course : Initial D 6 (forces comme avant), Initial D Zero
  (vibration ±0,03, couples −0,23 et +0,25), SWDC (80 00 01 avant la course, couples envoyés).
  D'après cinq relevés de vraies bornes (mesures de linuxloader, 27/10) : la recherche d'index après FC dure
  4,05 s (non plus 2 s) ; FF répond dans le mode qu'il termine (94... en mode 14) ; FA 00 00 à 0F relisent
  les valeurs des dernières commandes 80 à 8F (FA 00 00 puis 80 01 01 : 80 01 01 00). Vérifié par un banc
  d'essai qui rejoue ces exemples, puis en course sur Initial D 6 et Initial D Zero v1.31.
- Lampes : la carte 837-15070 sur COM11 (comme celle de SWDC).
- Ver.1.31 ✅ (27/10, copie `games/idz131` de racecab1, 12,3 Go, le jeu dans `app\package`) : le même module.
  Deux différences : son amdaemon (de mars 2018) cherche les noms d'ALL.Net en ROT13 (`anbzvarg.wc`,
  `nvzr.anbzvarg.wc`), que `allnet.cpp` décode maintenant ; ses `ICF1`/`ICF2` sont dans le `amfs` de segatools à
  côté de `app\` (le `TeknoParrot\AMFS` de la copie n'a que `emoney`), pris pour E: quand ils manquent ailleurs.
  Vérifié en mode agent : ALL.Net (PowerOn `ver=1.31`), Aime, réglage du volant, partie invité (Prologue,
  STAGE 0, course jusqu'à 170 km/h), retour de force (couples jusqu'à ±0,42). Le sélecteur la distingue de
  la Ver.2 par l'horodatage de son exécutable. Une ERROR 8304 « Failed in connect(GAME) » passagère (le jeu se
  relance seul jusqu'à l'attract) quand la SRAM d'un lancement précédent est reprise.
- Menu Tab ✅ (27/10, Direct3D 11, vérifié en Ver.1.31) : pages, reprise du jeu.
- Reste : les cartes Aime et le jeu en ligne (le serveur UserDB du jeu, que le server box joint ; protocole
  binaire chiffré, minime en a une version) ; un vrai volant sur borne.

## Frenzy Express (Uniana PC, 2001)

`xwin.exe` (copie `games\frenzyexpress`, 130 Mo depuis racecab1), x86, OpenGL, 640×480. Module `uniana.cpp`. Une
trottinette : guidon, poussées du pied à gauche et à droite, frein, six boutons. La borne passe par `xio.dll` (classe
`XIO`) : sa fonction `update`, appelée à chaque image, est celle de windowsloader et remplit les variables de la
bibliothèque que le jeu lit ensuite (la copie des bornes a une version clavier de `xio.dll`).

- Lancement ✅ : étalonnage du guidon au démarrage, « INITIALIZE » tout OK (I/O PORT compris), avertissement, titre,
  attract. L'étalonnage (xwin.exe+0x25c00, 300 images par étape) : le moteur pousse le guidon à droite, le jeu en
  prend la position, puis à gauche, puis le ramène au milieu ; « ERROR THE STEERING WHEEL » puis « DEFAULT SETTING »
  (0x00, 0x80, 0xFF) si la position de gauche n'est pas sous celle de droite. Un guidon que le moteur ne tourne pas
  (un volant sans retour de force, vJoy, ou un retour de force trop faible ou inversé) donnait l'erreur, et un faible
  une course trop courte. Depuis le 28/10, l'étalonnage reçoit le guidon qu'il attend : 0xFF à droite, 0x00 à
  gauche, 0x80 au milieu (la plage de windowsloader) ; il passe, le moteur joue toujours.
- Commandes ✅ (agent, vues dans I/O CHECK du menu de test) : guidon (0x00 à gauche, 0xFF à droite), frein, poussées
  gauche et droite, Start (bit 0x01), Service (0x02), Test (0x04, ouvre le MAIN MENU), pièce (0x08 ; Service compte
  aussi une pièce), choix gauche et droite (0x10, 0x20). L'accélérateur (la poignée des bornes moto) alterne les deux
  poussées, plus vite quand il est tourné à fond : 54 km/h. Partie : deux pièces = un crédit, choix du joueur, du
  costume et du pied, mission, course.
- Retour de force ✅ : `setSteeringBar` est le moteur du guidon (0 : aucun ; impair 0x0B..0x17 : vers la gauche,
  pair 0x0A..0x16 : vers la droite, sept forces). Le sens a été vérifié par l'étalonnage (avant qu'il reçoive le
  guidon attendu) : un guidon qui suit la force le passait, l'autre sens donnait « ERROR ». En course : un ressort vers
  le milieu, plus raide avec la vitesse.
- Sorties réseau : `lamp` (motif), `start_lamp`, `select_lamp`, `vibration` (3 au repos), `handlebar_motor`.
- Affichage ✅ : le jeu dessine 640×480 dans le coin d'une fenêtre de la taille de l'écran. Son module donne la
  taille (`wl_gl_picture`) ; depuis le 27/10, comme Initial D 6 : le jeu dessine dans le framebuffer de
  windowsloader, ce coin est recopié là où va l'image, la fenêtre réglée selon `DISPLAY_MODE`. Le jeu limitait sa
  fenêtre à 640×480 hors tout (`WM_GETMINMAXINFO`, la zone cliente restait à 634×451) : il reçoit maintenant les
  limites de Windows.
- Menu Tab ✅ (27/10) : comme Initial D 6 (OpenGL), sur l'image placée par windowsloader.
- Reste : vJoy et un vrai guidon sur borne, les lampes vues sur borne.

## D1GP Arcade (Taito Type X2, 2009)

`game.exe` dans `games/d1gp` (avec `IFC23.dll` et `game.inf`), v2.02 GBR, x86, Direct3D 9, 1360×768. Dans `taito.cpp`
(D:\ et E:\ redirigés comme pour les autres Taito).

- Lancement ✅ : « Network: initialize », « Connection: FAILURE » (35 s, pas de réseau de bornes), « DANGER! Handle
  initialize » (le volant se centre par le retour de force, 10 s), avis, logos, vidéo, démo, classements.
  - COM2 : la bibliothèque « Wdt » (chien de garde) envoie d'abord `05 03 e8` au nœud 0, sans réponse utile (le jeu
    continue), puis la bibliothèque « Xio » de Taito y parle JVS : la carte de Battle Gear 4 lui convient.
  - Le lecteur de cartes magnétiques sur COM1 n'est pas émulé (refusé) : « CARD VENDOR » / « ERROR » en bas de l'écran,
    le jeu se joue sans carte (« Will you use a card? » → Start).
- Commandes ✅ (mode agent, `scripts/agent/race_check.py d1gp`) : pièces (« Credit(s) 2 »), Start, mode, voiture, boîte
  AT, Drift Lesson, course (73 km/h) ; les menus se choisissent aussi au volant et à l'accélérateur.
  - JVS, trouvés par le test d'E/S avec des bits de sonde : START bit 3, VIEW CHANGE 2, SHIFT DOWN 8, SHIFT UP 9,
    SIDE BRAKE 10, SERVICE 14, TEST le bit système 7.
  - Volant et pédales par DirectInput : le jeu (DirectInput 8 en Unicode) ne garde que les manettes à retour de force
    et lit `<nom du produit>.txt` (les espaces en « _ ») pour ses boutons et axes. windowsloader lui donne un volant
    « Immersion TouchSense Steering Wheel » (le fichier de la copie, toutes valeurs à 0, convient) : X le volant,
    Y l'accélérateur, Z le frein. Le volant DirectInput de windowsloader a maintenant l'interface Unicode
    (IDirectInputDevice8W, vdinput.cpp).
- Retour de force ⚠️ : par la DLL Immersion (IFC23.dll), qui crée elle-même un DirectInput 7 par COM
  (CoCreateInstance de CLSID_DirectInput, en IDirectInputA) et y cherche un joystick à retour de force. windowsloader
  y accroche les mêmes méthodes que pour DirectInput 8 (system.cpp, dinputhook.cpp) et lui donne le même volant, avec
  les types de DirectInput 7 (les méthodes sont aux mêmes places) : effets constants, ressort, amortisseur et
  frottement, périodiques, joués sur la force de windowsloader (le ressort ramène au centre). Les sorties JVS de D1GP
  ne passent plus par le moteur de Chase H.Q. 2, qui remettait la force à 0 à chaque trame. Sens à confirmer au
  volant ; amortisseur et frottement non joués (vitesse du volant inconnue).
- vJoy ✅ : `--controllers`, course, 512 changements de force envoyés au joueur 1.
- Affichage ✅ : 1360×768 à 30 images/s, recopie GDI.
- Menu Tab ✅ : pause, pages Jeu (JVS sur COM2, volant DirectInput, retour de force Immersion), Affichage, Retour de
  force.
- Menu de service ✅ : TEST MODE (bouton Test), VIEW CHANGE descend, SHIFT DOWN remonte, START choisit ; test d'E/S.
- Crédits ⚠️ : pièces JVS comptées ; le lecteur de cartes et le distributeur de cartes (« CARD VENDOR ») absents.

## Valve Limit R (Taito Type X, 2005)

`launcher.exe` dans `games/vlimitr` (avec `game.inf` et `data\bin2`), VL1.34JA, x86, Direct3D 9, 640×480 en 16 bits
(R5G6B5). Dans `taito.cpp`, avec Battle Gear 4 (D:\ et E:\ redirigés, la première manette DirectInput cachée du jeu).

- Lancement ✅ : « Network Checking », « Loading », démo (classements, « Top Class Idols », vidéo).
  - COM2 : le JVS par la « BG3 I/O Libraly » de Taito ; la carte de Battle Gear 4 lui convient. COM1 : la carte du volant
    Taito (38400 bauds, commandes de 2 octets), celle de Battle Gear 4, déjà émulée.
  - Premier démarrage : « PLEASE CALIBRATE I/O DEVICE / PRESS TEST BUTTON ». Les pédales s'étalonnent dans le menu de
    test (I/O CHECK > ACCEL and BRAKE CALIBRATION : chaque pédale « Push Full » 5 s, « Release » 5 s, puis SAVE AND
    EXIT) ; le jeu l'écrit dans `expdat` / `extdat` à côté de l'exe, et ne le demande plus ensuite. Fait dans la copie.
  - Les images de l'agent échouaient : le fond d'image est en 16 bits ; la capture Direct3D 9 de windowsloader convertit
    maintenant R5G6B5, X1R5G5B5 et A1R5G5B5 (`d3d9hook.cpp`).
- Commandes ✅ (mode agent, `scripts/agent/race_check.py vlimitr`) : pièce (« CREDIT(S) 1/1 »), Start, passagère,
  voiture, départ, course en ville.
  - Le jeu traduit les octets du JVS par une table (+0x41ab60, table à 0x4c3c78) : TEST le bit système 7 ; START bit 15,
    SERVICE 14, NITRO 13, VIEW 12, SIDE BRAKE 11, SHIFT UP 10, SHIFT DOWN 9 (vus au test d'E/S avec des bits de sonde) ;
    l'accélérateur sur la voie analogique 1, le frein sur la 2 (sa bibliothèque lit les voies 1 à 3) ; le volant par la
    carte du volant (STEERING ±0,80 au test).
  - Test n'est pris qu'en front, à l'écran qui l'attend (l'état 5 du démarrage, qui alterne avec la démo) : un appui
    pendant la démo ne fait rien.
- Retour de force ⚠️ : le jeu pilote la carte comme Battle Gear 3, par paramètres d'effets que la carte applique (pas de
  couple en continu par `0D` comme Battle Gear 4) : `01` couple constant (chocs, 0x80 nul), `03` ressort (04 à 06),
  `04` amortisseur, `05`/`06` vibration (8 Hz, amplitude 0x28 ou 0). windowsloader joue le couple, le ressort et la
  vibration (échelles du ressort et de la vibration supposées), pas l'amortisseur ni le frottement (vitesse du volant
  inconnue) ; réservé à Valve Limit R, Battle Gear 4 inchangé. Sens et forces à confirmer au volant.
- vJoy ✅ : `--controllers`, course, 362 changements de force envoyés au joueur 1.
- Affichage ✅ : 640×480, recopie GDI.
- Menu Tab ✅ : pause, pages Jeu (JVS sur COM2, carte du volant Taito sur COM1), Retour de force.
- Menu de service ✅ : TEST MODE, SHIFT UP descend, SHIFT DOWN remonte, START choisit ; I/O CHECK (entrées, lampes),
  étalonnage des pédales ; « TO EXIT = TEST+SERVICE ».
- Crédits ✅ : pièce JVS.
- Garde-fous ajoutés pour lui (et D1GP) : adresses IP du PC (AddIPAddress, DeleteIPAddress), suppression de clés du
  registre, HKEY_CLASSES_ROOT et HKEY_USERS en lecture seule comme HKLM.

## Direct3D 8 (d3d8to9)

windowsloader joue Direct3D 8 sur Direct3D 9 avec d3d8to9 de crosire (`src/d3d8to9`, licence BSD 2 clauses,
version 255338f du 23/09/2026), compilé dans la DLL 32 bits (Direct3D 8 n'existe qu'en 32 bits). Seule
modification : son point d'entrée reçoit l'IDirect3D9 que windowsloader crée, et n'affiche plus de boîte ni de page
web quand `d3dx9_43.dll` manque (le journal le dit). L'appareil prend alors le chemin Direct3D 9 de windowsloader
(recopie GDI, modes d'affichage, recadrage, menu Tab, images de l'agent). `WINDOWSLOADER_D3D8=native` garde le Direct3D 8
de Windows (fenêtre gardée, images comptées, rien à l'écran sur ce PC).
