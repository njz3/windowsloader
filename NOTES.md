# Suivi des demandes et du développement

Dépôt `windowsloader` (https://github.com/njz3/windowsloader.git), branche `main`, poussée sur `origin`
(premier push le 21/10, demande 20 ; la règle « pas de commit avant que Sega Rally 3 tourne » est levée depuis).
Identité git locale au dépôt : `njz3 <benjamin.maurin@gmail.com>`.

Légende : ✅ fait et vérifié · 🔄 en cours · ⏳ à faire · ⚠️ fait, non vérifié

## Demandes

| # | Date | Demande | État |
|--:|:--|:--|:--|
| 1 | 21/10 | Demander à la session « Linux loader plus » comment commencer windowsloader | ✅ réponse : lanceur + DLL injectée, code de linuxloader copié (pas de sous-module), étapes ci-dessous |
| 2 | 21/10 | Valider l'architecture, commencer par Sega Rally 3, pas de commit avant que ça tourne, identité git locale | ✅ « oui pour tout » |
| 3 | 21/10 | En-tête des fichiers : auteur Benjamin Maurin - njz3, GPL v3, parties reprises de MAME, linuxloader, sm2-emu ou Flycast sous leur licence | ✅ tous les `.cpp`/`.h` ; `LICENSE` = texte GPL v3 (copié de sm2-emu) |
| 4 | 21/10 | C++ possible, peu de bibliothèques tierces, éviter les templates | ✅ règle suivie : C++ sans template ni STL ; MinHook (embarqué) et SDL3 (vcpkg) seulement |
| 5 | 21/10 | segatools utilisable, ou reprendre du code en le citant | ✅ ajouté à l'en-tête ; licence à vérifier avant toute copie |
| 6 | 21/10 | Licence : « GPL v3 » sans « only », l'auteur se réserve le droit de changer de licence | ✅ en-têtes mis à jour |
| 7 | 21/10 | Étape a : squelette (CMake, presets x86/x64, vcpkg, .gitignore, README, NOTES) | ✅ |
| 8 | 21/10 | Étape b : lanceur `windowsloader.exe` | ✅ testé sur Sega Rally 3 |
| 9 | 21/10 | Étape c : DLL observatrice (fichiers, ports COM, HID, Direct3D 9) | ✅ 3 essais sur Sega Rally 3, journaux dans `logs/` |
| 10 | 21/10 | Étape d : carte d'E/S de Sega Rally 3 (USB HID, VID 0x1043) et commandes (SDL3, vJoy) | 🔄 carte HID émulée : course jouée en mode agent (volant, accélérateur, frein, Start, vue, rapports + et −, vus par captures) ; commandes SDL3 + `controls.ini` (syntaxe de linuxloader) écrites, à essayer à la main |
| 11 | 21/10 | Étape e : retour de force | 🔄 trouvé : pas DirectInput (clavier et souris seulement) mais le port d'E/S 0x1020 par `Out32` de `inpout32.dll`, émulé ; force décodée et sens vérifié en mode agent (volant à droite : +0,37, il pousse à gauche ; à gauche : −0,32) ; rendu SDL haptic écrit, à essayer sur un vrai volant |
| 12 | 21/10 | Étape f : fenêtre et résolution | 🔄 le jeu est gardé en fenêtre (CreateDevice forcé en `Windowed`) ; taille de la fenêtre corrigée (demande 15) ; résolution et rapport d'aspect à faire |
| 13 | 21/10 | Étape g : dossier d'écriture (spécification UWF des émulateurs), sorties réseau | ⏳ |
| 14 | 21/10 | Étape h : mode agent (capture sur Present, image par image) | 🔄 première version : `--agent`, tube nommé, `ready`, `step`, `frame`, `shot`, `set`/`setraw`/`release`/`get`/`inputs`, `quit` ; client `scripts/agent/wlagent.py` ; pas encore image par image |
| 15 | 21/10 | La fenêtre est complètement écrasée, est-ce normal ? | ⚠️ non : le jeu crée sa fenêtre en 0×0 (le plein écran la dimensionne), Windows lui donne ~200×200 et l'image 1920×1080 y est tassée. Corrigé : cadre normal, zone client au rapport de l'image, aussi grande que l'écran le permet, centrée (1674×942 ici, mesuré par le DLL) ; à confirmer à l'œil |
| 16 | 21/10 | Un menu F10 pour mettre le jeu en pause et le configurer, comme linuxloader ? | ✅ fait par la demande 19 (menu F10, passé à Tab par la demande 77) ; la réponse d'alors : faisable, Dear ImGui dessiné dans le hook de Present (backend DX9), pause par gel des threads du jeu et du temps (QPC, timeGetTime, GetTickCount) ; pages Contrôles, Affichage, Retour de force |
| 17 | 21/10 | Essayer le menu de service | ✅ il n'existe pas dans le jeu : sur Europa-R c'est le Shell de la borne qui l'a (absent des 9 éditions de racecab1, `Shell\` n'a que `inpout32.dll`). Le Shell parle au jeu par `WM_COPYDATA` : type 3 = crédits restants, type 0x0D = quitter (le jeu s'arrête pour laisser le Shell afficher son menu). Sans Shell, le jeu le simule (`SimulateShell`). Ses réglages sont dans `ShellData\*.ini` : à reprendre dans F10 (demande 16) |
| 18 | 21/10 | As-tu testé les pédales, Start, vue, frein à main, rapports + et − ? | ✅ en mode agent, par la carte émulée (pas encore avec de vraies commandes) : accélérateur, frein (87 km/h → arrêt puis marche arrière en 60 images), Start, vue (chaque appui change la caméra), rapports + et −, **frein à main = bit 2** (à fond, 118 → 69 km/h en 30 images, l'arrière part ; témoin : 97 → 118). `Handbrake = KEY_Space, GC0_BUTTON_X` ajouté ; un `controls.ini` existant reçoit les lignes par défaut qui lui manquent |
| 19 | 21/10 | Ouvrir le menu paramètres du loader avec F10 | ⚠️ vu à l'écran en mode agent (`sr3_menu.py` : les 4 pages, son coupé, pause de 4,3 s, le jeu repart à la fermeture) ; F10, souris et capture d'une liaison à essayer à la main (lancement normal) ; Dear ImGui 1.92.0 (copié de sm2-emu, MIT) dans le hook de Present, jeu en pause (rendu bloqué, horloges du jeu gelées, son coupé), pages Jeu / Contrôles (liaisons, valeurs en direct, Assigner / Ajouter / Effacer avec capture de la touche, du bouton ou de l'axe) / Retour de force (force, inversion, valeur en direct) / À propos ; commande agent `menu on|off|tab N` |
| 20 | 21/10 | Commit et push | ✅ `main` poussé sur `origin` (4 commits : MinHook, Dear ImGui, le code, NOTES.md ; `2f00351`) |
| 21 | 21/10 | Comme linuxloader : rapport d'aspect, mode d'affichage, résolution interne | ⚠️ `[Display]` de `windowsloader.ini` (mêmes clés et valeurs que linuxloader), page Affichage de F10, F11 / Alt+Entrée. Vérifié : les rectangles des 16 combinaisons (journal), la résolution interne (Sega Rally 3 lancé en 1280×720), la page F10. Pas vérifié : le rendu à l'écran, qu'une capture GDI ne voit pas sur ce PC (noire même sans windowsloader) |
| 22 | 21/10 | En parallèle, copier les deux Daytona Championship USA | ✅ `games/daytonausa` (7,6 Go, 3048 fichiers) et `games/daytonausa-nse` (5,9 Go, 2763), sans échec. `Sega\Daytona\Daytona.exe` est x86 (D3D11 et D3D9, DINPUT8, XINPUT9_1_0, WS2_32) ; le Shell de la borne est là (`Sega\shell\Shell.exe`, x86, HID + SETUPAPI) |
| 23 | 21/10 | Retirer FFBPlugin des copies locales de Daytona | ✅ dans les deux, `Sega\Daytona\` → `Sega\Daytona\_teknoparrot_off\` (rien supprimé) : `dinput8.dll` (FFB Arcade Plugin, chargé sinon par l'import DINPUT8 du jeu), `FFBPlugin*`, `MetroFramework.dll`, `SDL2.dll` (non importé par `Daytona.exe`), `OutputBlaster*`, `teknoparrot.ini` |
| 24 | 21/10 | Ajouter le support des deux versions de Daytona Championship USA | ⚠️ `games/daytona.cpp`, `vdinput.cpp`, `d3d11hook.cpp`, menu F10 en Direct3D 11. Vérifié en mode agent sur les deux éditions, de la démo à la course : défis de sécurité tenus (le jeu quittait à 54 s), SKU (la NSE l'attend), pièce → crédit, Start, choix au volant (à droite → Championnat) et à l'accélérateur, circuit, voiture, course : accélérateur, frein (57 → 18 mph en 2 s), rapports, retour de force (menus : volant à droite → +0,71 vers la gauche ; course : volant à gauche → −0,45 à −0,49 en moyenne), F10 (pages Contrôles et Retour de force), réseau local refusé. Corrigé en route : la carte graphique du portable (le jeu manquait d'espace d'adressage sur l'Intel). Reste à essayer à la main : volant réel et sens du retour de force en course, levier en H, F11 (bandes noires en plein écran : réglées, demande 27) |
| 25 | 21/10 | Commit et push | ✅ 4 commits poussés (`2f00351..b673ee7`) : backend DX11 de Dear ImGui, correctif du `.gitignore` (`games/` cachait `src/hook/games/` : `segarally3.cpp` n'avait jamais été commité, le dépôt ne compilait pas), le code, NOTES.md |
| 26 | 21/10 | Essayer Daytona au volant ; « envoie-toi les commandes » | ✅ sessions vJoy prévenues avant (Compilation, Driveboard, Multi-Board, Linuxloader FFB edition : vJoy libre) et après. SDL ne voit qu'une manette sur ce PC : vJoy (aucun volant physique). Nouvelle option du lanceur `--controllers` (avec `--agent`) : le script conduit, les manettes sont ouvertes et reçoivent le retour de force. Course complète (`day_race.py wheel`) : effet de force constante créé et lancé sur vJoy sans erreur, 72 à 268 changements par phase, volant à gauche → −0,39 en moyenne, rapports, F10. Ce que reçoit le programme derrière vJoy n'est pas vérifiable d'ici |
| 27 | 21/10 | Vérifier le mode stretch en fenêtre réduite et les rapports d'aspect ; aussi le 4:3 avec bandes | ✅ `display_check.py` (16 couples aspect × mode, puis WINDOW_STRETCH réduit à 960×600, 800×800 et 1400×450), capture vue de l'écran en pixels physiques. Daytona (Direct3D 11) : 7 cas sur 16 faux au départ (FULLSCREEN sans bandes, 4:3_LETTERBOX étiré) → corrigé par une fenêtre enfant placée sur l'image. Après : Daytona et Sega Rally 3 justes partout, par exemple plein écran 16:10 ORIGINAL bandes 96/96 px, 4:3 bandes latérales 255/256, 4:3_LETTERBOX image 16:9 entière dans le cadre 4:3 (bandes 240 et 255), fenêtres réduites remplies, LETTERBOX réduit bandes symétriques (67/65, 91/91, 48/47) |
| 28 | 21/10 | Ajouter les jeux dans NOTES.md | ✅ section « Jeux des bornes » : la liste mesurée par la session Image borne (21 jeux Windows non protégés qui démarrent, les écartés et pourquoi), avec l'état dans windowsloader |
| 29 | 21/10 | La fenêtre « Lindbergh Loader Error » (« Unresolved symbol called: XIfEvent ») vient-elle de windowsloader ? Si oui, la renommer « Windows loader » | ✅ non : le texte n'existe pas dans windowsloader, il est dans linuxloader (`src/loader/elfLoader/symbolResolver.cpp`) : un jeu Lindbergh a appelé une fonction X11 que le chargeur ne fournit pas (sans doute un essai de la session Linuxloader FFB edition). Rien renommé |
| 30 | 22/10 | Mettre à jour les notes avec les derniers développements | ✅ demandes 26 à 30, points ouverts, journal (essai au volant, `--controllers`, écran éteint) |
| 31 | 22/10 | Supporter les jeux Windows à retour de force en 32 bits, Sega d'abord : depuis racecab1 si possible, sinon copie locale un jeu à la fois (essais : commandes, vJoy, FFB, affichage, menu de service), copie effacée ensuite ; « même si cela prend plusieurs heures », à la chaîne ; se coordonner avec les sessions Image borne et linuxloader | 🔄 lancer depuis le partage de racecab1 : non (lecture seule, et le jeu écrit dans son dossier) ; copie locale un jeu à la fois, aux moments que donne Image borne. Ford Racing fait (demandes 35 et suivantes) ; Initial D 6 AA joue jusqu'en course (plateforme RingEdge émulée) ; GRID fait ; DiRT Showdown démarre ; K.O. Drive fait (plateforme RingWide) ; Sonic, Battle Gear 4 et Chase H.Q. 2 démarrent sans être jouables (voir BILAN.md) |
| 32 | 22/10 | Effacer les copies locales des jeux déjà supportés ; à chaque copie, enlever le plugin FFB et les DLL TeknoParrot | ✅ Sega Rally 3 et les deux Daytona à la Corbeille ; Ford Racing : plugin, `teknoparrot.ini`, dgVoodoo, jconfig mis dans `_teknoparrot_off` |
| 33 | 22/10 | Supprimer définitivement les jeux locaux déjà validés | ⚠️ pas fait : la suppression définitive m'est interdite ; tout passe par la Corbeille, à vider par l'utilisateur |
| 34 | 22/10 | À la fin, un bilan par fonction (commandes, vJoy, FFB, affichage, menu de service…) dans un markdown rempli au fur et à mesure | 🔄 `BILAN.md` : vue d'ensemble et une section par jeu |
| 35 | 22/10 | Ford Racing est en 4:3 : bandes latérales quand on force 16:9 ; détecter l'aspect natif du jeu pour que ASPECT_RATIO corrige selon le choix | ✅ l'aspect natif est celui du back buffer ; 4:3 et 16:9 sont la forme du cadre, l'image y garde sa forme avec des bandes ; seuls les modes STRETCH étirent ; 4:3_LETTERBOX = 4:3. Vérifié à l'écran sur Ford Racing (16:9 : bandes de 106 px en fenêtre, 240 px en plein écran) |
| 36 | 22/10 | Afficher les FPS dans le titre de la fenêtre | ✅ « FULL_BLOWN [fordracing] - 61 fps », une fois par seconde |
| 37 | 22/10 | Si linuxloader ne gère pas l'aspect comme windowsloader, le lui demander | ✅ demandé ; fait par la session Linuxloader FFB edition (compilé, pas encore commité chez elle) |
| 38 | 22/10 | Le nom du jeu entre crochets dans le titre, pour l'autodétection de BFF ; passer la demande à linuxloader | ✅ « <titre> [fordracing] - 61 fps » (le nom du module : `segarally3`, `daytona`, `fordracing`) ; linuxloader l'a fait aussi (« [id4-exp-rev-d] - FPS: 60.01 ») |
| 39 | 22/10 | (après une connexion Bureau à distance) Reprendre et ne plus s'arrêter avant d'avoir lancé tous les jeux et fait tous les essais | 🔄 |
| 40 | 22/10 | Corriger d'abord les jeux qui ne s'affichent pas | ✅ sur ce PC un Present Direct3D (9 ou 11) en mode blt ne montre rien : l'image est recopiée par GDI (`[Display] PRESENT = GDI`, défaut ; `DIRECT` pour l'ancien comportement). Vus à l'écran : DiRT Showdown 60 i/s (fenêtre blanche avant), Sonic 61 (47 avant), Battle Gear 4 55 et Chase H.Q. 2 60 (noirs avant, pour d'autres raisons : son, JVS, chemins ; voir BILAN.md) |
| 41 | 22/10 | Commit et pousse | ✅ 3 commits poussés (`8bb9660..0a3d507`) : le code (huit jeux, plateformes RingEdge/RingWide et Taito, recopie GDI, `scripts/agent/race_check.py`), BILAN.md, NOTES.md |
| 42 | 22/10 | « As tu déjà fait GTI Club ? », puis « Image borne me dit avoir fini » | ✅ GTI Club 3 copié (3,15 Go), module `konami.cpp` : démarre, joue jusqu'en course, vJoy, F10, menu de service (voir BILAN.md) |
| 43 | 22/10 | (transmise par la session Linuxloader FFB edition) Les réglages de linuxloader, mêmes noms et même sens : `ASPECT_RATIO` étiré et recadré, `CROP_POSITION`, la courbure (`CROP_BARREL`, `BARREL_CENTER`, `BARREL_RADIUS`), le fichier `<nom court>.ini` par jeu avec « Sauvegarder pour ce jeu », `-c`, `--set SECTION.CLE=VALEUR` | ✅ tout, vérifié sur GTI Club 3 (D3D9, 16:9), Battle Gear 4 (D3D9, 4:3) et DiRT Showdown (D3D11, 16:9). Écarts : la courbure seulement avec `PRESENT = GDI` (le défaut) ; en `DIRECT`, D3D9 recadre sans courber et D3D11 montre l'image entière entre bandes noires |
| 44 | 22/10 | Commit et pousse | ✅ 3 commits poussés : le code (`ce52978`, GTI Club 3 et les réglages de linuxloader), BILAN.md et README.md (`b4d2724`), NOTES.md |
| 45 | 22/10 | Poursuit (la chaîne des jeux) | 🔄 Wasteland Racers 2071 : copie commencée, carte d'E/S étudiée (voir le journal) |
| 46 | 22/10 | Mets-toi en pause, je redémarre | ✅ copie arrêtée à 2529 fichiers sur 4860, Image borne prévenue |
| 47 | 22/10 | Commit et pousse | ✅ rien d'autre en attente : NOTES.md seul |
| 48 | 22/10 | Reprends | ✅ copie de Wasteland Racers finie (après un croisement avec la copie de linuxloader, tranché par Image borne) ; jeu lancé jusqu'en course (voir le journal) |
| 49 | 22/10 | (question) Télécharger d3d8to9 ? « Oui, télécharge-le » | ✅ crosire/d3d8to9, version 255338f (23/09/2026), relu, dans `src/d3d8to9` |
| 50 | 22/10 | SWDC (Sega World Drivers Championship) est-il compatible windowsloader ? | ✅ réponse : pas encore ; 64 bits, Unreal Engine 4 (Direct3D 11 probable, déjà géré), mais la plateforme Sega ALLS (amdaemon, keychip, carte d'E/S USB, carte du volant) est à faire, un module de la taille de RingEdge |
| 51 | 22/10 | Poursuis ton travail selon la liste de jeux | 🔄 Wasteland Racers fini (commandes, vJoy, F10, service) ; Battle Gear 4 Pro = Tuned + options ; Mario Kart DX 1.18 copié, commencé (carte d'E/S « WinArc » à retrouver) |
| 52 | 23/10 | Continue avec les jeux suivants | 🔄 Battle Gear 4 Pro fait (`[Game.bg4] CABINET = PRO` : seconde carte JVS, levier en H, embrayage, grille motorisée, `HOLD_GEAR`) ; Tuned et Chase H.Q. 2 revérifiés |
| 53 | 23/10 | Continue avec tous les autres jeux de la liste | 🔄 copiés : Battle Gear 4 (2006), D1GP, Valve Limit R ; Battle Gear 4 (2006) reconnu à part (correctifs à ses adresses) ; Mario Kart DX : la carte JVS est sur COM3, COM1 est la carte du moteur (protocole retrouvé, émulée) ; garde-fous : adresses IP, suppressions dans le registre, racines HKCR/HKU ; retour de force du volant DirectInput de windowsloader ; essais arrêtés à 3 h : plus aucun écran sur le PC |
| 54 | 23/10 | Commit et pousse | ✅ 3 commits poussés (`8f2c1e1..56fa766`) : le code (Wasteland Racers, Battle Gear 4 Pro et première édition, cartes de Mario Kart DX, Direct3D 8, garde-fous), BILAN.md et README.md, NOTES.md |
| 55 | 23/10 | Pour l'émulation de la présence de la clé, demande à linuxloader comment elle a fait | ✅ demandé ; réponse : les fonctions de la bibliothèque du jeu remplacées, des données fabriquées d'après le code qui les vérifie. Crazy Ride : « présente, vérifiée » suffit (le jeu ne garde qu'un booléen) ; `unis.cpp` écrit, pas encore lancé |
| 56 | 23/10 | Une page de réglages du jeu en cours dans la fenêtre des paramètres, remplie selon le jeu lancé (Battle Gear 4 : mode Pro ; options matérielles d'autres jeux) | ✅ onglet « Options du jeu » du menu F10, table d'options déclarée par le module du jeu ; Battle Gear 4 Tuned d'abord ; la page pas encore vue à l'écran |
| 57 | 23/10 | Demande à linuxloader comment elle va faire, pour faire la même chose | ✅ forme convenue : `[Game.<nom court>]`, `--set Game.CLE=VALEUR`, onglet « Options du jeu », case / liste / curseur, « (au prochain lancement) » |
| 58 | 23/10 | Une option de passthrough avec choix d'un port COM pour la carte moteur de Mario Kart et des autres jeux à port série, idem pour le JVS | ✅ [Emulation] EMULATE_JVS / EMULATE_DRIVEBOARD, [Paths] JVS_PATH / SERIAL_n_PATH (noms de linuxloader), partie « Cartes » des options du jeu ; vérifié sur un port absent, puis sur la paire virtuelle COM10 / COM11 (carte du volant et JVS de Battle Gear 4) |
| 59 | 23/10 | Décrire les protocoles de pilotage des cartes moteur dans DRIVEBOARD.md de BackForceFeeder, en particulier Namco et Taito sur port série | ✅ section « Other boards, as the Windows arcade games drive them » ajoutée à la fin : volant de Battle Gear 4, grille de BG4 Pro, moteur de Chase H.Q. 2 sur les sorties JVS, carte moteur de Mario Kart DX, Kickback de Namco, servo Sega des jeux RingEdge / RingWide, Crazy Ride, Wasteland ; non commité |
| 60 | 23/10 | Un JVS.md à côté : le protocole JVS complet et ses dérivés, et pour chaque plateforme ou jeu le sens des entrées-sorties | ✅ JVS.md (anglais) : couche physique, ligne de détection par plateforme, trame, statuts, toutes les commandes, celles de Taito et de Namco, les cartes des E/S de Lindbergh (d'après linuxloader), RingEdge / RingWide, Battle Gear 4 et sa version Pro, Chase H.Q. 2, Mario Kart DX ; non commité |
| 61 | 23/10 | La carte Taito de Battle Gear 3 n'est pas la même que les autres : garder ce qui est écrit, ajouter les autres cartes dans une section à part | ✅ rien de l'existant retiré ; les autres cartes dans la section à part, qui dit ne pas les supposer pareilles ; un paragraphe de comparaison corrigé (Battle Gear 3 envoie aussi 0D 80) |
| 62 | 23/10 | Il y a deux ports COM10 et COM11 en tunnel virtuel si tu veux essayer les modes passthrough | ✅ essayé avec Battle Gear 4 : la carte du volant puis le JVS sur COM10, un script Python jouant la carte sur COM11 ; créneau convenu avec Linuxloader |
| 63 | 23/10 | Commit et pousse juste ton dépôt | ✅ 3 commits poussés (`56fa766..2df21e3`) : le code (options du jeu, passthrough, Crazy Ride, commandes JVS de Namco), BILAN.md et README.md, NOTES.md ; BILAN.md mis à jour avant ; le dépôt de BackForceFeeder laissé tel quel |
| 64 | 23/10 | Où en es-tu ? | ✅ le point : demandes faites, reste de la liste des jeux (Mario Kart DX, Battle Gear 4 de 2006, D1GP, Valve Limit R, Crazy Ride, Sonic, SWDC) |
| 65 | 23/10 | Termine Mario Kart DX puis enchaîne avec les jeux suivants | 🔄 Mario Kart DX fait : démarrage complet (clé, lecteur de cartes, réseau des bornes, hors ligne), image (Direct3D 10), commandes vues au menu de test, retour de force reçu, F10 ; vJoy et le sens de la force au volant restent. Battle Gear 4 de 2006 fait : sa vidéo de démo par DirectShow (chemin redirigé), course, test d'E/S, vJoy, F10. D1GP fait : JVS (bits trouvés au test d'E/S), volant DirectInput en Unicode, retour de force Immersion par DirectInput 7, vJoy, F10 ; lecteur de cartes non émulé. Valve Limit R fait : JVS (table du jeu), carte du volant Taito et ses effets façon Battle Gear 3, étalonnage des pédales, captures 16 bits, vJoy, F10. Crazy Ride fait : blocage du rendu (fenêtres d'un autre fil), entrées, pédales, volant, vJoy, F10. Sonic fait : l'enregistrement système de l'EEPROM (les crédits démarrent), la sauvegarde sur `Y:\` (le blocage au choix du pilote), le jeu gratuit en option, course, vJoy, F10. Reste : SWDC (et Mario Kart DX v1.10), à copier de racecab1 |
| 66 | 23/10 | Dans la fenêtre des paramètres, pour chaque jeu, la carte d'E/S des contrôles et la carte moteur (« Controls on JVS board, version xx », « Motor board: Sega-like servoboard ») | ✅ deux lignes sous le nom du jeu dans la page « Jeu » du menu F10, déclarées par chaque module (`wl_game_set_boards`), notées aussi au démarrage ; libellés repris par Linuxloader |
| 67 | 23/10 | Un mode « plein écran fenêtré / borderless » et « plein écran fenêtré / borderless stretch » | ✅ `DISPLAY_MODE = BORDERLESS` / `BORDERLESS_STRETCH` (l'ancien plein écran, fenêtre sans bordure à la résolution du bureau) ; `FULLSCREEN` change maintenant la résolution de l'écran ; mêmes noms chez Linuxloader |
| 68 | 23/10 | Pendant que tu travailles et sans t'arrêter, le tableau des jeux déjà faits et supportés complètement, et de ceux restant à faire | ✅ donné d'après BILAN.md : 16 jeux jouables de bout en bout (retour de force à confirmer au volant pour la plupart) ; en cours Sonic, Valve Limit R (fini ensuite), Crazy Ride ; pas commencés SWDC et Mario Kart DX v1.10 |
| 69 | 23/10 | Commit et pousse | ✅ 3 commits poussés (`2df21e3..eafbe67`) : le code (Mario Kart DX, Battle Gear 4 de 2006, D1GP, Valve Limit R, Crazy Ride, modes plein écran fenêtré, cartes dans F10), BILAN.md et README.md, NOTES.md ; le dépôt de BackForceFeeder non touché |
| 70 | 23/10 | Où en es-tu dans la liste de jeux ? | ✅ le point d'après BILAN.md : 18 jeux jouables (retour de force à confirmer au volant pour la plupart), Sonic commencé, SWDC et Mario Kart DX v1.10 à copier |
| 71 | 23/10 | Continue | 🔄 Sonic fini : « PRESS START » et les pièces (enregistrement AM_SYSDATA_STATIC de l'EEPROM écrit), le choix du pilote (sauvegarde `Y:\ssr_save.bin` dans le dossier du jeu), course, option « Jeu gratuit », vJoy, F10 ; ensuite SWDC |
| 72 | 23/10 | Où en es-tu ? | ✅ le point : Sonic fini (pas encore commité), SWDC en copie depuis racecab1 (11,3 Go sur 12,5), puis Mario Kart DX v1.10 |
| 73 | 23/10 | Commit et pousse pour Sonic | ✅ 3 commits poussés (`eafbe67..f80c03c`) : le code (`ringedge.cpp` : enregistrement système de l'EEPROM, disque `Y:`, option `FREE_PLAY`), BILAN.md et README.md, NOTES.md |
| 74 | 23/10 | Je ferai le commit à la fin quand tu auras fait marcher tous les jeux | 🔄 noté : pas de commit avant ; Mario Kart DX 1.10 fait (clé, lecteur de cartes NFC sur COM4, authentification, course, retour de force, vJoy, F10) ; SWDC : la carte d'E/S passe, reste l'erreur 3003 (carte des lampes) |
| 75 | 23/10 | J'ai mis une exclusion sur le dossier racine | ✅ après la mise en quarantaine de `bin\Release\windowsloader.exe` par Defender (« Behavior:Win32/DefenseEvasion.A!ml », 23:23, au lancement d'un essai de SWDC : injection, programme compagnon sans fenêtre) ; exclusion posée par l'utilisateur, le lanceur reconstruit |
| 76 | 24/10 | Continue avec SWDC | 🔄 3003 passée (le tableau de bord sur COM20) ; puis 8404 « serveur d'installation LAN introuvable » : la borne est la principale (`config_LanServer.json`, `Y:\SDDS\LanServer.dat`), et amdaemon veut le fichier `ICF1`, absent de la copie |
| 77 | 24/10 | Passe la touche d'ouverture du menu paramètres à 'Tab' au lieu de F10, afin de rester homogène avec linuxloader et Supermodel | ✅ Tab ouvre et ferme le menu (pas Alt+Tab) ; Tab ne se capture plus comme commande, F10 si ; README, BILAN, commentaires. Pas essayé en jeu : l'écran du PC est déconnecté |
| 78 | 24/10 | Fais un build et une archive release que tu mettras à côté des autres releases des autres émulateurs, puis reprends ton travail sur SWDC | ✅ `releases\windowsloader-0.1.0-20261024-njz3-windows.zip` (32 et 64 bits, RELEASE.txt : jeux, ligne de commande, SHA-256), les réponses à Image borne ; 🔄 SWDC |
| 79 | 24/10 | Commit et pousse. Refais une release | ✅ 6 commits poussés (`f5c5104` et suivants : Mario Kart DX 1.10, touche Tab, SWDC en cours, docs, NOTES.md) ; release `windowsloader-0.1.0-20261024-2-njz3-windows.zip` tirée du commit poussé |
| 80 | 24/10 | Quels jeux te reste-t-il à faire ? | ✅ de la liste des 21 jeux des bornes, SWDC seul (bloqué au test réseau, essais en attente de l'écran) ; à confirmer : retour de force au vrai volant, vJoy de Mario Kart DX 1.18, Tab en jeu ; hors liste : les écartés (Wangan Midnight 5, 5DX+ et 6 « à juger », Initial D 7 et 8, Raw Thrills) |
| 81 | 24/10 | Essaie d'ajouter tous les autres jeux sauf les Raw Thrills | 🔄 faits : Sega Racing Classic, Storm Racer G, Wacky Races, Let's Go Safari (une course chacun) ; Initial D 7 et 8 en copie depuis racecab1 (au signal d'Image borne), puis les Wangan Midnight une à une |
| 82 | 24/10 | En particulier Sega Racing Classic et Storm Racer G | ✅ Sega Racing Classic (levier en H, carte moteur lue dans les données du jeu ; course, menu de service, vJoy, Tab) ; Storm Racer G joue une course (`stormracer.cpp` : `usb_io.dll` remplacée, MSAA coupé, vidéos noires sans le codec WMV) ; restent ses boutons à nommer |
| 83 | 24/10 | Reprends | ✅ Storm Racer G jusqu'en course : pièce, Start, menus, l'étape photo, course ; le moteur en ressort ; Wacky Races commencé |
| 84 | 24/10 | Fais les derniers jeux | ✅ Wacky Races (course, moteur par les sorties JVS, `LINKID` à 0 par défaut), Let's Go Safari (carte JVS 837-14895, clé en toolmode, course, vibration), Initial D 7 et 8 (JVS par `mxjvs`, lecteur de cartes émulé derrière `picodaemon.exe`, course) ; 🔄 Wangan Midnight 5, 5DX+ et 6 : copies depuis racecab1 après celles de Linuxloader |
| 85 | 24/10 | Demande à la session « Image borne » de retirer les fichiers FFBPlugin des jeux parfaitement supportés par windowsloader | ✅ demandé : Sega Rally 3 (huit éditions), Daytona Championship USA et NSE, Ford Racing Full Blown, GRID, Wasteland Racers 2071 ; les fichiers rangés dans un sous-dossier, la DLL seulement si c'est bien le plugin |
| 86 | 24/10 | En attendant, commit et pousse. Puis fais une release et préviens la session « Image borne » | ✅ 7 commits poussés (`bd122b8` et suivants : pédales `_SIDE`, Sega Racing Classic et Let's Go Safari, Storm Racer G, Wacky Races, BILAN.md, NOTES.md) ; release `windowsloader-0.1.0-20261024-4-njz3-windows.zip` tirée du commit poussé, Image borne prévenue |
| 87 | 24/10 | Commit et pousse, mais ne fais pas encore de release | ✅ 3 commits poussés : Initial D 7 et 8 (`mxjvs`, `picodaemon.exe` et `cardrw.cpp`, défis de la clé hors table), BILAN.md, NOTES.md ; pas de release |
| 88 | 24/10 | Où en est la copie ? | ✅ Wangan Midnight 5 : 36 155 fichiers sur 46 544 et 5,3 Go sur 18,5 à 21 h 07, environ 2 Mo/s, fin vers 23 h |
| 90 | 25/10 | Mets les jeux déjà faits à la corbeille | 🔄 la corbeille de C: ne prend que 48,5 Go pour 72 Go de copies : en deux fois. Faits : Showdown, Storm Racer G, Mario Kart DX 1.18 et 1.10, Initial D 7 et 8, Let's Go Safari, Sega Racing Classic (45,5 Go). Après que le propriétaire aura vidé la corbeille : les trois Battle Gear 4, Chase H.Q. 2, Crazy Ride, D1GP, GTI Club 3, Sonic, Valve Limit R, Wacky Races, Wasteland Racers (22,5 Go). Gardés : SWDC, Wangan Midnight 5 (en cours) |
| 92 | 25/10 | Est-ce que le mode agent ne permet pas de faire des captures à la demande ? | ✅ oui : `shot`, `screen`, et `ffb` pour les forces envoyées ; il faut lancer le jeu avec `--agent` (et `--controllers`) depuis un programme qui garde l'entrée et la sortie du lanceur ; `--shots` ne servait qu'à la ligne d'AttractPlus telle quelle |
| 93 | 25/10 | Retire le --shots et dis à la session « Image borne » d'utiliser l'agent si besoin | ✅ `--shots` retiré (retour du commit 5318018, pas encore commité) ; le mode agent expliqué à Image borne (lancement, protocole, `shot`, `screen`, `ffb`, `scripts/agent/wlagent.py`) |
| 94 | 25/10 | Commit et pousse | ✅ 2 commits poussés : le retrait de `--shots`, NOTES.md (demandes 92 à 94, la cause des plantages des éditions de Sega Rally 3) ; pas de release |
| 95 | 25/10 | La corbeille est vidée. Étapes suivantes ? | ✅ la seconde moitié des copies à la corbeille (les trois Battle Gear 4, Chase H.Q. 2, Crazy Ride, D1GP, GTI Club 3, Sonic, Valve Limit R, Wacky Races, Wasteland Racers : 22,5 Go) ; restent dans `games\` SWDC et Wangan Midnight 5 ; proposé : Wangan Midnight 5, puis 5DX+ et 6 une fois copiés |
| 96 | 25/10 | Reprends Wangan Midnight 5 en attendant les copies | 🔄 le lien avec le terminal de salle tenu pour établi (saut à wmn5r.exe+0x58e70f) ; E0505 « Disconnected from local network » levée : un terminal joué par windowsloader (paquets d'OpenParrot vers 127.0.0.1:50765, montrés depuis 192.168.92.20) ; le jeu arrive en démonstration « FREE PLAY ». Copies de 5DX+ (15,18 Go) et 6 (16,39 Go) faites pendant ce temps. Reste : lecteur de cartes, caméra, course, retour de force. Pas encore commité |
| 97 | 25/10 | Si on lance windowsloader seul, sans argument, ne peut-on pas avoir une liste des jeux déjà configurés ? | ✅ oui : repris par la demande 99 (sélecteur de jeux) |
| 98 | 25/10 | Dans l'archive release, enlève le fichier BILAN.md | ✅ `windowsloader-0.1.0-20261024-4-njz3-windows.zip` refaite sans BILAN.md (l'ancienne à la corbeille) ; RELEASE.txt renvoie à BILAN.md sur GitHub ; les prochaines releases suivront |
| 99 | 25/10 | Pour le lancement de windowsloader seul, sans argument, fais comme linuxloader qui propose un sélecteur de jeu | ✅ `windowsloader.exe` sans argument ouvre un sélecteur (Dear ImGui, Direct3D 11) : les jeux du dossier des jeux reconnus par la table `src/launcher/gamelist.cpp`, leur fiche, Entrée pour jouer ; essayé sur un dossier d'essai et sur `games\` (Wangan Midnight 5 lancé, le sélecteur revenu à l'arrêt) ; pas encore commité |
| 100 | 25/10 | Dans le titre de la fenêtre de l'émulateur, ajoute pour ma branche la version du build (comme le package) et « FFB Edition » | ✅ titre de la fenêtre du jeu : son titre suivi de « [wmmt5] - 60 fps - windowsloader 0.1.0-20261025-njz3 FFB Edition » ; aussi le titre du sélecteur, la page « À propos » du menu, le journal et la console. La version est écrite à chaque build par `cmake/build_version.cmake` (jour du build ; numéro d'archive par `-DWL_BUILD_NUMBER` pour une release ; branche et édition par `WL_FORK` et `WL_EDITION`) ; vérifié sur Wangan Midnight 5 et le sélecteur ; pas encore commité |
| 101 | 25/10 | Combien de jeux reste-t-il à faire ? | ✅ 4 dans le champ convenu (tous sauf les Raw Thrills) : Wangan Midnight 5 (en cours, en démonstration), 5DX+ et 6 (copiés), SWDC (bloqué au test réseau ALLS, reprise à décider) ; jamais classés sur racecab1 : Maximum Heat 3D, Nirin, Initial D Zero 1.31 et 2.30, Road Fighters 3D, Frenzy Express, CrazySpeed, Sega Rally 3 Revo |
| 102 | 25/10 | Commit et pousse | ✅ 4 commits poussés (`67deef4..a0c2972`) : le sélecteur de jeux avec la version dans les titres (mêmes lignes de `launcher.cpp`, `CMakeLists.txt`, `README.md`), le terminal de Wangan Midnight 5, BILAN.md, NOTES.md |
| 103 | 25/10 | Fais une release et notifie la session « Image borne » pour qu'elle mette à jour les deux bornes | ✅ `windowsloader-0.1.0-20261025-njz3-windows.zip` (1re du jour, commit `a0c2972`, 3 711 804 octets, SHA-256 2a09974c…c3c102) dans `emulateurs\releases` : les quatre binaires, LICENSE, README.md, RELEASE.txt (sans BILAN.md) ; vérifiée dépaquetée (« 0.1.0-20261025-njz3 FFB Edition ») ; Image borne prévenue (racecab1 et racecab2, garder les .ini) ; installée sur les deux bornes avec l'accord du propriétaire (« 0.1.0-20261025-njz3 FFB Edition », .ini gardés, anciens binaires dans `C:\Racecab\Emulators\_sauvegardes\windowsloader-avant-20261025`) ; Daytona avec CabinetID=2 vérifié sur racecab2 : écran d'attente FREE PLAY, plus de NETWORK DISCOVERY. Signalé par Image borne : sur les bornes, DirectInput voit les axes vJoy en tout ou rien (joy.cpl aussi) |
| 104 | 25/10 | Étapes suivantes ? | ✅ proposées : Wangan Midnight 5 jusqu'en course, puis 5DX+ et 6 ; décisions au propriétaire (SWDC, dossiers jamais classés de racecab1, réglage du retour de force de Mario Kart DX 1.18) ; les trois fonctions manquantes (WRITE_DIR, sorties réseau, menu Tab désactivable) |
| 105 | 25/10 | Fais d'abord les trois fonctions manquantes | ✅ `[Paths] WRITE_DIR` / `--write-dir` (superposition copie-sur-écriture dans `files.cpp`, réglages et journal à sa racine, sélecteur compris), `[Outputs] OUTPUTS_NETWORK` (`outputs.cpp`, protocole de MAME comme linuxloader : lampes JVS, `RawDrive`, `mame_start`/`mame_stop`, annonce UDP), `[Launcher] SETTINGS_MENU = false` ; `[Launcher] GAMES_DIR` pour le sélecteur, comme linuxloader (`[Picker]` encore lu) ; vérifiés sur Wangan Midnight 5 et un programme d'essai 32/64 bits ; pas encore commités |
| 106 | 25/10 | Que veux-tu dire par « le dossier de Windows, ProgramData, AppData et le dossier temporaire ne passent pas par le dossier d'écriture » ? | ✅ expliqué : ce que le jeu et ses bibliothèques y écrivaient restait à sa place (caches, fichiers temporaires, sauvegardes de certains jeux), donc perdu sous UWF |
| 107 | 25/10 | Je vais protéger avec UWF tout le disque sauf le dossier d'écriture : AppData et le dossier temporaire doivent être redirigeables ; tous sous la même clé WRITE_DIR, dans des sous-dossiers (appdata, temp, programdata, windows) ; pas plusieurs paramètres : WRITE_DIR vide, les dossiers du système normaux | ✅ un seul réglage, `WRITE_DIR` : `WRITE_DIR\<jeu>\appdata` (Roaming, Local, LocalLow), `temp`, `programdata`, `windows`, le reste sous la lettre de son lecteur ; `LoadLibrary` prend une DLL écrite par le jeu dans le dossier d'écriture ; les images de l'agent restent où il les demande ; vérifié (programme d'essai 32/64 bits, Wangan Midnight 5) ; pas encore commité |
| 108 | 25/10 | Commit et pousse. Fais une release et notifie « Image borne » | ✅ 3 commits poussés (`a0c2972..fc31bb3`) : dossier d'écriture, sorties réseau et menu Tab désactivable (un seul commit : fichiers partagés), BILAN.md, NOTES.md ; release `windowsloader-0.1.0-20261025-2-njz3-windows.zip` (2e du jour, `-DWL_BUILD_NUMBER=2` puis remis à vide, commit `fc31bb3`, 3 737 287 octets, SHA-256 cb9a6e7f…309d01), vérifiée dépaquetée ; Image borne prévenue (les deux bornes, garder les .ini ; fonctions éteintes par défaut) |
| 109 | 25/10 | Étapes suivantes ? | ✅ proposées : les compagnons (picodaemon, amdaemon) dans le sous-dossier de leur jeu avec WRITE_DIR, essai des trois fonctions sur borne, Wangan Midnight 5 jusqu'en course, 5DX+ et 6, décisions au propriétaire |
| 110 | 25/10 | (Image borne) Let's Go Safari s'arrête sur « Error 0949 / Keychip Not Found » avec la release 20261025 | ✅ régression trouvée : en toolmode (pas de table de clé : Let's Go Safari, K.O. Drive) tous les défis sont hors table ; ils recevaient des zéros jusqu'à la release 4, des octets fabriqués depuis 841c03f (fait pour Initial D 8). Corrigé : des zéros en toolmode, les octets fabriqués seulement avec une table ; pas essayé ici (copies vidées de la corbeille), pas encore commité. Image borne prévenue : ne pas poser la release 20261025-2 (même défaut) |
| 111 | 25/10 | Commit et pousse, fais la release -3 et notifie Image borne | ✅ 3 commits poussés (`fc31bb3..5ee5005`) : la correction du toolmode, BILAN.md, NOTES.md ; release `windowsloader-0.1.0-20261025-3-njz3-windows.zip` (commit `5ee5005`, 3 737 355 octets, SHA-256 85971276…ad57bf), vérifiée dépaquetée, le numéro de build remis à vide ensuite ; Image borne prévenue (K.O. Drive confirmé touché par elle sur racecab2 ; la -3 à poser quand le propriétaire le demandera, Let's Go Safari et K.O. Drive à vérifier) |
| 112 | 25/10 | Mets les archives défectueuses à la corbeille | ✅ `windowsloader-0.1.0-20261025-njz3-windows.zip` et `…-20261025-2-…` à la corbeille ; reste dans `emulateurs\releases` la seule `windowsloader-0.1.0-20261025-3-njz3-windows.zip` |
| 113 | 25/10 | Étapes suivantes ? | ✅ proposées : les compagnons dans le sous-dossier de leur jeu, essai sur borne de la -3, une vérification de chaque jeu sur racecab2 avant de poser une release, Wangan Midnight 5 jusqu'en course, 5DX+ et 6, décisions au propriétaire |
| 114 | 25/10 | Vas-y, commence par le point 1 (les compagnons et le dossier d'écriture) | ✅ le lanceur dit au compagnon quel jeu il accompagne (`WINDOWSLOADER_COMPANION_OF`) : picodaemon écrit dans `WRITE_DIR\initiald7` ou `initiald8` (son module d'après SBYD/SBZZ), amdaemon déjà dans `swdc` ; aucun compagnon n'ouvre le serveur des sorties réseau (amdaemon, démarré avant le jeu, prenait le port 8000). Vérifié avec SWDC ; picodaemon pas essayable ici ; pas encore commité |
| 115 | 25/10 | (Le propriétaire) vjoyd a été corrigé par Image borne : vJoy peut donner des valeurs analogiques | ✅ noté : les essais de volant et de pédales par vJoy redeviennent possibles sur les bornes |
| 116 | 25/10 | Commit et pousse. Regarde ensuite Wangan Midnight 5 | ✅ poussé (`884b357..2ce7e10`). Wangan Midnight 5 jouable : sa table de commandes (levier en H, vue, interruption, option « Garder le rapport »), une course en mode Story, le volant étalonné dans le menu de test ; le retour de force, réglage du jeu à mettre à ON, par la carte du volant « STR3 » sur COM1 (E00, C06/C01 suivant l'alimentation du moteur, « H » et la position après chaque commande) ; `vdev.cpp` accepte un port de complétion (boost::asio). `RawDrive` vu en course. Restent le lecteur de cartes et la caméra |
| 117 | 25/10 | Commit et pousse, demande à Image borne d'activer l'option pour activer le FFB | ✅ poussé : `690ecc9` (le code), `0f7de59` (BILAN.md), NOTES.md. Image borne prévenue : le réglage du jeu « ステアリング反力の使用 » à ON dans le menu de test (ゲーム設定), le chemin donné ; mais seulement avec un windowsloader contenant `690ecc9`, absent de la release -3 (avec elle, E2212 « STR PCB Link error » vers 60 s) ; le réglage écrit dans les fichiers F*/G* du jeu, donc dans WRITE_DIR sous UWF. Pas de release faite |
| 118 | 25/10 | (Image borne) La -3 sur racecab2 ; Wangan attendra une release avec `690ecc9` ; son dossier pour AttractPlus ? | ✅ noté : Let's Go Safari 493 paquets de retour de force, K.O. Drive 139, plus d'Error 0949 (la correction de la -3 confirmée sur borne) ; Chase H.Q. 2 étalonné (volant 0x0000 à 0xFF00, pédales), `rtb.dat` et `rtc.dat` écrits. Réponse donnée avec la -4 : `C:\racecab\Games\Wangan Midnight Maximum Tune 5 - Namco ES3X\wmn5r.exe` (le dossier copié depuis racecab1), l'ajout à AttractPlus au propriétaire |
| 119 | 25/10 | Fais la release -4 et notifie Image borne | ✅ `windowsloader-0.1.0-20261025-4-njz3-windows.zip` (source `a506271`, 3 743 075 octets, SHA-256 e7b55361…72c5fa) : seul Wangan Midnight 5 change ; vérifiée dépaquetée (démonstration, version -4 au journal, E00/C06/C01 de la carte du volant, pas d'E2212), le numéro de build remis à vide. Image borne prévenue : l'archive, les fichiers de TeknoParrot à écarter du dossier de Wangan, le réglage du retour de force et l'étalonnage dans le menu de test, WRITE_DIR sous UWF ; la pose quand le propriétaire la demandera |
| 120 | 25/10 | Oui, qu'elle installe la -4 et active le FFB | 🔄 transmis à Image borne avec ces mots : la -4 sur les deux bornes, les fichiers de TeknoParrot écartés du dossier de Wangan, « ステアリング反力の使用 » à ON dans le menu de test ; l'ajout à AttractPlus pas compris dans la demande. Attendus en retour : le sens du volant, E2212 ou non, le réglage gardé après redémarrage |
| 121 | 25/10 | Commit et pousse (`7873f6a` ; le push refusé d'abord : la pull request n° 1 de Legired fusionnée sur GitHub, GTI Club à la résolution `[Display]`, mon commit replacé après elle) | ✅ poussé ; la fusion compile en 32 et 64 bits sans avertissement |
| 122 | 25/10 | Commit et pousse. Fais une release et notifie Image borne pour qu'elle mette à jour les deux bornes | ✅ rien à commiter (déjà poussé) ; `windowsloader-0.1.0-20261025-5-njz3-windows.zip` (source `7873f6a`, 3 748 969 octets, SHA-256 e3bc8a10…740a25) : la -4 plus GTI Club à la résolution `[Display]` (pas vérifié : plus de jeu 32 bits sur ce PC) ; vérifiée dépaquetée avec Wangan (version -5, carte du volant), le numéro de build remis à vide. Image borne invitée à poser la -5 sur les deux bornes (à la place de la -4), le retour de force de Wangan à ON comme demandé ; BILAN.md : la ligne de GTI Club |
| 123 | 25/10 | Étapes suivantes ? | ✅ proposées : Wangan 5 DX+ puis 6 (même famille) ; en attente d'Image borne (sens du volant de Wangan, GTI Club en 32 bits, Mario Kart DX) ; restes de Wangan 5 (cartes, caméra, pièces, vJoy) ; jamais vérifiés ici (Tab avec `SETTINGS_MENU = false`, CopyFile/SetFileAttributes/RemoveDirectory, picodaemon) ; décisions au propriétaire (SWDC, AttractPlus, dossiers de racecab1, FFB de Mario Kart DX 1.18, ID7/8) |
| 124 | 25/10 | (Image borne) Wangan 5 sur racecab2 avec la -4, puis la -5 posée sur les deux bornes | ✅ noté : fichiers de TeknoParrot écartés sur les deux bornes (manifeste `C:\Racecab\Utils\ffbplugin_off_manifeste.csv`), le réglage du retour de force à ON sur les deux ; en course le ressort ramène vers le centre (±0,24), pas d'E2212 ; GTI Club inchangé avec la -5 (122 paquets). Sur racecab1, « 同一の基板IDの筐体が存在します » sous 基板ID = 1 : expliqué à Image borne (une autre borne de même identifiant entendue sur le réseau local, pas le terminal joué), deux vérifications demandées ; changer l'identifiant reste au propriétaire |
| 127 | 25/10 | (Image borne) L'avertissement 基板ID de racecab1 | ✅ noté : Wangan ne tournait pas sur racecab2 quand racecab1 a été réglée ; chaque borne a deux cartes actives (Ethernet 192.168.1.4 / .5 et l'adaptateur VPN de TeamViewer). Piste : racecab1 s'entend elle-même. Proposé au propriétaire : ignorer dans windowsloader les annonces de la borne elle-même sur le port 50765 (pas fait) |
| 128 | 25/10 | (Image borne, pour le propriétaire) Les problèmes restants sur racecab2 : Mario Kart DX sans retour de force, Wasteland Racers bloqué sur « Get Ready To Race », Initial D 6 en erreur 5101 | 🔄 répondu : Mario Kart DX, le réglage du jeu (1.18 « ハンドル反力の使用 » dans ゲーム設定, 1.10 « Reaction force » dans « Game options ») à mettre à ON, avec l'accord du propriétaire ; Wasteland, mes essais tenaient l'accélérateur pendant le chargement (comme l'ancien vjoyd) : essai demandé accélérateur tenu, avec --log ; Initial D 6, le lecteur de cartes n'est émulé que pour ID7/8 (picodaemon) : --log demandé pour voir le port que le jeu ouvre, puis le lecteur émulé de cardrw.cpp à lui donner |
| 129 | 25/10 | Oui pour tout (commit de DX+, release -6, les annonces de la borne, Wangan 6) | ✅ DX+ poussé (`a19c809`, `89cb20a`). L'avertissement de racecab1 n'était pas un écho : mon Wangan d'essai (192.168.1.134, même réseau, ID 1) tournait à 20:28, quand racecab1 a été réglée, et s'est arrêté lui-même sur E2403. Le jeu annonce sa borne à 225.0.0.1:50765 par `WSASendTo`, hors des refus de system.cpp : corrigé pour les deux Wangan (envois vers une autre machine gardés sur 127.0.0.1:50766, paquets d'une autre machine donnés vides), vérifié avec une borne simulée (`ef325a2`, `a1cd3ac`). Release `windowsloader-0.1.0-20261025-6-njz3-windows.zip` (source `a1cd3ac`, 3 752 773 octets, SHA-256 d1e0fe9c…39cbdeb), vérifiée dépaquetée, numéro de build remis à vide ; Image borne prévenue (les mots du propriétaire cités, la pose et Mario Kart à voir avec lui) ; Wangan 6 ensuite |
| 131 | 25/10 | (Image borne) Mario Kart DX, la -6, Wangan sur les bornes, le journal d'Initial D 6 | ✅ noté : Mario Kart DX 1.18 et 1.10 à ON sur les deux bornes, 45 paquets chacun (« E05-11 ERROR DEVICE » en tête du menu de test, déjà vu, sans effet) ; la -6 sur racecab1, racecab2 à suivre ; Wangan 5 (593 paquets) et DX+ dans AttractPlus sur les deux bornes, DX+ à ON sur racecab1, plus de message rouge. Initial D 6 : le journal n'a ni port série ni mémoire partagée pour le lecteur (les 10 082 connexions vont à la clé, 40106) ; copie du jeu depuis racecab1 proposée au propriétaire pour trouver comment il cherche son lecteur |
| 132 | 25/10 | (Image borne) Wasteland Racers sur racecab2 avec la -6, accélérateur tenu : ça marche ; « Get Ready » dure 150 à 170 s, est-ce le jeu ou le chargeur ? | ✅ noté : course et retour de force (184 paquets). L'attente n'est pas le réseau (élection réglée à 176 s) : c'est « Object Manager - Load Map » du jeu, 141 s (209 à 350 s), l'image à 60 images/s ; environ 60 s pour tout « Get Ready » sur mon portable. Deux essais demandés : `--set Display.PRESENT=DIRECT` (si le chargement avance image par image, la recopie GDI le ralentit), et la durée sous TeknoParrot (le disque de la borne) |
| 133 | 25/10 | Termine Wangan 6 | ✅ jouable (non commité) : TeknoParrot lance `AMAuthd.exe` (le démon ALL.Net/Mucha, joint par COM) avant le jeu ; ici, sans lui, l'étape « GAME SERVER CONNECT CHECK » passe sans la réponse du serveur (+0x6a0df3), puis E0552 « OFFLINE UPTIME ERROR » (36 h depuis la dernière authentification, +0x6916bd) levée comme pour Mario Kart DX. Démonstration (bandeau « Please connect this unit to the network », 10 minutes sans arrêt), course en mode Story, STEERING POWER à ON dans le menu de test, retour de force (même carte du volant que le 5 ; gauche −0,25, droite positif). Le nom cherché par `getaddrinfo` est noté au journal (seule l'adresse du terminal) |
| 134 | 25/10 | (Pendant le 133) Pour le serveur ALL.Net, les segatools ne peuvent-ils pas t'aider ? | ✅ répondu : pas pour Wangan 6 : son authentification est Mucha de Bandai Namco, pas l'ALL.Net de Sega que segatools détourne vers un serveur local, et le 6 tourne hors ligne sans serveur. Pour SWDC (Sega ALLS, arrêté au test réseau), oui, c'est la bonne piste |
| 135 | 25/10 | Pour jouer en link à Mario Kart DX, faut-il un serveur comme avec TeknoParrot ? | ✅ répondu : le link de Mario Kart DX est entre bornes (diffusion UDP 20199 sur le réseau des bornes), sans serveur vu dans le jeu ; mais windowsloader isole aujourd'hui chaque borne (même adresse 192.168.112.11 sur toutes, diffusions gardées sur 127.0.0.1, envois vers le réseau des bornes abandonnés) : le link entre bornes ne marche pas. Il faudrait une option qui donne à chaque borne son numéro et fait passer son trafic de link vers l'autre borne |
| 136 | 25/10 | (Image borne) Wasteland avec `PRESENT=DIRECT` : « Load Map » en 12 s au lieu de 141 s, c'est le chargeur ; disques SSD | ✅ corrigé (non commité) : le jeu présente sans attendre l'écran (intervalle IMMEDIATE) et charge une texture par image ; la recopie GDI (2560×1440 MSAA relue et mise à l'échelle) le tenait à 60 images/s. `d3d9hook.cpp` : pour un tel jeu, une recopie par rafraîchissement de l'écran au plus (dès 3/4 de la période), les autres images présentées à la fenêtre cachée. Programme d'essai Direct3D 9 au même appareil : 192 images/s seul, 76,5 avec la -6, 3 387 avec la correction, 41,5 en DIRECT. L'agent bloqué en DIRECT : pendant les 90 s de vidéo d'introduction, le jeu ne présente rien (premier Present à 92 s dans les deux modes), d'où « frame 3 » ; `step` répond au bout de 60 s au plus |
| 137 | 25/10 | Oui, fais commit, pousse, release et notifie Image borne | ✅ 3 commits poussés (`bee00db` Wangan 6 et la table des Wangan, `be27a6c` la recopie GDI, `68bc3b5` BILAN.md) ; release `windowsloader-0.1.0-20261025-7-njz3-windows.zip` (source `68bc3b5`, 3 754 901 octets, SHA-256 10f48c07…7f0cc), vérifiée dépaquetée (Wangan 6 en démonstration, programme d'essai Direct3D 9 à 2 420 images/s), numéro de build remis à vide ; Image borne prévenue (les mots du propriétaire cités, la pose à voir avec lui ; Wasteland à revoir en premier, Wangan 6 et son chemin de réglage) |
| 138 | 26/10 | Étapes suivantes ? ; (Image borne) la pose de la -7 arrêtée : Windows Defender bloque windowsloader.exe (Behavior:Win32/DefenseEvasion.A!ml) | ✅ étapes proposées (Initial D 6 et sa copie, SWDC avec segatools, le link de Mario Kart DX, la recopie GDI en Direct3D 11, les restes des Wangan) ; Defender : pas propre à la -7, la même menace (2147738096) ici le 23/10 sur un lanceur de développement, puis sur la -6 et la -7 dépaquetées (check10, check11), le lanceur 32 bits seul retiré à chaque fois ; ce qu'elle voit est le lancement lui-même (jeu suspendu, VirtualAllocEx, WriteProcessMemory, CreateRemoteThread(LoadLibraryW)), jugé sur chaque exécutable sans réputation. Issues au propriétaire : faux positif signalé à Microsoft, exclusion, signature du code. Racecab1 reste en -6, racecab2 non touchée |
| 139 | 26/10 | Defender plus tard. Fais tous les points : Initial D 6, SWDC avec segatools, link Mario Kart (les deux versions), lenteur des jeux Direct3D, les restes de Wangan 6 | 🔄 commité (`49c2c58` Wangan 6, `7b0af5b` link Mario Kart, `04cc203` Initial D 6, `defc8ef` Direct3D), SWDC en cours (140). Direct3D : la recopie au plus une fois par rafraîchissement pour Direct3D 9, 10 et 11 (`wl_display_gdi_due` dans display.cpp) ; programme d'essai Direct3D 11 59,8 → 176,4 images/s (486 seul), Direct3D 9 7 690. Wangan 6 : l'avertissement des 35 h (« will shutdown shortly ») retiré (+0x68f6d0), menu Tab vu (pause, cinq pages) ; cartes, caméra, pièces laissées (jeu sans carte, terminal en jeu gratuit). SWDC : 4105 sans réglage d'amdaemon, 4103 avec ALL.Net, livraison, monnaie, ABAAS et installation LAN coupés ; il veut son serveur de jeu Sega « ABaaS » (`/api/server/alive`…), que segatools ne fournit pas (il renvoie vers un serveur à part ; ARTEMiS ne sert pas SWDC) : chantier à part (serveur minimal ou hors-ligne). Copies d'Initial D 6 et des Mario Kart DX 1.18 / 1.10 reprises de racecab1 par SMB (Image borne prévenue). Initial D 6 : l'erreur 5101 des bornes vient de picodaemon.exe, que son game.bat lance et pas windowsloader : le lanceur le démarre aussi pour le 6 (vérifié : licence passée, achat de la voiture). Link de Mario Kart DX : `[Game.mkdx] LINK` (les adresses des PC des bornes) et `CABINET` ; le jeu voit le réseau des bornes (192.168.112.10+N), ses paquets UDP 20199/20100 vont aux PC listés ; le PCB-ID reste le réglage du menu de test (E24-03 si deux bornes l'ont pareil). Essai sur ce PC, deux jeux 1.18 (127.0.0.1 / 127.0.0.2, PCB-ID 2 réglé sur le second) : course ensemble, deux joueurs et deux COM, même classement sur les deux écrans ; deux jeux 1.10 de même (« Deux Joueurs VERSUS », course au Château de Peach, même chrono). Une 1.10 et une 1.18 ne se lient pas (diffusions différentes). Reste l'essai sur les deux bornes : leurs adresses dans `LINK`, PCB-ID 2 sur la seconde, le pare-feu (avec l'accord du propriétaire) |
| 140 | 26/10 | Fais le petit serveur SWDC | 🔄 (non commité, `allnet.cpp`) ALL.Net joué sur ce PC dans amdaemon : noms naominet.jp et sous-domaines → 192.0.2.1 (TEST-NET, la bibliothèque d'ALL.Net refuse 127/8), tenporouter.loc et bbrouter.loc → la passerelle, mobirouter.loc et dslrouter.loc introuvables (les deux à la fois : 8005), connexions 192.0.2.1:80 → serveur 127.0.0.1:40180 ; PowerOn (zlib + base64 « DFI », lignes finies par CR LF) et DownloadOrder répondus → test réseau du jeu : authentification ALL.Net GOOD. Le serveur de titre (ABaaS GS) : URL « http:// » hôte « :80 », corps = en-tête de 16 octets (01 01, statut, SDDS, 8 octets : l'IV) + AES-256-CBC(zlib(JSON)), clé dérivée de la « title key » ; /api/server/alive répondu → « GAME SERVER » bon, puis le démarrage demande data/load (free_buckets : réglages de saison, serveurs de matching…), ranking/list, data/save : réponses vides → plantage (données de saison absentes), statut d'erreur → nouvelles tentatives sans fin. Reste : le contenu JSON de ces réponses (le serveur à déplacer dans le jeu pour sa clé), Aime (AiMeDB, port 22345), l'erreur 8404 (serveur d'installation LAN : la livraison réseau veut AMFS\ICF1, absent) |
| 141 | 26/10 | Peux-tu écouter ce qui passe sur les ports TCP quand le jeu fonctionne avec TeknoParrot ? | ✅ sans droits d'administrateur, les connexions seulement : aucune pendant 60 s ; capture pktmon lancée par l'utilisateur pendant un démarrage : aucun échange de SWDC ni d'amdaemon sur le réseau (pas de DNS de naominet.jp, pas de HTTP), seulement l'interface de TeknoParrot vers teknoparrot.com en HTTPS ; la boucle locale n'est pas capturée. TeknoParrot lance le jeu en `-launch=MiniCabinet` et amdaemon avec `config.json` seul. Fichier de capture supprimé par l'utilisateur |
| 142 | 26/10 | Étapes suivantes ? ; fais le 1 (commit et push du travail fini, sans SWDC) puis le 2 (SWDC en mode MiniCabinet) | ✅ commits `49c2c58`, `7b0af5b`, `04cc203`, `defc8ef` et docs, l'état commité compilé seul (32 et 64 bits), poussés. SWDC en `-launch=MiniCabinet`, installation LAN et livraison coupées : le même démarrage (serveur de titre, data/load…) et le même plantage à 17 s, faute de données de saison (+0x30f7f5) ; le mode ne dispense pas du serveur, le 8404 n'a pas eu le temps de paraître |
| 143 | 26/10 | Pourra-t-on avoir les lampes et autres sorties réseau de chaque jeu, avec un passthrough, mode Raw, pour piloter une servoboard ? | ✅ réponse : déjà là avec `[Outputs] OUTPUTS_NETWORK` (protocole MAME/linuxloader, TCP 8000, UDP 8001) : `RawLamps`/`lamp<n>` pour les jeux à carte JVS, `RawDrive` = les octets du jeu à la carte moteur Sega tels quels (mode Raw), sinon des trames de servoboard faites de la force ; passthrough série (`EMULATE_DRIVEBOARD = false`, `SERIAL_n_PATH`). Manquent les lampes des jeux sans JVS (SWDC, Wasteland, Storm Racer, Sega Rally 3, Daytona, Ford Racing, GTI Club 3, Crazy Ride) : proposé en session à part |
| 144 | 26/10 | Afficher la liste (avec ascenseur) des variables que le jeu courant peut émettre, avec leur type, dans « Options du jeu » ou « Sorties et réseau » | ✅ (non commité) onglet « Sorties et réseau » (menu, page 6 pour l'agent) : état du serveur, tableau défilant Nom / Type / Valeur / Description ; les cartes déclarent leurs sorties à l'enregistrement (`wl_outputs_declare_lamps` dans jvs.cpp, `wl_outputs_declare_drive` dans driveboard.cpp), la force d'un autre moteur donne `RawDrive` en trames ; les valeurs sont gardées même sans `OUTPUTS_NETWORK`. Vu dans Mario Kart DX (RawLamps, RawLamps1, lamp0..15) et SWDC (RawDrive, valeur en direct) |
| 145 | 26/10 | Effectue ici la tâche « Lampes réseau pour les jeux sans carte JVS » | ✅ (non commité) `outputs.cpp` : lampes d'une autre carte envoyées comme celles d'une carte JVS (`wl_outputs_name_lamps`, `wl_outputs_lamps` : `RawLamps`, `lamp<n>`, noms pour le menu) et valeurs nommées (`wl_outputs_declare_value`, `wl_outputs_value` : « nom = valeur »). Wasteland Racers 2071 : `SetDigitalOut` → `lamp0..15` (non essayé : le jeu n'est pas sur ce PC). SWDC : commande 04 de l'IO4 (14 sorties, dans amdaemon) → mémoire partagée → le jeu → `lamp0..13` ; `lamp0` s'allume en attract et s'éteint à l'appui sur START (vu par un client TCP). Mario Kart DX : commandes Namco 0x78, 0x79, 0x7A = 9 LED RVB chacune, 4 bits par couleur (27 quartets + 4 bits, bande symétrique) → `led0_r` … `led26_b` (0-15), vues dans le flux (81 valeurs, ~1000 changements chacune par minute d'attract). Wangan : lampes déjà par la carte JVS. Pas encore : Storm Racer G, Crazy Ride, GTI Club 3 (format des lampes dans leurs bibliothèques, absentes de ce PC), carte LED 837-15070 de SWDC (une seule salve au démarrage en mode Cabinet : 0x32 LED 0 à 9 en rouge, 0x39 FET à 0, 0x3C) ; Sega Rally 3, Daytona, GRID, Showdown, Ford Racing : le jeu n'envoie rien à sa carte |
| 146 | 26/10 | Oui, fais le 1 (commit à part de l'onglet « Sorties et réseau » et des lampes) puis le 2 (copie des bibliothèques des lampes depuis racecab1) | ✅ 1 : `d83b381` (code, la partie IO4 de `alls.cpp` seule, sans le serveur SWDC), `2039b31` BILAN, `81f9fbf` NOTES (les lignes 144 et 145 y disent encore « non commité ») ; l'état commité compilé seul (32 et 64 bits), non poussé. 2 : Image borne prévenu, 6 fichiers lus sur `\\racecab1\C\racecab\Games` (~8 Mo, dans le scratchpad) ; puis, non commité : Storm Racer G (`usb_io.dll` : octet 6 de `UsbIO_SetLedLightStatu` = 6 lampes, `UsbIO_SetPWMLightStatu` = 6 intensités) → `lamp0..5`, `pwm0..5` ; Crazy Ride (`UsbPluginsDll_R.dll` : `BIND_SetBoxLight`/`Edge`/`Side`/`MachineSide` 3 bits, `Speed`/`Stop`/`Brake` 2 bits, `GameState`, `SetStartLightState`) → 17 valeurs, la fonction de la DLL appelée ensuite ; GTI Club 3 (`libacio.dll` : `ac_io_hbhi_control_lamp_on`/`off`/`bright`/`mode`, 8 lampes, intensité 0-31) → `lamp0..7`, `bright0..7` ; Wasteland : noms lus dans `Inputs.xml` (1 BOOST, 2 CAMERA, 3 CHEAT1, 4 ACTIVEBOOST, 10 BRAKES, lecture essayée sur la copie). Pas essayé en jeu (jeux absents de ce PC) |
| 147 | 26/10 | Commite et pousse. Fais une release pour les bornes et notifie « Image borne » pour qu'elle mette à jour | ✅ commits `c16c7f7` (lampes de Storm Racer G, Crazy Ride, GTI Club 3, noms de Wasteland) et `db63468` BILAN, poussés avec ceux de la 146 (`47f18f6..db63468`) ; release `windowsloader-0.1.0-20261026-1-njz3-windows.zip` (source `db63468`, sans le travail SWDC en cours, 3 774 565 octets, SHA-256 1118b524…937c1), vérifiée dépaquetée (Wangan 6 en démonstration, sans le message hors ligne), numéro de build remis à vide ; Image borne prévenue (les mots de l'utilisateur cités) |
| 148 | 26/10 | Reprends SWDC (jusqu'à une course) ; le jeu ne passe pas en course ; le jeu reste en attract mode, « press start button » | ✅ (non commité) le serveur de titre dans le jeu (127.0.0.1:40181) avec ses réglages de saison, de titre, recommandés (`update_time` : sinon division par zéro dans DecideTimeSale) et d'événement (les manches) ; le serveur Aime (AiMeDB, 40182, dans amdaemon) ; mode MiniCabinet ; « CONNECT MAIN CABINET » par la diffusion UDP en boucle locale. Partie invité : à la connexion, Start ouvre « ゲストログイン » (いいえ par défaut) → gauche puis Start. Le script d'agent suit les séquences du journal (`[X] BeginPlay`) au lieu d'un plan figé (le jeu plus lent que prévu) et répète Start dans l'attract → course, résultats, continue, game over |
| 149 | 26/10 | Pourquoi « GAME SERVER » est-il marqué « BAD » au démarrage ? | ✅ répondu : `UNetBattleClient::ConnectTest`, « MatchingServerIpAddr is empty » : le serveur de matching (clés matching_server_allnet) n'est pas donné ; sans effet sur une partie seul, pas de course en réseau |
| 150 | 26/10 | Le jeu va en course. Est-ce qu'il y a du FFB ? | ✅ répondu : oui, la carte du volant sur COM1 reçoit ses commandes (84 le couple, 85, 86, 80…) |
| 151 | 26/10 | Est-il possible de passer la langue en anglais ? | ✅ répondu : seulement les messages d'amdaemon (`common.language` ; ressources english, chinese, japanese) ; les textes du jeu n'existent qu'en japonais |
| 152 | 26/10 | Oui, fais les points 1 à 3 (retirer le code de diagnostic ; le lanceur en MiniCabinet avec les réglages d'amdaemon ; la langue anglaise) ; ne mets pas la langue chinoise, mais l'anglaise ; tu peux proposer le chinois, mais avec les noms anglais des langues dans l'interface | ✅ (non commité) diagnostic retiré (journal d'alib, limite du journal remise à 2000, le correctif IsBusy d'Aime) ; le lanceur ajoute `-launch=MiniCabinet` au jeu (sauf un `-launch=` donné) et passe à amdaemon `config.json` et `%TEMP%\windowsloader-swdc-amdaemon.json` (au lieu de `config_LanServer.json`), écrit par le module dans amdaemon (services de Sega et de LAN coupés, langue) ; option `[Game.swdc] LANGUAGE` English / Japanese / Chinese, anglais par défaut. Vérifié : deux parties invité avec les réglages par défaut, sans le correctif Aime (course, résultats, continue, game over, sortie code 0) |
| 153 | 26/10 | Est-ce qu'il y a du FFB dans cette course ? ; dis-moi le résultat du FFB | ✅ le journal de COM1 s'arrête à sa limite (500) avant la course : seconde partie avec la force lue par l'agent et le volant tourné. Course : 5944 changements, −0,961 à +0,961 ; volant à −0,5 moyenne +0,157, au centre +0,020, à +0,5 −0,182 : vers le centre, comme Wangan 6 ; 0 aux résultats |
| 154 | 26/10 | Penses-tu qu'on puisse faire des courses en mode link, entre deux cabines sur le même LAN ? ; oui, fais la première étape ici | ✅ répondu : probablement, mais c'est un gros chantier. Le jeu a un mode course locale (`LocalRace`), trouve la borne principale (MainDetector, UDP 40257), demande au serveur de titre `matching_server_local`, `_develop`, `_allnet`, `_allnet_v105`, `_allnet_v220` (non donnés : « GAME SERVER BAD », MatchingStatus Error) ; la course passe de jeu à jeu (paquets P2P Race, SyncHost, SyncClient). À faire : le serveur de matching (protocole inconnu), le réseau entre les PC (comme le link de Mario Kart DX), le mode (MiniCabinet ou Cabinet), les essais sur deux bornes. 🔄 première étape (une adresse de matching sur ce PC, les connexions du jeu journalisées) écrite puis mise de côté avant tout essai (`matching_probe.patch`, non commité), pour la 155 puis la pause |
| 155 | 26/10 | Fais d'abord une release avec zip et notifie « Image borne » pour qu'elle mette à jour | ✅ sans `-windowed`, la fenêtre de SWDC reste réduite (3 images/s) : fix `43b2bdc` (le sélecteur le donne), commité avec l'accord de l'utilisateur, poussé ; release `windowsloader-0.1.0-20261026-2-njz3-windows.zip` (source `43b2bdc`, 3 798 189 octets, SHA-256 91fdc23f…). Vérifiée dépaquetée : SWDC jusqu'à la partie invité avec `--write-dir`, Wangan 6 jusqu'à l'attract. Un premier essai de Wangan 6 planté (0xC0000374 pendant l'énumération HID : le périphérique Intel 8087:0ac2 de ce PC refuse sa collection et le jeu appelle HidP_GetCaps quand même ; même chemin dans l'essai réussi de la -1 : intermittent). Defender : `Trojan:Win32/Bearfoos.A!ml` sur le windowsloader.exe dépaqueté à son 3e lancement, mis en quarantaine (rien changé à Defender ; windowsloader64.exe et les DLL pas touchés). Image borne notifiée |
| 156 | 26/10 | Je dois éteindre, mets-toi en pause | ✅ la sonde de matching mise de côté, l'arbre propre, rien en cours |
| 157 | 27/10 | (Image borne) la -2 posée sur racecab1 et racecab2, SWDC mis en service (accord du propriétaire) ; sur racecab2 « Error 0919 — DVD Drive Remain Error » (graveur HP en E:) ; corrige d'abord l'erreur 0919 du lecteur DVD | ✅ (non commité) amdaemon cherche un lecteur de CD/DVD parmi les volumes (`FindFirstVolumeW`, `QueryDosDeviceW` → `\Device\CdRom`, `GetDriveTypeW` ; 0917 pour une clé USB) : `alls.cpp` lui cache, et au jeu, les lecteurs de CD/DVD et les disques amovibles (`GetDriveTypeW` / `A` → DRIVE_NO_ROOT_DIR, ces volumes sautés, `\Device\CdRom` introuvable) ; D:, E: et Y: sont des disques fixes. Reproduit sur ce PC avec une image ISO vide montée en E: (CD-ROM) : 0919 en 10 s avec la -2 ; avec le correctif, les volumes CD/DVD et amovible (Z:) cachés, attract sans erreur ; image démontée ensuite |
| 158 | 27/10 | (Image borne, pour le propriétaire) Quels jeux sont déjà jouables en link (2 ou 4 bornes) avec la -2, et ce qui doit différer ou être pareil d'une borne à l'autre ? | ✅ répondu : Mario Kart DX seul (1.18 et 1.10, `[Game.mkdx]`) : `LINK` = les adresses des PC, la même ligne partout (racecab1 en premier) ; `CABINET` 1 / 2 ; PCB-ID différent sur chaque borne (menu de test) ; UDP 20199 et 20100 ; rien de plus pour le solo (sans `LINK`, seule) ; jusqu'à 4 bornes. Pas de link pour SWDC (matching), les Wangan (gardés sur le PC exprès), Daytona et les autres. Erreur de ma réponse : la 1.10 dite « pas essayée en link », alors qu'elle l'avait été sur ce PC (course au Château de Peach, BILAN) : corrigé auprès d'Image borne |
| 159 | 27/10 | Commit et pousse (le correctif de l'erreur 0919) | ✅ `81866f7` (code et README), `23ce37a` (BILAN et NOTES), poussés |
| 160 | 27/10 | Étapes suivantes ; (Image borne) link de Mario Kart DX vérifié sur les deux bornes avec la -2 | ✅ étapes proposées (release du correctif 0919 puis SWDC sur la borne, link de Mario Kart DX, sonde de matching de SWDC, Defender, restes de SWDC, plantage HID de Wangan 6). Mario Kart DX 1.18 : `--set Game.LINK=192.168.1.4,192.168.1.5`, `CABINET` 1 / 2, PCB-ID 1 / 2 : paquets dans les deux sens, démo synchronisée, une course à quatre (deux bornes + deux COM), l'objet de l'autre Mario visible ; 1.10 : « Deux Joueurs VERSUS / CO-OP » proposé sur les deux, course pas lancée ; bandeau hors ligne sans effet. BILAN mis à jour (link ✅ pour la 1.18). Le correctif 0919 : Image borne le transmet au propriétaire (pas un accord) |
| 161 | 27/10 | Fais la release et préviens Image borne ; j'ai ajouté une exclusion pour Windows Defender | ✅ release `windowsloader-0.1.0-20261027-1-njz3-windows.zip` (source `23ce37a`, 3 800 337 octets, SHA-256 afbec123…), seul changement le correctif 0919 de SWDC. Vérifiée dépaquetée : SWDC par windowsloader.exe (32 bits) avec dossier d'écriture et l'image ISO remontée en E: : « hidden (CD/DVD drive) », attract sans erreur ; Wangan 6 jusqu'à l'attract (les mêmes 10 exceptions C++ attrapées qu'avec la -2). Defender n'a rien bloqué cette fois. Image borne prévenue (la pose : accord du propriétaire) |
| 162 | 27/10 | Termine ensuite SWDC et faisons les jeux suivants | 🔄 SWDC : menu Tab vu (7 pages, jeu en pause), menu de test (« GAME TEST MENU », test d'entrées : volant, pédales, boutons) ; mode Cabinet, option CABINET, boutons du volant et sorties réseau (163 à 166) ; le test du retour de force reste sur « CHECKING... » (le jeu interroge la carte par FD, test dans les Blueprints) ; restent le matching, vJoy, un vrai volant. Jeux suivants pas commencés |
| 163 | 27/10 | (Image borne) la 20261027-1 posée sur racecab1 et racecab2 (accord du propriétaire) ; 0919 corrigée sur racecab2 ; en mode agent, des Start pas pris, partie invité pas menée au bout | ✅ noté (« hidden (CD/DVD drive) » dans le journal d'amdaemon, Network Test, attract, pièce, LOGIN) ; la connexion invité expliquée : Start ouvre une question dont le choix par défaut est いいえ, puis gauche (はい) et Start |
| 164 | 27/10 | Il faudrait aussi faire les inputs et outputs en mode cabinet normal, pas en mini | ✅ (non commité) le mode Cabinet joue maintenant (services réseau sur le PC, installation LAN coupée : plus de 8404 ni de 4103), partie invité complète ; les boutons du volant y passent par le tableau de bord (COM20, réponse « ps » et six chiffres : jaune, rouge, vert, bleu, palette droite, palette gauche, trouvés au test d'entrées) ; en MiniCabinet amdaemon ne transmet pas le mot du joueur 2 de l'IO4 (FFFF, octets 34 à 63, max_player 2 essayés) : boutons du volant absents |
| 165 | 27/10 | Fais en sorte que le choix du cabinet soit une option du jeu, modifiable en fichier de config ou via la page des paramètres du jeu, prise au lancement | ✅ (non commité) `[Game.swdc] CABINET` = `CABINET` (défaut) ou `MINI` (« Type de borne », page Options du jeu, au prochain lancement) ; lu par le lanceur (`g_gameOptionArgs` : --set Game.CABINET=, sinon le fichier ou son `swdc.ini`), qui donne `-launch=Cabinet` / `-launch=MiniCabinet` sauf si la ligne de commande a déjà un `-launch=`. Vérifié : défaut → Cabinet ; --set MINI → MiniCabinet ; fichier « mini » → MiniCabinet ; -launch=Cabinet à la main l'emporte sur MINI |
| 166 | 27/10 | En fonction du type de cabinet, ajoute les variables de sorties network supplémentaires (vitesses, LED, etc.) | ✅ (non commité) en mode Cabinet seulement (selon le -launch= du jeu) : tableau de bord → `speed`, `gear`, `rpm`, `dash_lamp0` à `dash_lamp7` ; carte LED 837-15070 → `led0_r` à `led8_b` (0-255), `fet0` à `fet2`. Course enregistrée sur le port 8000 : speed 74 à 116 km/h (116 valeurs), gear 2/3/4, lampes et LED (rouge, orange, jaune) ; le clignotement et le fondu pas rejoués |
| 167 | 27/10 | (Driveboard émulation et FFB, pour le propriétaire) proposer le choix du dispositif de retour de force dans le menu Tab, page « Retour de force » | ✅ fait dans la 170, avec l'accord de l'utilisateur. Règles de sm2-emu et Supermodel : `[ForceFeedback] FFB_DEVICE` = `AUTO` ou le nom système du dispositif, comparé en entier sans la casse ni les espaces autour ; absent : retour en Automatique (le volant du joueur 1 ; décision du propriétaire, qui remplace le « sans repli » annoncé d'abord), « <nom> (non connecté) » dans la liste, repris s'il est branché ensuite (si le branchement se voit) ; AUTO aussi pour une valeur vide ; deux du même nom : celui d'où la direction est lue ; changement appliqué tout de suite |
| 168 | 27/10 | (Linuxloader FFB edition) vJoy pris ~2 min pour un essai, puis par Flycast ~10 min | ✅ noté ; les essais de SWDC n'utilisent pas vJoy (pas de --controllers) |
| 169 | 27/10 | (Linuxloader FFB edition) vJoy repris pour de courts essais, puis libre | ✅ noté ; mes deux passages sur vJoy (course SWDC avec --controllers, essais de FFB_Device) annoncés avant et rendus après |
| 170 | 27/10 | Fais le point 2 : ajoute le choix du FFB tel que demandé par l'autre session, avec option fallback ; (Multi-Board/Flycast, pour le propriétaire) « peux-tu aussi demander à la session windowsloader de gérer ce comportement fallback/retour au dispositif ? » | ✅ (non commité) `controls.ini` `[Config] FFB_Device` = AUTO (ou vide : le volant du joueur 1) ou le nom système du dispositif (SDL, « vJoy Device »), comparé en entier sans la casse ni les espaces autour ; `--set ForceFeedback.FFB_DEVICE=` l'emporte. Le choisi joue seul les effets ; de deux du même nom, celui du joueur 1 ; absent (au départ ou débranché) : le volant du joueur 1, et il reprend les effets dès qu'il est rebranché (les dispositifs sont réattribués à chaque branchement, la force du moment renvoyée). Menu Tab, page Retour de force : liste « Dispositif de retour de force » (Automatique, les dispositifs connectés, « <nom> (non connecté) »), appliquée tout de suite (l'effet de l'ancien fermé, la force renvoyée au nouveau) et enregistrée. Vérifié avec Wangan 6 et vJoy : AUTO → vJoy Device (joueur 1) ; « VJOY device » avec espaces → vJoy Device (FFB_Device) ; un volant absent → message unique, vJoy Device (joueur 1) ; la liste affiche « (non connecté) ». Pas vérifiés : le changement depuis le menu (l'agent ne clique pas dans une liste) et le rebranchement (vJoy ne se débranche pas) |
| 171 | 27/10 | Lis tes messages | ✅ pas de message dans la boîte de la session ; résumé des messages des autres sessions (Flycast : retour en automatique et retour au dispositif ; FFB : la même correction ; Linuxloader : vJoy libre) |
| 172 | 27/10 | Commit et pousse, puis fais une release ; notifie Image borne pour qu'elle mette à jour | ✅ commits `30e1508` (SWDC : option CABINET, tableau de bord, carte LED), `ee7c600` (FFB_Device), `eb9e32b` (BILAN, NOTES), poussés ; release `windowsloader-0.1.0-20261027-2-njz3-windows.zip` (source `eb9e32b`, 3 809 762 octets, SHA-256 2f5b7566…) ; vérifiée dépaquetée : SWDC par windowsloader.exe (32 bits) avec dossier d'écriture, en Cabinet par défaut, jusqu'à la partie invité ; Wangan 6 jusqu'à l'attract ; Defender n'a rien bloqué. Image borne prévenue (le défaut de SWDC passe en Cabinet ; pose à l'accord du propriétaire) |
| 174 | 27/10 | Quel est le jeu suivant de la liste dans NOTES ? ; demande à Image borne la liste des jeux Windows que tu pourrais prendre en charge | ✅ réponse d'Image borne (profils de racecab1, bornes éteintes) : les candidats ajoutés à « Jeux des bornes » (Autres jeux Windows) : Frenzy Express, Initial D Zero v2.30 et v1.31 ; avant : plus de jeu dans la liste « Jeux des bornes » (les 21 faits, SWDC compris, et une partie des écartés) ; demande envoyée à Image borne (nom, profil, chemin et architecture de l'exe, plateforme, loader actuel, protection, retour de force, taille), renvoyée après le redémarrage de sa session ; le tri sera soumis à l'utilisateur avant toute copie |
| 176 | 27/10 | (Image borne) la 20261027-2 posée sur racecab1 et racecab2 (accord du propriétaire) ; SWDC rejoué sur racecab2 | ✅ noté : SWDC en Cabinet sur racecab2 (« the game plays a full cabinet », -launch=Cabinet, lecteur DVD caché, attract sans erreur, au test d'entrées STEERING BUTTON BLUE et STEERING PADDLE L à ON ; partie invité pas rejouée). |
| 178 | 27/10 | Corrige le gel de Wangan 6 ; continue sur l'émulation de la carte moteur ; poursuis sur Wangan 6 | 🔄 gel vu une fois, sur racecab2 (20261027-2, mode agent, STEERING POWER sur ON) : plus d'image pendant le premier step (image 318), juste après l'ouverture de COM1, le jeu vivant. Non reproduit : ici (STEERING POWER sur ON par un dossier d'écriture, lanceur seul, agent, agent avec images, la suite exacte de racecab2 par windowsloader.exe) ni sur racecab1 (trois essais d'Image borne, accord du propriétaire : lanceur seul, STEERING POWER sur ON, mode agent). L'échange COM1 est le même ligne pour ligne sur les deux bornes. Ajouté (non commité) : le rapport de gel (`crash.cpp`), sans image pendant 5 s (la pause du menu Tab mise à part), le journal liste chaque thread du jeu et où il attend, puis à 15 s ; vérifié en 64 et 32 bits (thread principal retenu 17 s). Version de test dans `releases/test-wmmt6-freeze` ; essai sur racecab2 demandé à Image borne pour quand elle sera rallumée |
| 179 | 27/10 | Que peux-tu faire en attendant ? | ✅ proposé : Wangan 6 en course avec STEERING POWER sur ON (retour de force), le commit du rapport de gel, Frenzy Express, Initial D Zero v2.30, le link de SWDC |
| 180 | 27/10 | A-t-on l'option de lire les entrées en arrière-plan ? ; non, le clavier reste lu seulement quand la fenêtre a le focus pour le moment, on fera cela plus tard | ⛔ abandonné (demande 190) : le clavier reste lu seulement quand une fenêtre du jeu a le focus, à ne pas implémenter (perturbant pour l'utilisateur). Les manettes, volants et vJoy (SDL) restent lus en arrière-plan ; le clavier (`KEY_...`), Tab et F11 seulement quand une fenêtre du jeu est devant |
| 181 | 27/10 | Fais les points 1 à 5 ci-dessus | 🔄 (2) commits `259aba8` (rapport de gel) et `48b27df` (NOTES), non poussés. (1) ✅ Wangan 6 en course avec STEERING POWER sur ON (dossier d'écriture) et la version du jour : Yokohama, commandes du moteur (cible, raideur, amortissement), force −0,20 volant à gauche, +0,20 à droite, 0 au centre, comme le 25/10 ; aucune ligne « freeze: ». (3) copie de Frenzy Express demandée à Image borne (racecab1, accord du propriétaire attendu). (4) Initial D Zero v2.30 après Frenzy Express. (5) serveur de matching de SWDC commencé puis mis de côté (182). Racecab1 ne se fige dans aucun des cinq essais de Wangan 6 d'Image borne (freeze1 à 5) : l'essai utile reste sur racecab2 |
| 182 | 27/10 | Mets de côté le serveur pour SWDC, une autre personne va travailler dessus | ✅ mis de côté, la sonde retirée (`matching_probe.patch` dans mon dossier de travail). Relevé pour la suite : avec `matching_server_*` = 127.0.0.1 donnés par le serveur de titre, le jeu se connecte en TCP au port 55000 (`UNetBattleClient::ConnectTest`) ; un serveur qui accepte suffit pour « GAME SERVER GOOD » (result 1) ; le jeu envoie deux fois `0c 73 73 73 73` puis ferme. Protocole binaire propre au jeu : commandes CONNECT, MESSAGE, MATCHING, MATCHINGCANCEL, VEHICLE, CLOSE, LEFTTIME, MATCHINGTIMEOUT, COMPLETE, MATCHINGINFO, USERIDRESULT, MATCHINGERROR ; classes `packet::` TcpPacketBase, UdpPacketBase, Close, CloseResult, Lobby, LobbyResult, LocalHp, Matching, MatchingCancel, MatchingCancelResult, MatchingCompleteResult, MatchingCreateResult, MatchingResult, Ping, UdpHp, UdpHpResult, UdpHpSendResult, UserIdResult, Vehicle ; borne principale par UDP 40257 (MainDetector). Rien de public trouvé sur ce protocole |
| 183 | 27/10 | Où en es-tu de la copie ? ; vas-y, fais Frenzy Express | ✅ copies d'Image borne (accord du propriétaire) : Frenzy Express (1 825 fichiers, 130 Mo, `games/frenzyexpress`), Initial D Zero v2.30 (18 385 fichiers, 14,1 Go, `games/idz230`, profils TeknoParrot dans `games/idz230-borne`). Frenzy Express jouable (non commité) : module `uniana.cpp` (`XIO::update` de `xio.dll` rempli par windowsloader, le moteur du guidon en retour de force, les lampes en sorties réseau), affichage OpenGL 640×480 placé selon `DISPLAY_MODE` (`wl_gl_picture`, viewports déplacés, viewport relu rendu au jeu), entrée du sélecteur, `PedalLeft`/`PedalRight` dans controls.ini. Vérifié en agent : démarrage, attract, menu de test, partie (pièces, choix, mission, course à 54 km/h), étalonnage passé par un guidon qui suit le moteur. Voir BILAN |
| 184 | 27/10 | Va en menu de service avec le bouton Test pour voir les retours analogiques et digitaux | ✅ Test = bit 0x04 (MAIN MENU : I/O CHECK, LAMP CHECK, SCREEN CHECK, COLOR CHECK, SOUND/GAME/COIN OPTIONS, CALIBRATION, BOOKKEEPING, ALL FACTORY SETTINGS, GAME MODE). I/O CHECK : guidon aux deux bouts et à mi-course, frein à mi-course et au maximum, poussées gauche et droite (0x01), l'accélérateur les alternant, LEFT/RIGHT SELECT, SERVICE, TEST : tous vus. Les bits des boutons corrigés en chemin (Start 0x01, Service 0x02, Test 0x04, pièce 0x08) : planche envoyée |
| 185 | 27/10 | Pour le compteur de fps, mets un chiffre après la virgule (59.9 fps) ; (Linuxloader FFB edition, pour le propriétaire) la même demande | ✅ (non commité) titre « … - 60.1 fps - windowsloader … », mesuré au compteur de performance (GetTickCount, au pas de 16 ms, rendait la décimale aléatoire) ; README mis à jour ; Linuxloader prévenu |
| 186 | 27/10 | Oui, commite et pousse, puis fais Initial D Zero ; refais ton message en français ; fais les étapes 1 à 3 | ✅ poussé `eb9e32b..226d613`. Initial D Zero v2.30 (non commité) : la Nu dans `alls.cpp` (registre AAV2/SDDF, carte AMEX : SRAM, EEPROM, DS, GPIO, JVS par leurs interfaces), JVS 837-14572 et levier en H, Aime 837-15286 sur COM10, lampes 837-15070 sur COM11. (1) ✅ le server box du magasin en second compagnon (le lanceur en démarre maintenant plusieurs), sa fenêtre cachée (`wl_display_hidden`) : ERROR 8306 levée, attract, partie invité jusqu'en course. (2) le serveur UserDB pas nécessaire pour jouer en invité : seules les cartes Aime l'attendent (bandeau « Aime cannot be used for Online Battle »), pas fait. (3) ✅ le moteur du volant sur COM1, Sega servoboard en statut long (FA 00 10) : FORCE FEEDBACK TEST n'est plus bloqué sur CHECKING, LEFT/RIGHT dans le bon sens, couple en course. Corrigés en chemin : les jeux de SetupDi rendus par Windows avec le même handle (`devices.cpp`), le décalage d'un octet après FA (Initial D 6 aussi). SWDC vérifié (test réseau comme avant) |
| 187 | 27/10 | Pour le protocole de la servoboard, demande à la session BackForceFeeder ; tu peux consulter toi-même DRIVEBOARD.md du dépôt BackForceFeeder | ✅ réponse de « Driveboard émulation et force feedback » (captures d'une vraie borne ID4/ID5) et DRIVEBOARD.md relu (« Reply mode: 0xFA », statuts courts et longs) : FA 00 10 = statuts de 4 octets (B0/90 et l'état), FA 00 14 = avec la position, FA 00 1F et FF = statuts d'un octet ; tout est fait dans `driveboard.cpp` |
| 188 | 27/10 | Commit et pousse ; étapes suivantes ? ; fais 1, 3 et 4 (Initial D 6 en course avec le changement de la carte moteur, Initial D Zero v1.31, le frein d'Initial D Zero) | ✅ poussé `226d613..12f52ab`. Non commité : (1) Initial D 6 en course (`id6_race.py`) avec les statuts longs : forces comme avant. (4) les pédales d'Initial D Zero sur 0 à 0x3000 (le jeu montre 0 à 255 sur cette course, 255 au-delà ; avant, saturées à 19 %). (3) v1.31 copiée par Image borne (`games/idz131`, 12,3 Go, le jeu dans `app\package`) et jouée par le même module : son amdaemon cherche les noms d'ALL.Net en ROT13 (`anbzvarg.wc`, `nvzr.anbzvarg.wc`), décodés par `allnet.cpp` ; ses `ICF1`/`ICF2` sont dans le `amfs` à côté de `app\`, pris pour E: ; partie invité jusqu'à la course (170 km/h), retour de force ; entrée « Ver.1.31 » du sélecteur (horodatage de l'exécutable). En passant : `wl_sram.bin` et `wl_eeprom.bin` de la carte AMEX allaient à côté du jeu même avec `--write-dir` : ils vont maintenant dans le dossier d'écriture (`wl_files_write_path`) ; la SRAM reprise d'un lancement précédent donnait une ERROR 8304 passagère. Menu Tab vérifié sur Initial D Zero (Direct3D 11) |
| 189 | 27/10 | Regarde aussi le menu Tab pour les jeux OpenGL | ✅ (non commité) menu Tab en OpenGL : dans `SwapBuffers`, backend OpenGL 2 de Dear ImGui (opengl32 chargé en différé, seulement par un jeu OpenGL), l'image du jeu relue dans une texture et redessinée sous le menu. Initial D 6 laisse programmes Cg, tampons et autres états actifs (menu noir, retourné, décalé) : état neutre posé autour de chaque dessin et celui du jeu rendu. Vérifié : Frenzy Express (image placée par windowsloader) et Initial D 6 (pages, reprise du jeu). Initial D 7 et 8 : le même chemin, pas de copie sur ce PC |
| 190 | 27/10 | Oublie la lecture du clavier en arrière-plan : à ne pas implémenter, perturbant pour l'utilisateur ; la fenêtre doit avoir le focus pour que les touches soient reçues | ✅ noté, ligne 180 corrigée ; rien à changer : le clavier n'est lu qu'avec le focus |
| 191 | 27/10 | Vérifie l'émulation de la servoboard avec le protocole (DRIVEBOARD.md) ; elle doit être commune à tous les jeux Sega | ✅ (non commité) `driveboard.cpp` relu commande par commande : 80 (mode court, la force retombe 240 ms après la dernière trame ; l'activation), 83 (gain), 84 (couple), 85 (sinus : octet 1 la fréquence en demi-hertz, octet 2 l'amplitude ; l'ancien code les lisait à l'envers), FF (valeurs par défaut), joués par un fil toutes les 10 ms. Une seule émulation pour les jeux Sega à servoboard série (Initial D 6 à 8, K.O. Drive, Sonic, SWDC, Initial D Zero) ; les cartes moteur sur port parallèle (Sega Rally 3, Daytona, Ford Racing, GRID, DiRT) gardent leur décodeur. Revérifié en course : Initial D 6, Initial D Zero, SWDC |
| 192 | 27/10 | Commit et pousse ; étapes suivantes ? | ✅ poussé `12f52ab..78180dd` (menu Tab OpenGL, effets de la servoboard, Initial D Zero v1.31, docs). Proposées : une release pour les bornes, l'affichage OpenGL d'Initial D 6 à 8, les cartes Aime d'Initial D Zero, le gel de Wangan 6 sur racecab2, les ⏳ du bilan |
| 193 | 27/10 | Fais l'affichage OpenGL | ✅ (non commité) `[Display]` appliqué aux jeux OpenGL comme aux jeux Direct3D : le jeu dessine dans un framebuffer de windowsloader à la taille de sa fenêtre, recopié à chaque image (`glBlitFramebuffer`) là où va l'image, bandes noires autour ; la fenêtre réglée selon `DISPLAY_MODE`, F11, Alt+Entrée ; recadrage sans courbure. Le jeu n'en voit rien (ses `glBindFramebuffer(0)`, `glGetIntegerv`, `glDrawBuffer`/`glReadBuffer` sont traduits). Vérifié : Initial D 6 (ORIGINAL, 4:3, 4:3_CROP, 4:3_STRETCH, fenêtre, plein écran fenêtré, plein écran ; course rendue comme avant ; menu Tab) et Frenzy Express (qui limitait sa fenêtre à 640×480 hors tout : limites de Windows rendues). L'agent `screen` d'un jeu OpenGL relit la fenêtre par OpenGL (la copie d'écran GDI tombait à côté sur ce portable mis à l'échelle). Initial D 7 et 8 : même code, pas de copie ici |
| 194 | 27/10 | Fais les corrections (servoboard, d'après les relevés de linuxloader) et renomme les fichiers driveboard relatifs à la servoboard en servoboard ; garde le nom driveboard pour les cartes moteur comme Sega Rally 3 ; si c'est le même fichier, scinde-le ; une classe parente et deux sous-classes si c'était la même classe | ✅ (non commité) `driveboard.cpp`/`.h` → `servoboard.cpp`/`.h` (`git mv`), `wl_servoboard_register`, `wl_servoboard_free_wheel(_test)`, journal « servoboard: ». Rien à scinder : ce fichier ne sert que la servoboard série ; les cartes moteur (Sega Rally 3 au port 0x1020, Daytona, Ford Racing, GRID, DiRT au port parallèle, Sega Racing Classic) ont leur décodeur dans le module de leur jeu, sans code ni classe commune. `EMULATE_DRIVEBOARD` garde son nom (celui de linuxloader, pour toutes). Corrections : recherche d'index 4,05 s ; FF répond dans le mode qu'il termine ; FA 00 00 à 0F relisent les dernières 80 à 8F ; la lecture rend les réponses en attente jusqu'à la taille demandée. Banc d'essai (les exemples des relevés) et courses d'Initial D 6 et d'Initial D Zero v1.31 |
| 195 | 27/10 | Commit et pousse | ✅ `2563515` (affichage OpenGL), `26c9b6f` (servoboard : nom et relevés), puis la doc ; poussés |
| 196 | 27/10 | Précise-moi le dernier point (l'exception setupapi d'Initial D 6) ; oui, fais-le | ✅ (non commité) cause : la liste de périphériques que le jeu libérait (`SetupDiDestroyDeviceInfoList`, pas suivi) restait notée comme celle de `\\.\mxsmbus` ; Windows redonnait le même identifiant à DirectInput (sa liste HID), que windowsloader ne note pas : DirectInput recevait la carte émulée, et setupapi lisait sa marque 0x574C4844 comme un pointeur. `devices.cpp` : `SetupDiDestroyDeviceInfoList` suivi, et l'identifiant rendu oublié à chaque `SetupDiGetClassDevs`, ceux de Windows compris. Trois lancements d'Initial D 6 : DirectInput trouve le vJoy, plus d'exception |
| 197 | 27/10 | Sega Rally 3 : les pédales ne marchent pas, la voiture part en marche arrière à chaque fois ; sur la borne avec le BFF, en forçant les axes, en boîte automatique la voiture recule alors que les menus se valident ; tous les autres utilisateurs ont le même problème ; recopie en local Sega Rally 3 Standard | 🔄 pas de copie du jeu sur ce PC (effacée le 22/10). En mode agent l'accélérateur validait les menus et la voiture avançait : le codage de la carte est juste. Relevés d'Image borne sur racecab1 (racecab2 éteinte) : pas de journal windowsloader de Sega Rally 3 (pas de --log) ; [CalibrationData] des neuf éditions = 40..210 (comme les valeurs par défaut du module) ; controls.ini : P1_Gas = JOY0_AXIS_1_NEGATIVE_SIDE, P1_Brake = JOY0_AXIS_2_NEGATIVE_SIDE, GearUp/GearDown = JOY0_BUTTON_8/9, les mêmes axes que le profil TeknoParrot. L'étalonnage est écarté ; DirectInput aussi (les journaux de ce PC : le jeu n'énumère que clavier et souris). Copie de l'édition Standard (Image borne, 519 fichiers, 1,29 Go, games/segarally3) ; reproduit avec le vJoy de ce PC et le controls.ini de la borne (sr3_vjoy.py du bloc-notes) : pédales au repos au milieu de leur axe (la disposition du BFF) : 40/40 au repos, 210 enfoncées, boîte AT, la voiture avance (100 à 156 km/h). Axe du frein (Z) au minimum au repos : frein lu enfoncé (210), les menus se valident à l'accélérateur, en course rapport R et « MAUVAIS SENS ! » : le symptôme de la borne. windowsloader lit juste ; sur la borne, le frein n'est pas au milieu de son axe au repos. Vérifié ensuite avec le vJoy (Y- accélérateur, Z- frein, du milieu relâché à 0 enfoncé, sr3_halves.py) : valeurs reçues par la carte 40 de 0x8000 au milieu, puis 48 (0x3C00), 81 (0x3000), 124 (0x2000), 167 (0x1000), 188 (0x0800), 210 (1), pareil pour le frein. En course, boîte AT : demi-accélérateur 46 km/h à 3 s, 65 km/h à 6 s, en 1re au rupteur (pas de passage de rapport) ; à fond 4e, 114 km/h à 6 s ; demi-frein et frein ralentissent ; frein à fond à l'arrêt : R, la voiture recule (« MAUVAIS SENS ! ») ; un frein à peine enfoncé (43, 48, 59) à l'arrêt : 1re, 0 km/h, pas de recul. Le jeu tourne à 10-27 images/s ici : attentes en secondes. Frein à fond depuis 118 km/h, une image toutes les 0,3 s : 108 (4e), 84 (3e), 57, 22 (1re), arrêt vers 1,6 s, puis R 2, 11, 35, 48, 60 km/h à 3,4 s : le frein ralentit jusqu'à l'arrêt, puis la boîte AT passe en R tant qu'on le garde (la logique du jeu, AVCVehicleAutoGearBox ; le rapport de la carte passe par le même code qu'une vraie : octet 3 frein vers la voie 4, octet 4 accélérateur vers la voie 7, Rally.exe+0x17b6eb). À vérifier sur la borne : « Frein (axe) » au repos et « Accélérateur (axe) » à fond dans la page Contrôles du menu Tab |
| 198 | 27/10 | (Linuxloader, demande du propriétaire) mets les pédales du BFF dans les liaisons par défaut | ✅ (non commité) P1_Gas = GC0_AXIS_RIGHTTRIGGER, JOY0_AXIS_1_NEGATIVE_SIDE ; P1_Brake = GC0_AXIS_LEFTTRIGGER, JOY0_AXIS_2_NEGATIVE_SIDE (accélérateur sur Y-, frein sur Z-, au repos au milieu). Vérifié avec le vJoy de ce PC sur Initial D 6 (controls.ini vide complété) : 0 au repos, 126 à mi-course, 255 à fond. Un controls.ini existant garde ses lignes |
| 199 | 28/10 | Vérifie avec ton vJoy si la liaison des axes marche et si le jeu voit les demi-courses négatives en course ; l'axe Y de l'accélérateur et l'axe Z du frein vont de mi-course (relâchée) à 0 (enfoncée) ; le gros R au milieu du cadran veut dire marche arrière à 61 km/h | ✅ voir la demande 197 : liaisons justes (40 au repos, 124 à mi-course, 210 à fond, linéaire sur la moitié basse), le jeu voit les demi-courses (1re bloquée à 65 km/h à demi-accélérateur, 4e à fond) ; le R venait du frein gardé après l'arrêt (la boîte AT du jeu) |
| 200 | 28/10 | Commit et pousse | ✅ `46e3214` (SetupDi), `9399176` (pédales du BFF par défaut), puis la doc ; poussés |
| 201 | 28/10 | Notifie Image borne ; commit, pousse, fais une release et notifie Image borne | ✅ Image borne prévenue des changements poussés. Release `windowsloader-0.1.0-20261028-1-njz3-windows.zip` (source `ea1c9c4`, 3 711 704 octets, SHA-256 a8f019a8…bf860eb) dans `emulateurs\releases` : les quatre binaires, LICENSE, README.md, RELEASE.txt (nouveau depuis la 20261027-2 : Initial D Zero Ver.2 et 1.31, Frenzy Express, menu Tab et affichage des jeux OpenGL, servoboard d'après DRIVEBOARD.md et les relevés, pédales du BFF par défaut, Sega Rally 3 vérifié au vJoy, exception setupapi d'Initial D 6, rapport de gel, fps à une décimale). Trouvé en la vérifiant : le menu Tab d'Initial D 6 transparent sur une image claire (le jeu laisse un mélange GL_MAX) : corrigé (`a444779`). Vérifiée dépaquetée : Frenzy Express (étalonnage, menu), Initial D 6 (menu opaque), Initial D Zero v1.31 en 64 bits (ALL.Net, Aime, réglage du volant, menu). Image borne prévenue (pose à l'accord du propriétaire) |
| 202 | 28/10 | Frenzy Express fait une erreur steering au démarrage | ✅ `ea1c9c4` : l'étalonnage (xwin.exe+0x25c00) prend la position du guidon à la fin de la poussée à droite, puis à gauche, et donne « ERROR THE STEERING WHEEL » puis les valeurs par défaut si la gauche n'est pas sous la droite : un guidon que le moteur ne tourne pas (vJoy, sans retour de force, trop faible ou inversé) les donne égales. Il reçoit maintenant 0xFF poussé à droite, 0x00 à gauche, 0x80 au milieu, la plage de windowsloader : il passe (gauche 0x00, milieu 0x7f, droite 0xff), le moteur joue toujours. Dans la release 20261028-1 |
| 203 | 28/10 | (Image borne) la 20261028-1 posée sur les deux bornes ; Frenzy Express et Initial D Zero 1.31 essayés sur racecab1 | ✅ noté : posée avec l'accord du propriétaire, .ini gardés, anciens binaires dans _sauvegardes\windowsloader-avant-20261028-1, aide « 0.1.0-20261028-1-njz3 FFB Edition ». Racecab1, agent : Frenzy Express en démo à 1 800 images sans « ERROR THE STEERING WHEEL » ; Initial D Zero 1.31 au réglage du volant à 3 600 ; tout fermé au quit. Pas encore : racecab2, Initial D Zero 2.30, Initial D 7 et 8, K.O. Drive, Sonic, la page Contrôles de Sega Rally 3, les vraies commandes. Frenzy et Initial D Zero ne sont pas encore dans RunWindowsLoaderGame.cmd ni dans les listes AttractPlus (à la décision du propriétaire) |
| 204 | 28/10 | Ajoute une option en ligne de commande pour lister les jeux pris en charge : nom du jeu, nom compact (nom de rom), version, exécutable attendu, CRC32 de l'exécutable | ✅ `1534f99`, `9942fbb` : `windowsloader.exe --list-games` : une ligne par programme accepté (jeu, nom court, version, programme, CRC32). La table du sélecteur (`gamelist.cpp`) reçoit le nom court (celui du module : [Game.<nom>], WRITE_DIR\<nom>), la version (Initial D Zero 1.31 / 2.30, Mario Kart DX 1.18 / 1.10 ; ailleurs « - », les copies ne la disent pas : ressources de version vides ou sans sens, SWDC annonce ver=0.00 à ALL.Net) et un CRC32 par programme. CRC32 : ceux des copies de ce PC (16 programmes), puis le relevé d'Image borne sur racecab1 (lecture seule, 56 exécutables, les mêmes CRC pour les copies communes) : tous sauf le game4.exe de Battle Gear 4 2006, absent de la borne ; versions aussi d'après les noms de dossier (Battle Gear 4 Tuned 2.8, Crazy Ride 1.9.0), les ressources de version qui ont un sens (GRID 1.1, DiRT Showdown 1.2, Wasteland 1.6) et l'Install.ini de Daytona (171221, 190306). La New Season Edition de Daytona a sa ligne (son propre Daytona.exe) ; le sélecteur montre toujours les deux éditions comme la première. Le CRC32 est seulement indiqué (demande du propriétaire : ne jamais refuser un exécutable d'un autre CRC32) : la reconnaissance ne le lit pas |
| 205 | 28/10 | Commit et pousse, fais une release et notifie Image borne ; attends le retour d'Image borne dans ce cas | ✅ release faite après le relevé d'Image borne : `windowsloader-0.1.0-20261028-2-njz3-windows.zip` (source `9942fbb`, 3 714 713 octets, SHA-256 2e05b312…c5ea2e7d), seul changement --list-games ; vérifiée dépaquetée (la liste en 32 et 64 bits, Frenzy Express jusqu'au menu). Image borne prévenue (pose à l'accord du propriétaire) |
| 206 | 28/10 | (Image borne) la 20261028-2 posée sur les deux bornes ; les lanceurs passent --write-dir | ✅ noté : posée avec l'accord du propriétaire (zip conforme, .ini gardés, anciens binaires dans _sauvegardes\windowsloader-avant-20261028-2), --list-games donne 42 lignes sur chaque borne. Depuis ce midi, les lanceurs des bornes passent --write-dir (Saves\windowsloader_single en solo, Saves\windowsloader_link_cabN en link) : Frenzy Express sur racecab1 n'a rien écrit dans son dossier, 1 020 fichiers dans le dossier d'écriture, dont le cache OpenGL du pilote NVIDIA (frenzyexpress\appdata\Local\NVIDIA\GLCache) : gardé là sous UWF, il évite de recompiler les shaders à chaque démarrage |
| 207 | 28/10 | Dans la liste des jeux, ajoute une colonne pour les jeux qui gèrent le link et/ou le FFB ; dans le sélecteur, l'indiquer aussi quand on choisit un jeu | ✅ `d0c217a` : la table du sélecteur a deux champs, retour de force et link, à trois valeurs (WL_FEATURE_YES vérifié, PARTLY joué mais pas tout vérifié, NO), d'après BILAN.md : retour de force oui pour Sega Rally 3, Daytona et NSE, Ford Racing, GRID, Wasteland, les trois Wangan, Initial D Zero, Frenzy Express ; en partie pour les autres (sens à confirmer au volant ; Let's Go Safari : vibration). Link : oui pour Mario Kart DX 1.18 (course à deux bornes), en partie pour la 1.10 (course sur ce PC, choix « Deux Joueurs » sur les bornes), non ailleurs (borne seule). --list-games : colonnes FFB et Link (yes, partly, no) ; la fiche du sélecteur : « Retour de force » et « Link » (oui, en partie (voir BILAN.md), non), vus sur Initial D Zero 1.31 et Mario Kart DX 1.10 |
| 208 | 28/10 | Ajoute le support multilingue et traduis toute l'interface en anglais ; puis : gère les langues avec des fichiers .po | ✅ `0ef3f62` : catalogues gettext lus tels quels par le lanceur et la DLL (`src/common/lang.cpp`, sans gettext ni .mo) : le français reste la langue du code, `lang\en.po` donne l'anglais (275 textes : menu Tab, sélecteur, fiches des jeux, options et sorties des modules), `lang\windowsloader.pot` le modèle pour une autre langue. `[Launcher] LANGUAGE` = AUTO (langue de Windows : français, sinon son .po, pt_BR puis pt, sinon anglais), fr, en ou un autre code ; transmis à la DLL (WINDOWSLOADER_LANGUAGE). Choix dans le pied du sélecteur et sur la page À propos du menu : appliqué aussitôt, écrit dans windowsloader.ini. `scripts/lang_update.py` : extraction de `wl_tr("…")` et `WL_N("…")` (comme xgettext), fusion comme msgmerge, `--check`, `--new`, `--unmarked`. Vu en anglais et en français : sélecteur, les sept pages du menu sur Sega Rally 3 et Mario Kart DX 1.18, changement à chaud dans les deux ; aucun texte manquant au journal |
| 209 | 28/10 | Commit, pousse et merge | ✅ `d0c217a` (retour de force et link de --list-games et du sélecteur), `0ef3f62` (langues de l'interface, catalogues .po), puis cette doc ; poussés sur main, la seule branche (rien d'autre à fusionner) |
| 210 | 28/10 | Commit et pousse, fais une release et notifie Image borne pour la mise à jour | ✅ rien à commiter (main déjà poussé en `147f393`) ; release `windowsloader-0.1.0-20261028-3-njz3-windows.zip` (source `147f393`, 3 899 287 octets, SHA-256 216ca344…d74c4a707) : les langues de l'interface et les colonnes retour de force et link ; l'archive a maintenant un dossier `lang\` (`en.po`, `windowsloader.pot`) à côté des 4 programmes. Vérifiée dépaquetée : les 6 fichiers identiques au build, `--list-games` avec FFB et Link, Sega Rally 3 en 0.1.0-20261028-3 avec `--set Launcher.LANGUAGE=en` (275 traductions lues, menu Tab en anglais : Jeu, Options du jeu, À propos ; aucun texte manquant au journal). Image borne notifiée : poser aussi `lang\`, et `[Launcher] LANGUAGE = fr` pour garder le français sur une borne dont Windows n'est pas en français (AUTO y donnerait l'anglais) |
| 211 | 28/10 | (Image borne) la 20261028-3 posée sur les deux bornes | ✅ noté : posée sur racecab1 et racecab2 avec l'accord du propriétaire, `lang\en.po` et `lang\windowsloader.pot` à côté ; les binaires donnés (début du SHA-256 : exe 555B8EE4, exe 64 8AB67039, dll 32 498F6EC7, dll 64 C4636818) sont bien ceux de l'archive. Le propriétaire garde LANGUAGE = AUTO (les Windows des bornes sont en français) : le gabarit `windowsloader.ini.template` de Build-Config a maintenant une section [Launcher] LANGUAGE = AUTO, donc un choix fait dans le sélecteur ou le menu Tab est remis à AUTO au Build-Config suivant. Build-Config a effacé l'ancienne copie `Saves\windowsloader_single\windowsloader.ini` (les lanceurs passent maintenant `--write-dir Saves\windowsloader_<identité>`). `--list-games` donne les colonnes FFB et Link ; aucun jeu relancé, le français d'AUTO pas encore vu à l'écran |
| 212 | 28/10 | Sur la borne, SR3 : le sens de la force est inversé (la jauge de la pause était juste, le volant partait à gauche au lieu de droite) ; « Inverser le sens » le remet bien | ✅ (non commité) l'envoi à SDL était à l'envers pour tous les jeux : la direction d'un effet SDL est celle d'où vient la force, une force positive sur (1, 0) pousse donc vers la gauche (comme linuxloader, `forceFeedback.c`), et windowsloader la croyait vers la droite. `controls.cpp` envoie maintenant la force du jeu telle quelle (> 0 à gauche). Les essais vJoy d'avant contrôlaient la force du jeu, pas ce que SDL en fait. Sur racecab1, la copie `Saves\windowsloader_single\controls.ini` a maintenant `FFB_Invert=1` : à remettre à 0 avec la correction. Vu aussi sur la borne : l'interface est en français avec AUTO (capture du menu) |
| 213 | 28/10 | SR3 très lent par rapport à TeknoParrot, des saccades | 🔄 journal de racecab1 (Image borne, `logs/cabinet-sr3`) : GTX 1050 Ti (écran dessus) et Intel HD 4600 active sans écran ; SR3 en 1920x1080 avec MSAA 2x (son config.ini), BORDERLESS, PRESENT = GDI (par défaut) ; 26 à 33 images/s en jeu (les 150 s du début à 0,3 image/s étaient le menu Tab resté ouvert 147 s). Sur ce PC : 24,8 images/s en GDI (paliers à 29,7 : chaque image rate une composition sur deux), 19 en DIRECT (portable : l'image passe de la NVIDIA à l'Intel, pas représentatif). Échantillonnage : le thread de rendu attend surtout dans le chemin GDI de windowsloader (relecture de l'image, puis DwmFlush). Proposé : essayer `[Display] PRESENT = DIRECT` sur la borne, puis, si c'est fluide, le mettre dans le gabarit de Build-Config et réfléchir à DIRECT par défaut |
| 214 | 28/10 | La touche Échap ne quitte pas le jeu | ✅ (non commité) action `[Common] ExitGame` de linuxloader : `KEY_Escape, GC0_BUTTON_START + GC0_BUTTON_BACK, JOY0_BUTTON_11 + JOY0_BUTTON_10` par défaut, ajoutée aux controls.ini existants ; le jeu s'arrête comme par « Quitter le jeu » du menu (le lanceur ferme ses compagnons), le clavier seulement avec le focus, rien quand le menu Tab est ouvert, seulement un nouvel appui. Vérifié : la ligne ajoutée et lue (3 liaisons) ; l'appui lui-même pas simulé ici (Échap envoyé à la mauvaise fenêtre aurait pu toucher l'application Claude) |
| 215 | 28/10 | Contrôles : un bouton « défaut » par ligne pour remettre la liaison par défaut ; puis : reprends idéalement la même interface que linuxloader (une liste de périphériques, « Lier » ouvre une fenêtre avec chaque commande) | ✅ (non commité) page Contrôles refaite comme celle de linuxloader (`guiControls.cpp`, ses textes français repris) : « Périphériques » (clavier, puis chaque manette avec son préfixe JOY<n> / GC<n>, « (retour de force) » pour celle des effets), « Lier » ouvre la page de liaisons du périphérique sur toute l'image : les actions de [Common] et [Driving] par joueur, les entrées du périphérique, leur état en direct (barre centrée avec la zone morte de sm2-emu, course de pédale, point), « Lier » (l'entrée suivante du périphérique remplace les siennes), « Délier », et en plus de linuxloader « Ajouter » (l'entrée suivante s'ajoute aux siennes : deux touches, ou une touche et un bouton se lient chacun depuis la page de son périphérique) et « Défaut » par ligne (les entrées de ce périphérique comme un controls.ini neuf) ; « Réinitialiser... » pour tout le périphérique. Sous la liste : « Ce que reçoit le jeu » (la valeur que lit sa carte, pour diagnostiquer comme les pédales de SR3) et « Zones mortes et options ». Écrit à chaque changement dans controls.ini, relu aussitôt ; touches capturées non transmises à ImGui. Commande d'agent `menu map N`. Vu en français et en anglais sur SR3 (page clavier) ; la page d'une manette (barres d'axe, pédales) et la capture pas vues ici (pas de vJoy libre) |
| 216 | 28/10 | Passe PRESENT = DIRECT par défaut dans windowsloader, et ne mets GDI que pour tes tests | ✅ (non commité) `display.cpp` : DIRECT par défaut (valeur lue et windowsloader.ini écrit neuf), GDI sur demande ; README. Sur ce PC d'essai, `PRESENT = GDI` dans `bin\Release\windowsloader.ini` (hors git) |
| 217 | 28/10 | linuxloader a-t-il la même inversion des forces ? Demande à sa session de vérifier | ✅ réponse de Linuxloader FFB edition : pas d'inversion chez lui. Son relevé vJoy du 27/10 (même code de signe qu'aujourd'hui) : la trame 84 01 40 (« couple 0x40 vers la gauche ») arrive dans vJoy en direction 8191 (90°) et magnitude +5039 ; malgré cAxes = 1 / rglDirection[0] = 0 dans SDL_dinputhaptic, ses effets STEERING_AXIS sortent tous à 90°, comme notre CARTESIAN (1, 0, 0). Le relevé de la borne (level positif = volant à gauche) colle à sa convention. À confirmer de notre côté : la version corrigée doit donner direction 8191 et magnitude positive dans vJoy pour une force à gauche. Ouvert chez lui : un utilisateur a signalé une inversion sur Initial D (test FORCE FEEDBACK > LEFT de son menu, à faire) |
| 218 | 28/10 | Ajoute un bouton « Ajouter », et demande à linuxloader de faire comme toi, avec aussi « Défaut », « Ajouter » et « Ce que reçoit le jeu » | ✅ (non commité) « Ajouter » sur chaque ligne de la page de liaisons (grisé quand le périphérique n'a rien sur la ligne), vu en français ; demandé à Linuxloader FFB edition : « Ajouter » / « Add », « Défaut » / « Default » (comparer la liste des entrées du périphérique, pas la ligne), « Ce que reçoit le jeu » / « What the game gets » |
| 219 | 28/10 | Commit, pousse et fais une release ; notifie Image borne pour la mise à jour | ✅ `130d92b` (page Contrôles, ExitGame, sens de la force), `6bc8e69` (PRESENT = DIRECT), `a169377` (docs), poussés ; release `windowsloader-0.1.0-20261028-4-njz3-windows.zip` (source `a169377`, 3 921 923 octets, SHA-256 577020c8…aa13649d0), avec `lang\`. Vérifiée dépaquetée : les 6 fichiers identiques au build, `--list-games`, Sega Rally 3 en 0.1.0-20261028-4 sans windowsloader.ini (PRESENT = DIRECT écrit et pris, ExitGame ajouté à controls.ini et lu, 312 traductions, page Contrôles en français). Image borne notifiée : FFB_Invert à remettre à 0 sur racecab1, DIRECT à regarder (SR3, un jeu Direct3D 11), Échap quitte maintenant les jeux |
| 220 | 28/10 | (Image borne) la 20261028-4 posée sur les deux bornes | ✅ noté : posée avec l'accord du propriétaire (zip conforme, la -3 et les anciens controls.ini dans `_sauvegardes\windowsloader-avant-20261028-4`, `lang\` posé). FFB_Invert=0 partout (la copie lue de racecab1 était à 1). ExitGame : le propriétaire veut Échap seul (`ExitGame = KEY_Escape` dans le modèle de Build-Config et la copie lue, sans Start+Back ni boutons vJoy). Le propriétaire avait modifié à la main le controls.ini du lanceur de racecab1 (P1_Start = JOY0_BUTTON_3, Nitro = JOY0_BUTTON_4, Action = JOY0_BUTTON_5, SelectUp/SelectDown + JOY0_BUTTON_9/8), sans effet car windowsloader lit la copie du dossier d'écriture : reportées par Image borne. Remarque d'Image borne : avec un dossier d'écriture, une modification des fichiers du lanceur n'est plus vue une fois la copie faite ; linuxloader avertit (« the one read, is older than… »). Rien relancé : DIRECT, SR3 fluide et le sens de la force restent à voir à la borne |
| 221 | 28/10 | (utilisateur, à la borne, la 20261028-4) Tout est bon pour les 60 images/s et le sens du couple | ✅ noté : Sega Rally 3 à 60 images/s avec PRESENT = DIRECT, et le retour de force dans le bon sens avec FFB_Invert = 0, vus au volant de la borne |
| 222 | 30/10 | Reprends (l'avertissement proposé après la remarque d'Image borne, ligne 220) | ✅ (non commité) avec un dossier d'écriture, le lanceur compare les dates : un windowsloader.ini ou un controls.ini du dossier du lanceur plus récent que sa copie (dont les changements ne sont donc pas lus) est signalé sur la console du lanceur, dans le journal (« settings: …, the one read, is older than … », la phrase de linuxloader) et en orange en haut de la page « Jeu » du menu Tab (traduit). Rien n'est recopié : la copie peut porter ce que le menu a enregistré. `writedir.cpp` (`wl_write_dir_file` rend 1), variable WINDOWSLOADER_OLDER vers la DLL, `wl_settings_older`. Vérifié sur SR3 avec des copies vieillies de deux jours : windowsloader.ini signalé partout (console, journal, menu), controls.ini non (son original du 27/10 est plus ancien que la copie), comme il faut |
| 223 | 30/10 | Quand la redirection d'écriture (WRITE_DIR) est active, l'indiquer dans la fenêtre où est ce message | ✅ (non commité) page « Jeu » du menu Tab, au-dessus de l'avertissement : « Redirection d'écriture active, dossier : WRITE_DIR\<jeu> » ; rien quand elle est inactive (y compris un dossier qui n'a pas pu être créé : le journal le dit), à la demande de l'utilisateur ; traduit (`wl_files_write_state`, `files.cpp`). Vu sur SR3 : active (le dossier affiché) et inactive (rien) |
| 225 | 30/10 | (Image borne) le gel de Wangan 6 reproduit sur racecab2 avec la 20261028-4 (lancé comme par AttractPlus, attract, PRESENT = DIRECT) : plus d'image à 16,5 s, le jeu vivant mais inactif ; le rapport de gel l'a pris | ✅ (non commité) cause trouvée dans les piles (`logs/cabinet-wmmt6-gel`, désassemblage de la DLL de la -4) : à 16,28 s le jeu initialise le lecteur de cartes (BngRwInit, BngRwAttach 0 et 1) sur son thread principal, puis une requête LED/bip/action (+0x8cffa6) ; windowsloader appelait le rappel du jeu (+0x8cf780) aussitôt, dans la requête, et ce rappel attend un verrou que le jeu tient autour de la requête (+0xf1bbf, WaitForSingleObject) : interblocage, même pile dans les rapports à 5, 15, 25 et 35 s. Les threads « dans windowsloader64.dll+0x40ef4 » sont ceux de boost::asio au repos dans le GetQueuedCompletionStatus du terminal (sans rapport). Correction (`namco.cpp`) : les rappels de bngrw.dll partent d'un thread de la bibliothèque, après la requête, comme la vraie bngrw.dll (file de 64, ordre gardé) ; vaut aussi pour Mario Kart DX 1.18. Vérifié ici : Mario Kart DX 1.18 attache le lecteur (66,6 s) et arrive à l'attract avec les rappels décalés ; Wangan 6 100 s sans gel, mais ce PC n'initialise pas le lecteur (pas de BngRwInit : réglage de la borne ?), donc le cas de racecab2 reste à revoir sur la borne. Vu en passant sur Mario Kart DX : bandeau rouge « この基板をネットワークに接続してください まもなくゲームの稼動が停止します » (réseau ALL.Net, comme celui caché pour Wangan 6 depuis la 20261026-1), non traité |
| 226 | 30/10 | (Image borne) Wangan 6 rendu à TeknoParrot sur les deux bornes | ✅ noté : décision du propriétaire ce matin ; retiré de la liste Windows Loader, WMMT6 remis dans TeknoParrot.txt. Wangan 6 reviendra à windowsloader quand le gel (225) sera corrigé et vérifié sur racecab2 |
| 227 | 30/10 | Commit, pousse et fais une release, notifie Image borne ; puis : fais un pull, merge avec upstream | ✅ le push refusé : origin/main avait 3 commits du 29/10 faits ailleurs (`bd6428c` fenêtre du jeu au premier plan, `a1d80fd` serveur des sorties réseau démarré et arrêté depuis le menu, `9db8da6` vraie carte IO4 en USB) ; mes 3 commits rejoués dessus (rebase : seuls lang/en.po et lang/windowsloader.pot en conflit, régénérés par lang_update.py, 320 textes traduits), compilés, poussés : `11a3124` (rappels du lecteur de cartes), `95836cf` (dossier d'écriture), `14c7036` (docs). Release `windowsloader-0.1.0-20261030-1-njz3-windows.zip` (source `14c7036`, 3 934 861 octets, SHA-256 12f77d76…c778bb3b), avec `lang\`. Vérifiée dépaquetée : les 6 fichiers identiques au build, `--list-games`, Sega Rally 3 (32 bits) en 0.1.0-20261030-1 avec « Redirection d'écriture active » sur la page Jeu, Wangan 6 (64 bits) 40 s sans gel. Image borne notifiée : poser, puis rejouer l'attract de Wangan 6 sur racecab2 lancé à la main (2 à 3 min) |
| 228 | 30/10 | (Image borne) pourquoi Wangan 6 initialise le lecteur sur racecab2 : son `TP\setting.lua.gz` (27/10 14:11) a mIcCardRw = 1, mIcCardVender = 1, mFreePlay = 0, mForceFeedback = 1, mTouchPanel = 1 ; les modèles de racecab1 et la copie du PC d'essai ont mIcCardRw = 0, mIcCardVender = 0 | ✅ gel reproduit et correction vérifiée sur le PC d'essai, le setting.lua.gz modifié posé seulement dans un dossier d'écriture de test (script `wmmt6_cardrw.py` du scratchpad ; l'original de games/wmmt6 intact) : 20261028-4 → lecteur initialisé à 11,3 s, image bloquée sur la 242e, même pile qu'à racecab2 ; 20261030-1 → plus de gel, mais avec mIcCardVender = 1 erreur E2301 « カードディスペンサー接続エラー » au démarrage (pas de distributeur de cartes joué) ; 20261030-1 avec les réglages de racecab2 sauf mIcCardVender = 0 → attract 90 s, lecteur attaché, « BANAPASSPORT CARDs cannot currently be used », FREE PLAY. Dit à Image borne : mIcCardVender = 0 pour Wangan 6 sous windowsloader (aligner racecab2 sur racecab1, décision du propriétaire) |
| 229 | 30/10 | Demande à Image borne d'aligner racecab2 sur racecab1 ; peut-on prévoir d'émuler un distributeur de cartes ? ; d'accord, on garde mIcCardVender = 0 | ✅ alignement du `TP\setting.lua.gz` de Wangan 6 demandé à Image borne (sauvegarde de l'actuel ; à vérifier : calibrage des pédales de racecab2, et mForceFeedback, STEERING POWER, qui coupe le moteur du volant à 0). Distributeur de cartes : non. Avec mIcCardVender = 1 le jeu attache un 2e lecteur par bngrw.dll (`BngRwAttach 1 "COM2"`), déjà joué, et lève pourtant E2301 : il attend autre chose du mécanisme, à retrouver. Sans intérêt ici (il ne vend que des cartes Banapass vierges, inutilisables sans vrai lecteur ni serveur, et les bornes n'en ont pas) : mIcCardVender = 0 gardé, décision de l'utilisateur. Puis : « garde mForceFeedback = 1 sur racecab2 » (STEERING POWER sur ON), transmis à Image borne : tout comme racecab1 sauf mForceFeedback = 1 |
| 231 | 30/10 | (Image borne) Wangan 6 sur racecab2 avec la 20261030-1, attract, 150 s, deux essais (`logs/cabinet-wmmt6-1030`) | ✅ gel corrigé confirmé sur la borne : essai 2 (copie du dossier d'écriture à mIcCardRw = 1, mIcCardVender = 0, mFreePlay = 0), BngRwInit à 13,7 s, aucun « freeze: », 60 images/s. Essai 1 : E2301 encore, parce que le jeu lisait la copie de Saves\…\wmmt6\…\TP\setting.lua.gz (mIcCardVender = 1) et non le fichier modifié en place : le même piège de copie unique, pour les fichiers du jeu (l'avertissement de la 20261030-1 ne couvre que windowsloader.ini et controls.ini). E0516 « ALL.Net AUTH ERROR » à 33,5 s : attendue (pas de serveur ALL.Net), aussi sur le PC d'essai vers 34-37 s. « gzip error » au démarrage : aussi ici, sans effet. 🔄 E0505 « NETWORK DISCONNECT » à 32,5 s, seulement sur racecab2 : ici le jeu s'adresse au terminal à 30,7 s (getaddrinfo 192.168.92.20:50765), là-bas jamais ; le jeu continue (60 images/s). Cause à trouver ; demandé à Image borne si quelque chose s'affichait |
| 232 | 30/10 | (Image borne) le réglage de Wangan 6 de racecab2 aligné sur racecab1, mForceFeedback = 1 | ✅ noté : le TP\setting.lua.gz des deux bornes est un lien physique que le lanceur TeknoParrot refait à chaque lancement depuis setting.lua_single / _link_cab1 / _link_cab2.gz : les trois modèles changés, sur les deux bornes (choix du propriétaire, même image) ; valeurs R1 = R2 : mIcCardRw = 0, mIcCardVender = 0, mFreePlay = 1, mTouchPanel = 0, mForceFeedback = 1 ; sauvegardes dans `C:\Racecab\_sauvegardes\wmmt6-modeles-avant-ffb-20261030` et `wmmt6-writedir-setting-20261030` (l'ancien de R2) ; copies du dossier d'écriture retirées (le prochain lancement windowsloader recopie le fichier en place). Calibrage 32512 / 0 / 0 (R2 avait 32768 / 20480 / 28672) : à confirmer dans le menu de test de R2, pédales au repos. Avec mIcCardRw = 0, le lecteur n'est plus initialisé sur les bornes |
| 233 | 30/10 | Demande à Image borne de relancer Wangan 6 sur racecab2 avec une capture | ✅ (`logs/cabinet-wmmt6-e0505`) avec les réglages alignés (mIcCardRw = 0, mIcCardVender = 0, mFreePlay = 1, mForceFeedback = 1), la 20261030-1, attract, 68 s : plus d'E0505, le terminal trouvé à 29,47 s (getaddrinfo 192.168.92.20:50765), E0516 seule (attendue), pas de gel ; à 25 s le logo Bandai Namco, à 40 s l'attract « BANAPASSPORT CARDs cannot currently be used », FREE PLAY, à 60 s une course de démonstration 4/4, aucune boîte d'erreur. L'E0505 du matin venait donc de l'ancien réglage de racecab2, mais pas de mFreePlay = 0 seul (comme le pensait Image borne) : sur le PC d'essai, mFreePlay = 0 avec le lecteur actif trouvait le terminal à 30,7 s sans E0505 ; une autre valeur de l'ancien fichier (gardé dans `_sauvegardes\wmmt6-writedir-setting-20261030`, 164ACA03). Rien à corriger tant que les bornes gardent ce réglage |
| 236 | 30/10 | Efface la note sur Storm Rider ; retire aussi la mention de la fiche du sélecteur ; commit et pousse | ✅ ses lignes retirées des notes ; fiche du sélecteur « Moto. Carte par motobikeio.dll ; non pris en charge. », `lang/en.po` et `lang/windowsloader.pot` régénérés (320 textes), le commentaire de `wahlap.cpp` raccourci ; compilé en 32 et 64 bits, poussé |
| 237 | 30/10 | Fais un build release | ✅ `windowsloader-0.1.0-20261030-2-njz3-windows.zip` (2e du jour, `-DWL_BUILD_NUMBER=2` puis remis à vide, source `2dbe798`, 3 935 087 octets, SHA-256 2205ffb5…3466a99) dans `emulateurs\releases`, avec `lang\` : seul changement la fiche de Storm Rider du sélecteur ; RELEASE.txt : la -1 confirmée sur racecab2 pour le gel de Wangan 6. Vérifiée dépaquetée : les 6 fichiers identiques au build, `--list-games` (42 lignes), Sega Rally 3 (32 bits) 30 s en 0.1.0-20261030-2 avec dossier d'écriture (320 traductions lues). Image borne pas prévenue (pas demandé) |
| 238 | 30/10 | (Émulateur Hikaru) Décrire la page Contrôles pour la reprendre dans Hikaru Loader | ✅ décrite à la session Hikaru : disposition (Périphériques, « Lier » vers la page plein écran de liaisons, Ce que reçoit le jeu, Zones mortes et options), capture d'une entrée par changement depuis l'état de départ, SDL3 seul, controls.ini de linuxloader, fichiers et fonctions de `menu.cpp` et `controls.cpp`, pièges ; deux captures fraîches de Sega Rally 3 en mode agent |
| 239 | 30/10 | Pourquoi le premier bouton de vJoy (1) apparaît-il comme bouton 0 ? Puis : une option de l'émulateur pour afficher les boutons avec +1, +1 par défaut, controls.ini gardant les numéros de SDL ; passer l'information à linuxloader, Hikaru et Supermodel | ✅ (non commité) SDL compte les boutons à partir de 0 (`JOY0_BUTTON_0`), Windows et vJoy à partir de 1, et la page affichait le numéro de SDL tel quel (linuxloader aussi). `[Input] BUTTONS_FROM_ONE` de windowsloader.ini (section `[Input]` de linuxloader), `true` par défaut : « Bouton %d » reçoit le numéro + 1 à l'affichage seulement (controls.ini, capture, lecture inchangés ; chapeaux et axes pas touchés) ; case « Numéroter les boutons à partir de 1 (comme Windows et vJoy) » sous la liste des périphériques, écrite aussitôt dans le fichier de réglages lu ; fichier par défaut, README, `lang/en.po`. Vérifié avec vJoy (`--controllers`, vJoy annoncé puis rendu à Linuxloader et MAME FFB edition) : ExitGame `JOY0_BUTTON_11 + JOY0_BUTTON_10` affiché « Bouton 12 + Bouton 11 », « Bouton 11 + Bouton 10 » avec `false`. Transmis à Linuxloader FFB edition et à Émulateur Hikaru (même nom, même défaut) ; pas de session Supermodel ouverte, rien transmis. Linuxloader l'a repris (non commité chez elle) : même clé, même défaut, case et infobulle ; compilé, `--create config` l'écrit ; sa page pas encore vue à l'écran. Supermodel (prévenu ensuite) : rien à faire, il numérote déjà les boutons et les joysticks à partir de 1, dans Supermodel.ini (`JOY1_BUTTON1` = bouton 0 de SDL) comme dans son menu ; un `--set Input.BUTTONS_FROM_ONE=` y est ignoré avec une ligne au journal |
| 240 | 30/10 | Des commentaires pour chaque paramètre des .ini et leur valeur par défaut ; les ajouter automatiquement au fichier s'ils n'y sont pas | ✅ (non commité) windowsloader.ini et controls.ini neufs : un commentaire au-dessus de chaque clé, finissant par « Default: … », plus un en-tête (controls.ini : toute la syntaxe des liaisons, les touches, les noms de manette, les suffixes). Ajoutées au fichier neuf : `[Emulation] EMULATE_JVS` / `EMULATE_DRIVEBOARD` et `[Paths] JVS_PATH` (SERIAL_<n>_PATH, `[Game.<nom>]` et `[ForceFeedback] FFB_DEVICE` décrits en commentaire) ; aucun `=` dans un commentaire (Windows lirait une clé : « (PRESENT = GDI only) » corrigé). `wl_ini_merge` (`settings.cpp`) : un fichier existant (windowsloader.ini, le <nom>.ini d'un jeu, controls.ini ; pas un `-c` d'un autre nom) reçoit à chaque lancement les sections, clés et commentaires qui lui manquent, ses valeurs et son ordre gardés, écrit à côté puis remis en place, rien d'écrit s'il ne manque rien. Essayé sur les copies des .ini de ce PC (Sega Rally 3, dossier d'écriture) : windowsloader.ini 15 clés et l'en-tête ajoutés, controls.ini 46 commentaires, aucune valeur changée (PRESENT = GDI gardé) ; second lancement : rien écrit |
| 241 | 30/10 | Nom court de Daytona Championship USA : « daytona3 » au lieu de « daytona », qui perturbe la détection automatique de BackForceFeeder ; demander à Image borne le nom le plus adapté | ✅ rien renommé, sur l'avis d'Image borne : le nom court de windowsloader n'entre pas dans la détection de BFF. Pour les jeux de windowsloader et linuxloader, les profils BFF visent la console du lanceur (processus « conhost|cmd », titre « *[rom]* »), que RunWindowsLoaderGame.cmd intitule « [Daytona3] » ou « [Daytona3NSE] » (le nom de la liste AttractPlus) ; aucun profil ne vise Daytona.exe, et le seul en « *[daytona]* » (MAME-Daytona USA) exige le processus mame. La cause : sur racecab1 le profil « TP-Daytona USA 3 ChampionShip » filtre « *[Daytona3USA* », qui ne reconnaît aucune des deux éditions (Daytona sous le profil Default) ; racecab2 garde l'ancien « *Daytona3* » (47 profils sans [ ] là-bas). Corrigé côté BFF par Image borne, qui essaie les 72 entrées des deux loaders sur racecab1 |
| 242 | 30/10 | Changer quand même le nom en daytona3, aussi dans les clés de configuration ; la NSE en daytona3nse ; tout en minuscules ; puis : ne pas reprendre l'ancien nom, aucune référence à daytona dans le code ni d'importation de paramètres (code en développement) | ✅ noms courts `daytona3` et `daytona3nse` (le module les donne selon la version d'Install.ini) : titre « [daytona3] », `--list-games`, sélecteur, `[Game.daytona3]` / `[Game.daytona3nse]`, `daytona3.ini` / `daytona3nse.ini`, `WRITE_DIR\daytona3` / `daytona3nse`, sorties réseau (`mame_start`). Aucune reprise de « daytona » : une reprise écrite d'abord (options, daytona.ini, copie du dossier d'écriture), vérifiée avec un faux Daytona.exe, puis retirée à la demande de l'utilisateur. Sur les bornes, `[Game.daytona]`, `daytona.ini` et `Saves\…\daytona` ne sont plus lus |
| 243 | 30/10 | Oscillation du volant sur Daytona 3 (va-et-vient lent, ~1 Hz, de −90° à +90°, pas sur Sega Rally 3) ; le jeu n'a-t-il que 4 bits + 1 de sens ? Voir le FFB Arcade Plugin ; décompiler le calcul de la force ; jouer la vraie commande ; ajouter la commande et les lampes aux sorties réseau ; d'où vient la courbe de centrage, se règle-t-elle au menu de service ? | ✅ (non commité, pas essayé à la borne) décompilé (capstone) sur les deux Daytona.exe des bornes, copiés par Image borne (CRC 08823BEF, 9650CBB4) : aucune force plus fine que 15 niveaux. Le jeu remplit les sorties d'une borne Model 2 (+0x1de100 en 2017) : lampes et commande de la carte moteur de Daytona USA (SailorSat : 1x moteur coupé, 2x maintien, 3x centrage, 4x décentrage, 5x/6x roulis) ; `src\soc\forcefeedback.cpp` (+0x128c50) la traduit en octet du port avec la position du volant, selon `GameData\config.ini [Wheel]` (PERIOD1..3, FORCE1/FORCE2/FORCE_MIN/GRASS/GARIGARI_FORCE en jeux _W/_N/_S, VIB_FREQUENCY, VIB_FORCE) et `ShellData.ini [CalibrationData] FeedbackStrength` (1 W, 2 N, 3 S). Copie de la borne : FeedbackStrength 2, FORCE2_N 1, PERIOD1 0.1375 : le centrage atteint 0,86 en 14 % de la course, en forces constantes une image en retard : l'oscillation. Le cas 1x fait un créneau gauche/droite de 20 appels. Le plugin lit un autre octet (+0x131FEC9, codes 8x à Fx), sans référence dans le code : pas repris. Fait d'abord : la commande traduite en effets SDL (ressort, roulis, vibration), remplacé par la 244. Le menu de test de Daytona est celui de Shell.exe, non émulé : FeedbackStrength et `[Wheel]` se règlent dans les fichiers. Script de mesure `scripts/agent/day_settle.py` |
| 244 | 30/10 | Demander à sm2-emu comment il émule la carte de Daytona (quasi parfaite, jusqu'au panel spring) et son gain au-delà de 100 % ; ne pas retirer la couche SDL générique (valable pour les autres jeux : Sega Rally 3 marche très bien) | ✅ (non commité, pas essayé à la borne) réponse de sm2-emu : aucun effet condition, la carte simulée (son programme, EPROM EPR-16488A) et une seule force constante par image calculée avec la position du volant, plus le ressort mécanique du panneau (0 sur ±1500, plein à 15500, 20 % du couple 32/63 par défaut ; il donne aussi le « breakaway » du calibrage de la carte). Gain `wheel_ffb_strength` 0 à 200 % sur le couple, linéaire puis écrêté à ±32767. Réécrit pour windowsloader d'après sa description (pas de copie : conditions supplémentaires de sa licence) dans `daytona.cpp` : 0x on/off, 1x rien, 20-27 frein visqueux, 30-37 et 38-3F ressorts de centrage (zones mortes 2 et 7, plein 0x15 et 0x18, maintien au centre, ressorts lâchés après 10 oscillations rapides), 40-47 décentrage, 50-57 / 60-67 poussées, 7x boost ; puissance en rampe de 1 par ms, au plus 63 ; course inversée suivie ; options `DRIVE_DIP` (0-7) et `PANEL_SPRING` (0-100 %). Le gain reste `FFB_Strength` de controls.ini (multiplie la force puis écrête à ±1, déjà au-delà de 100 % possible). La couche SDL générique gardée : force constante inchangée pour tous les jeux, ressort et vibration (`wl_ffb_set_effects`, créés au premier usage) disponibles, aucun jeu ne les demande aujourd'hui. Relu ensuite : la carte démarre forces en marche (la copie de la commande, lue une fois par image, peut manquer un 0x0A), la puissance repart de 0 seulement pour une poussée qui change de sens, la force n'est envoyée qu'à 256/32767 de changement ou au passage par 0. Essayé hors jeu : les fonctions de la carte, extraites telles quelles de `daytona.cpp`, sur un volant simulé (inertie, frottement léger) lâché à 60 % : le centrage du jeu par l'octet du port (courbe et réglages de la borne, 15 niveaux, une image de retard) le fait aller d'une butée à l'autre sans fin dès un couple fort, le symptôme de la borne ; la carte simulée le laisse à ±0,1, autant que moteur coupé (le ressort du panneau sur ce volant presque sans frottement) ; 5x pousse à gauche, 6x à droite, 4x vers l'extérieur |
| 245 | 30/10 | Regarde pour le link de Daytona 3 directement sur les bornes : apparemment ça ne marche pas | 🔄 (non commité, pas essayé sur les bornes) cause : windowsloader le voulait seul, `NumCabinets` et `CabinetID` forcés à 1 et le réseau local refusé (`connect 192.168.1.1..8:26000 refused` dans le journal de racecab2). Protocole lu dans le jeu (`ping.cpp`, battement de cœur, RankComm TCP 5888, picomm UDP sur 192.168.1.255, ImageComm pour les photos) : tout en 192.168.1.x, la borne 1 maître. Link écrit comme celui de Mario Kart DX (`[Game.daytona3] LINK`, `CABINET`, la borne N en 192.168.1.N, les adresses traduites dans la table d'importation), essayé sur ce PC avec un faux Daytona.exe. Relevés d'Image borne (lecture seule) : racecab1 192.168.1.4, racecab2 192.168.1.5 (plus TeamViewer en 7.x/8), pare-feu désactivé ; ShellData.ini, `shell\Game.ini` et `GameData\config.ini` (NETWORK_TYPE) échangés par liens physiques par les lanceurs TeknoParrot (solo, link 1, link 2), en place : link ; d'après les notes du propriétaire, le link sous TeknoParrot voulait une webcam. Liste des jeux : link « en partie » pour les deux éditions |
| 246 | 30/10 | Ajoute la molette de souris pour faire défiler les fenêtres dans ImGui, par exemple les contrôles | ✅ (non commité) le menu Tab la traitait déjà dans la procédure de fenêtre, mais tant que le menu est ouvert la boucle du jeu ne tourne plus et WM_MOUSEWHEEL reste dans sa file. Désormais un hook souris bas niveau (WH_MOUSE_LL) sur un fil de windowsloader lit la molette quand une fenêtre du jeu est au premier plan et le menu ouvert, la garde du jeu et la donne à ImGui. Vérifié sur Sega Rally 3 (mode agent, six crans envoyés par SendInput) : la page Contrôles descend jusqu'en bas, trois crans la remontent en haut. Le sélecteur de jeux (sa propre boucle) l'avait déjà |
| 247 | 30/10 | Commit et pousse ; notifier Image borne et lui demander d'essayer le link | ✅ poussé (`1edab58` molette, `374cd99` link de Daytona, `a4aa07e` NOTES) ; binaires d'essai (pas une release) dans le scratchpad, `windowsloader-0.1.0-20261030-njz3-essai-link.zip`, posés par Image borne avec l'accord du propriétaire. Résultat : découverte réussie, « POWER-ON TEST NETWORK » LINK ID 1 et 2 [GOOD], TOTAL MACHINE 2, puis chaque borne ne voit plus que elle-même ; « CoCreateInstance(SystemDeviceEnum) refused: no camera » sur les deux, `GameSettings.ini` TOTAL CABINETS = 1. Le propriétaire : même blocage sous TeknoParrot sans webcam ; les deux bornes en ont une. Corrigé : `[Emulation] CAMERA` (faux par défaut) et le jeu en link reçoit la caméra d'office (`wl_system_allow_camera`), TOTAL CABINETS suit LINK, compteurs du trafic link toutes les 10 s et fins des connexions TCP au journal (le journal s'arrêtait à 10 envois par borne) ; refait sur ce PC avec le faux jeu |
| 248 | 30/10 | (Image borne) essai n° 2 du link de Daytona (caméra donnée) : même blocage sur POWER-ON TEST NETWORK ; les deux journaux copiés dans games/daytona3/linklogs, aucun Log_DT écrit par le jeu | 🔄 lu : caméra donnée sur les deux (OpenCV m_bEnableCapture = 1, images en diffusion UDP vers 192.168.1.255:3167 + numéro de borne, ~19 par s, que personne ne reçoit). La borne 1 charge bien plus lentement (25 à 40 images/s, figée 5,8 s dans le pilote NVIDIA ; la borne 2 à 60) : la borne 2 attend le battement de cœur TCP 25000 dès 40 s (5 essais refusés), la borne 1 seulement à 57 s ; reliées à 57 s, la borne 1 n'envoie presque rien (104 octets en 10 s), chacune coupe vers 70 s (recv 10060, send 10053) sans jamais rouvrir : l'autre borne « [      ] ». Ensuite les données de course passent dans les deux sens (~60 paquets/s de 496 octets), la traduction des adresses tient. Les messages du jeu (OutputDebugString) étaient perdus : les 2000 lignes prises par ses chargements `.nrfc`, désormais écartés (`wl_system_quiet_debug`). Essai n° 3 demandé avec la borne 1 lancée ~20 s avant la 2 |
| 249 | 30/10 | (Image borne) essai n° 3 du link de Daytona, la borne 1 lancée 20 s avant : test réseau passé et « LINK SYSTEM JUSQU'À 2 COUREURS » sur les deux, mais chaque borne repart seule au salon d'attente ; journaux dans games/daytona3/linklogs/cab*-essai3 | ✅ (non essayé sur les bornes) lu : « commLink OK » des deux côtés, ~60 paquets/s dans chaque sens pendant une minute ; puis la borne 1 sans image 32 s (puis 15 et 13 s ; 28 s dans l'essai a), son fil principal dans DefWindowProc → uxtheme → la boucle modale de Windows ; la borne 2 déclare « nodeID0:0 is timeout », « Network Disconnection », la 1 aussi en entrant au salon. Seule la borne 1 est en `DISPLAY_MODE = WINDOW` (barre de titre), la 2 en BORDERLESS, mêmes PC (GTX 1050 Ti). Cause : un appui sur Alt ouvre le menu de la fenêtre (SC_KEYMENU) et Windows tient le fil du jeu jusqu'à la touche ou au clic suivant. Corrigé dans `display.cpp` : SC_KEYMENU n'est plus passé au jeu, les boucles de menu et de déplacement sont notées au journal ; vérifié sur Sega Rally 3 en WINDOW, Alt appuyé sur sa fenêtre : « the window's menu (Alt) not opened », 29 images/s sans gel. Les autres gels (6 s, D3DCOMPILER) sont la compilation des shaders au premier chargement |
| 250 | 30/10 | Essaie également le LINK en bloquant la webcam, comme s'il n'y en avait pas de branchée : c'est contraignant d'avoir une webcam ; (Image borne) l'Alt de la borne 1 venait de son outil de test (vjoydffb, « focus » tapait Alt avant SetForegroundWindow), corrigé de son côté | 🔄 `[Emulation] CAMERA` à trois valeurs : AUTO (défaut, caméra donnée seulement si le module la demande : le link de Daytona), true, false (jamais, même en link). Les fichiers des bornes ont reçu `CAMERA = false` par la fusion des défauts de la version précédente : il vaut désormais « jamais », d'où `--set Emulation.CAMERA=AUTO` ou `false` explicite pour les essais. Vérifié sur ce PC avec le faux Daytona.exe (AUTO et true : donnée ; false : « no camera for the game, although… »). Le premier essai sur les bornes, sans caméra, avait aussi la borne 1 en retard : à refaire. Essai demandé à Image borne, avec et sans caméra |
| 251 | 31/10 | (Image borne) essai n° 5 : course à deux réussie avec les webcams (A), link tombé sans caméra (B) ; le propriétaire demande une webcam émulée (image noire), puis une option par jeu dans « Options du jeu » : Auto (la webcam si présente, sinon l'émulée), Image fixe (un JPG ou PNG, noire si vide), puis chaque périphérique de capture du PC par son nom ; commune à tous les jeux à caméra (Mario Kart DX par exemple), chaque jeu son choix | 🔄 (non essayé sur les bornes) lu : avec ou sans caméra, la borne 1 attend une connexion TCP juste après « commLink OK » ; avec la caméra les photos arrivent, sans, elle ferme au bout de 11 attentes et le test réseau reste bloqué. Le jeu ouvre sa caméra par `cv::VideoCapture(700)` d'OpenCV 2.4 (+0x16ab3b ; constructeur +0x642df0, NSE +0x6484f0), puis un fil prend `cap >> frame` (+0x16bf50). Fait : `camera.cpp` (options `CAMERA` = AUTO / PICTURE / nom d'une caméra, liste faite à l'affichage par l'énumérateur DirectShow ; `CAMERA_PICTURE` lu et mis à la taille par WIC ; type d'option texte et liste dynamique dans le menu, valeurs jusqu'à 260 caractères) ; Daytona : le constructeur remplacé, la caméra du PC demandée (son rang) ou un objet de windowsloader, ouvert, dont les images pointent sur l'image fixe (sans compteur de références). `[Emulation] CAMERA` : AUTO laisse faire l'option du jeu, false refuse les caméras du PC (l'émulée alors). Vérifié hors jeu : l'objet appelé comme le jeu (set, isOpened, >>, read, get ; Mat 320x240, somme 0), les deux caméras de ce PC listées, un PNG relu en BGR, un chemin manquant noir. Mario Kart DX (Media Foundation) n'a pas encore de caméra émulée |
| 252 | 31/10 | Oui, la caméra émulée de Mario Kart DX | ✅ (non commité au moment de l'essai) le jeu importe MFEnumDeviceSources (MF.dll), MFCreateSourceReaderFromMediaSource (MFReadWrite.dll), MFCopyImage ; il prend la première caméra, lit le premier flux vidéo (sous-types RGB32, RGB24, YUY2, NV12 ; taille et pas du type natif), en asynchrone (son OnReadSample : GetBufferByIndex(0), Lock, copie, puis ReadSample) ; il n'ouvre la caméra que si ゲーム設定 → ナムカム（カメラ）の使用 est ON (OFF dans la copie : « 現在、カメラは使用できません »). Fait : `mfcamera.cpp`, commun aux jeux Media Foundation : la caméra choisie en tête de liste, ou un IMFActivate / IMFMediaSource / IMFSourceReader de windowsloader (RGB32 640x480, 30 images/s, l'image fixe de camera.cpp, échantillons et tampons de Media Foundation) ; `[Game.mkdx] CAMERA`, `CAMERA_PICTURE` ; `wl_system_camera_allowed` pour que les appels de windowsloader respectent `[Emulation] CAMERA`. Essai sur ce PC (1.18, NamCam mise à ON par le menu de test en mode agent, dans le dossier d'écriture de l'essai) : l'écran « しゃしんさつえい » montre l'image, mais rouge et bleu inversés : le chemin RGB32 du jeu lit R, G, B ; corrigé (`rgbOrder`), revu juste. La caméra réelle pas essayée ici (ce serait la webcam de ce PC) |
| 253 | 31/10 | Peux-tu essayer d'ajouter le mode link à Ford Racing, GTI Club, DiRT ? ; puis : seulement les exécutables, essais sur les bornes par Image borne avec des journaux détaillés (Ford Racing copié en entier par le propriétaire) | 🔄 (pas essayé sur les bornes) lu : les trois trouvent leurs bornes par diffusion puis se parlent à leurs adresses : Ford Racing, protocole Sega (`net_sega.c`), sa carte par SIO_GET_INTERFACE_LIST, ports 53001 à 53003, `CabinetPostion` ; GTI Club (Konami, e-Amusement), 255.255.255.255, salon, GetAdaptersInfo, CABINET ID du menu de test ; DiRT Showdown, « linked_group_mode », sessions, GetAdaptersAddresses. Fait : `link.cpp` commun, actif seulement avec `[Game.<nom>] LINK` (sinon les sockets restent à system.cpp, hors du réseau local) : diffusions envoyées à chaque PC de LINK, autres adresses refusées, datagrammes d'ailleurs pas entendus, seule la carte de LINK montrée (le VPN de TeamViewer écarté) ; journal détaillé (bind, listen, connect, accept, 25 premiers paquets par PC et sens avec ports, taille et premiers octets, trafic toutes les 10 s, fins de TCP). Ford : CabinetPostion d'après CABINET ; Showdown : NumCabinets et CabinetID ; GTI : CABINET ID au menu de test. Essai de Ford Racing seul sur ce PC : carte filtrée, diffusions dupliquées vers les PC de LINK et reçues |
| 254 | 31/10 | (Image borne) caméra de Mario Kart DX 1.18 sur racecab2 (NamCam mise à ON avec l'accord du propriétaire) : l'émulée marche (contrôle de démarrage passé, しゃしんさつえい avec l'image, 100 images) ; la vraie DV20 en AUTO : E08-01 « ナムカム（カメラ）が見つかりません », la caméra listée à chaque image | 🔄 (non essayé sur la borne) le jeu coupe les convertisseurs de Media Foundation (MF_READWRITE_DISABLE_CONVERTERS) et ne prend que le premier format natif de la caméra (RGB32, RGB24, YUY2 ou NV12) : une webcam dont le premier est MJPG n'est pas reconnue. Désormais, pour une caméra du PC, les convertisseurs sont rendus, un lecteur intermédiaire choisit un format que le jeu connaît (le sien, YUY2 640x480 d'abord ; sinon décodé en YUY2, NV12 ou RGB32), le donne comme premier format natif avec son pas, et note au journal les formats de la caméra ; l'émulée revérifiée ici. La caméra réelle pas essayée ici (celle de l'utilisateur) |
| 255 | 31/10 | (Image borne, pour le propriétaire) la webcam des bornes, « DV20 USB CAMERA » (USB VID_4C4A PID_4A55, UVC, pilote usbvideo) : Mario Kart DX doit l'accepter (le plan de camera3, formats natifs et format choisi au journal) ; Daytona 3 : avec la DV20 les cases photo restaient noires, noter ses images (ouverte ou non, taille et format, luminosité des premières, si elles continuent), qu'elles arrivent décodées ; les deux jeux : en AUTO, si la DV20 ne s'ouvre pas ou ne donne pas d'image, la caméra émulée | 🔄 (pas essayé sur les bornes) Daytona : l'objet `cv::VideoCapture` de la vraie caméra garde ses fonctions, sauf grab, retrieve, >> et read, surveillées (seuls les appels du jeu : >> appelle read, qui appelle grab et retrieve) : 5 premières images au journal (taille, canaux, profondeur, octet moyen), puis le compte toutes les 10 s ; aucune image pendant 3 s, ou 100 premières toutes noires (octet moyen < 1) : l'image fixe à la place. OpenCV 2.4 passe par DirectShow, qui décode le MJPG de lui-même. Vérifié hors jeu avec une fausse caméra (éclairée : ses 160 images ; noire : émulée après 100 ; sans image : émulée après 3 s). Mario Kart DX : la caméra essayée une fois avant d'être listée au jeu (une seconde énumération, un lecteur synchrone au format choisi pour le jeu, une image lue avec sa luminosité ; arrêtée ensuite) ; pas d'image en 5 s ou erreur : l'émulée. L'émulée revérifiée ici en PICTURE ; la vraie caméra pas essayée ici |
| 256 | 31/10 | (Image borne) essai de camera4 avec les DV20, AUTO, salle des bornes éteinte : Mario Kart DX 1.18 (racecab2) : formats natifs 0 à 12 (NV12 / MJPG 1280x720, 640x480, 480x320, 720x480, 1920x1080 ; YUY2 640x480, 320x240), « read as YUY2 640x480 », première image luma 0.3, plus de E08-01, la vue de la caméra (noire) sur しゃしんさつえい ; Daytona 3 en link : images 320x240, 3 canaux 8 bits, octet moyen 0.0, « 100 black frames » puis l'émulée (noire aussi), course à deux réussie ; remarque : dans une borne sombre au démarrage, le joueur ne verrait jamais son visage | ✅ les images noires de Daytona ne remplacent plus la caméra (l'émulée sans image fixe est noire elle aussi) : elles sont gardées, le journal note le passage au noir ou à l'éclairé (20 fois au plus) ; seul le manque d'image pendant 3 s donne l'émulée. Vérifié hors jeu (fausse caméra noire puis éclairée : ses 160 images, « frame 81 lit ») |
| 257 | 31/10 | Quels autres jeux peuvent avoir le mode link et ne sont pas supportés par windowsloader ? | ✅ réponse dans la conversation : les plus proches du link écrit (réseau local sans serveur) : Sega Rally 3 (salon et places dans l'exécutable), Wasteland Racers 2071 (élection d'un serveur par diffusion sur UDP 2070), Sonic & Sega All-Stars Racing (diffusion), GRID, Wacky Races (`LINKID`), Battle Gear 4 / Tuned, D1GP, Valve Limit R, Sega Racing Classic ; plus lourds (services Sega / Namco) : Initial D 6 à 8 et Zero, Wangan 5 / 5 DX+ / 6, SWDC ; pas de link ou inconnu : Let's Go Safari, Crazy Ride, K.O. Drive, Chase H.Q. 2, Storm Racer G, Storm Rider, Frenzy Express |
| 258 | 31/10 | (Image borne) essais du link de Ford Racing, GTI Club et DiRT Showdown sur racecab1 et 2 (camera4, LINK=192.168.1.4,192.168.1.5) | 🔄 Ford Racing ✅ course à deux (6 voitures, chacun voit l'autre) ; GTI Club ✅ course à deux (MATCHING / STORE, 2 joueurs et 2 COM) ; DiRT Showdown : les bornes se trouvent (UDP 1240), se relient (1001), la partie MULTI-PLAYER ouvre une session (1000 / 1250, la borne 2 connectée à la 1), mêmes étapes du Shell, trafic de course d'environ 1,6 Ko/s, mais chacun court seul contre l'IA. Le jeu ne lit pas NumCabinets (absent de l'exécutable). Le journal du link note maintenant les 25 premiers paquets de chaque port local (et non plus de chaque PC), pour voir ceux de la course ; le prochain essai dira ce qu'échangent les bornes pendant la course |
| 259 | 31/10 | (Image borne) DiRT Showdown avec link7 : le link marche ; course Knockout à deux, 6 IA et 2 joueurs, mêmes points sur les deux bornes, chacune voit l'autre « AAA » ; le premier essai était très probablement lié aussi (lu trop vite) | ✅ link de Showdown validé. Le journal par port gardait une place par port temporaire de Windows (chaque diffusion de Showdown a son socket) : les 16 places prises, les paquets envoyés sur 1000 (la course) n'étaient pas notés ; les ports de 49152 et plus partagent désormais une place |
| 260 | 31/10 | (Flycast, pour le propriétaire) dossier d'écriture inutilisable, règle commune à tous ses émulateurs : le tester au démarrage (le créer, y créer, écrire et effacer un fichier) ; s'il échoue, ne rien écrire dans les dossiers habituels (« tant pis les écritures sont perdues et on n'écrit pas dans les dossiers protégés quoi qu'il se passe »), lire quand même le dossier modèle et celui du jeu, aucun journal (« Non il n'en écrit aucun »), une notification de 20 s au démarrage, sans boîte de message, au texte de Flycast | ✅ avant : seul un dossier impossible à créer était écarté, et tout s'écrivait alors aux endroits habituels ; un dossier qui refusait l'écriture était utilisé (réglages d'usine, journal perdu). Désormais : essai dans le lanceur (WRITE_DIR, la copie des réglages aussi) et dans la DLL (WRITE_DIR\<jeu>) ; inutilisable : `WINDOWSLOADER_WRITE_DENIED`, réglages lus à leur place, écritures refusées (celles du jeu vers le dossier d'écriture où elles échouent, toutes les autres hors de lui par les hooks de fichiers, les réglages par défaut directement), pas de journal sauf `--log`, la raison sur la console et OutputDebugString ; le texte par-dessus le jeu 20 s (ImGui, Direct3D 9/10/11 et OpenGL, sans pause), et dans la page Jeu du menu ; le bouton Sauvegarder le dit. Le sélecteur n'écrit plus le windowsloader.ini modèle. Vérifié avec Ford Racing : lecteur absent et dossier refusé (icacls), rien de changé dans les dossiers du lanceur et du jeu, le texte à 6 s et plus à 24 s ; dossier normal comme avant ; le texte aussi sur Mario Kart DX (Direct3D 10), Wangan Midnight 5 (Direct3D 11, qui se bloque après 9 images : ses fichiers de comptabilité refusés) et Initial D 6 (OpenGL) |
| 261 | 31/10 | Demande à linuxloader comment il débloque les jeux qui ne démarreraient pas, comme Wangan 5, avec nettoyage automatique ; puis : oui, fais comme linuxloader, écris dans %temp%, mais uniquement pour certains fichiers jugés essentiels (un filtre sur le nom), pas tous les fichiers temporaires inutiles ; efface les anciens fichiers quand on quitte ou quand on lance | ✅ linuxloader : pas de nettoyage générique, les écritures d'un dossier d'écriture refusé vont dans des copies de travail (%TEMP%\linuxloader-nosave-<pid>, writeDir.c), effacées à la sortie et au lancement suivant. windowsloader : le même, limité aux fichiers nommés par `[Paths] TEMP_WRITES` (jokers, `;` entre eux) et par le module du jeu (`wl_files_temp_writes` : `*BK_*.bin` pour Wangan 5, 5 DX+ et 6), plus la mémoire des cartes que windowsloader garde (`wl_files_write_path`) ; copies faites depuis leur place, relues pendant la partie ; le lanceur efface les dossiers `windowsloader-nosave-*` dont le processus n'existe plus, à son lancement et à la fin. Toute autre écriture reste refusée. Vérifié avec Wangan Midnight 5 : ses 12 BK_*.bin dans les copies, 60 images/s jusqu'au bout (au lieu de s'arrêter après 9), dossier temporaire vidé, dossier du jeu inchangé |
| 262 | 31/10 | Le filtre est-il commun à tous les jeux, chaque module a-t-il ses filtres ? Ajoute ceux de SWDC et des autres jeux qui en ont besoin pour démarrer ; demande à linuxloader de faire le même filtre ; puis : commit, pousse, release, notifie Image borne | 🔄 commun (`wl_files_temp_writes`, files.cpp), chaque module peut déclarer les siens (seul Wangan pour l'instant), plus `[Paths] TEMP_WRITES`. linuxloader l'a fait (e34fe0f). Fait en chemin : dossier des copies au nom du lanceur (le jeu et amdaemon partagent leurs fichiers), chaque chemin refusé noté une fois, un fichier existant ouvert en écriture sans être recréé ouvert en lecture seule (Frenzy Express bloquait sur « ascii.tga is NOT FOUND », va maintenant à l'attract). Daytona 3 ne démarre pas sur ce PC (DLL absente de la copie, 0xC0000135, aussi sans dossier d'écriture). Le tour des autres jeux (SWDC…) reste à finir. Release 20261031-1 (a96b93a) : `releases\windowsloader-0.1.0-20261031-1-njz3-windows.zip`, SHA-256 7AA23A6D…2CE7F4, vérifiée sur Ford Racing (32 bits) et Wangan Midnight 5 (64 bits) ; Image borne prévenu pour la poser |
| 263 | 31/10 | Travaille d'abord sur le mode link : Sega Rally 3 (copié sur ce PC), Wasteland Racers 2071 (le protocole le plus simple), Sonic & Sega All-Stars Racing, GRID, Wacky Races, Battle Gear 4 / Tuned, D1GP, Valve Limit R, Sega Racing Classic | 🔄 Sega Rally 3 : Demonware en LAN (diffusion UDP 50000, session UDP 3074 et TCP 8086, GetAdaptersInfo), Winsock 1 par numéro : `link.cpp` prend désormais aussi `WSOCK32.dll` ; `[Network] CabinetID` / `Enabled` de ShellData.ini d'après LINK, option CABINET (1 à 4) ; essayé seul sur ce PC (CabinetID lu, carte filtrée, recherche vers les deux PC, écoute sur 50000), pas encore sur les bornes. Sur les bornes (essai A) : course rapide à deux réussie, confirmée par le propriétaire ; la seconde borne appuie sur Start pendant le décompte de la première (environ 15 s). Les autres : exécutables et réglages copiés par Image borne (Wasteland et SSASR complets, GRID en partie, les cinq autres en cours) ; analysés (sous-agents) et branchés : GRID (CabinetID 1 à 6, diffusion UDP 12000, UDP/TCP 7342), Wasteland (élection UDP 2070, puis DirectPlay par dpnet.dll), SSASR (adresses 192.168.88.k traduites vers les PC de LINK : `wl_link_virtual_net`) ; `link.cpp` : gethostbyname du nom du PC → son adresse de LINK seule. Pas essayés ici (copies sans données) |

## Points ouverts

- **Licences du code à reprendre.** linuxloader n'a pas de fichier `LICENSE` dans son dépôt : vérifier
  la licence d'upstream (lindbergh-loader) avant d'en copier du code dans un projet GPL v3. Flycast :
  GPL v2 « ou toute version ultérieure » dans ses en-têtes, donc compatible. MAME : GPL v2 pour
  l'ensemble, mais chaque fichier a sa licence (souvent BSD-3 ou GPL-2.0+) ; vérifier fichier par
  fichier. segatools : licence à lire avant de reprendre quoi que ce soit.
- **Session à distance.** Ce PC est utilisé en Bureau à distance : Sega Rally 3 refusait de démarrer
  (« Remote Sessions not supported »). Corrigé : `GetSystemMetrics(SM_REMOTESESSION)` renvoie 0 au jeu.
- **Écran verrouillé.** Même chose quand la session est verrouillée (`LogonUI.exe` tourne, l'affichage
  devient « WinDisc ») : `CreateDevice` échoue (0x8876086C). Les essais demandent une session
  déverrouillée.
- **Session déconnectée.** Quand la fenêtre Bureau à distance est fermée, la session Windows est
  « déconnectée » (`qwinsta`) : plus d'affichage, Direct3D perd son device (Present échoue, les captures
  aussi) ou ne peut pas en créer (`CreateDevice` 0x8876086C, « Failed to create a d3d device »). Les
  essais du jeu demandent une session connectée. Le jeu ne fait jamais `Reset` après une perte.
- **Mes essais et vJoy.** Les essais en mode agent ne lisent pas les manettes et n'envoient rien à vJoy,
  sauf avec `--controllers` (agent + manettes + retour de force). Un lancement normal ouvre vJoy (joueur 1)
  avec le retour de force. `SDL_JOYSTICK_IGNORE_DEVICES=0x1234/0xBEAD` ne l'empêche pas (essayé le 21/10 :
  vJoy ouvert 15 s, en démo, forces nulles, vJoy annoncé libre par la session Flycast). Avant un essai qui
  l'ouvre : prévenir les sessions Compilation, Driveboard, Multi-Board et Linuxloader FFB edition.
- **Affichage Direct3D sur ce PC.** Un Present en mode blt (D3D9 DISCARD/COPY, DXGI) ne montre rien à
  l'écran, même sans windowsloader (programme d'essai `d3d9red`) ; seuls D3D9Ex FLIPEX et GDI s'affichent.
  D'où la recopie GDI par défaut (`PRESENT = GDI`, demande 40). Sur la borne, `PRESENT = DIRECT` reste possible.
- **Pare-feu Windows.** Battle Gear 4 écoute sur le port 8821 (liaison des bornes) : une boîte « Sécurité
  Windows » pour `game.exe` est ouverte depuis le 22/10, laissée à l'utilisateur.
- **Son de ce PC.** Les haut-parleurs y sont déclarés en 7.1 et DirectSound est très lent par tampon (36 ms
  pour créer un tampon, 0,3 s un tampon 3D, plus d'une seconde par validation 3D de Chase H.Q. 2) : Battle
  Gear 4 reçoit « stéréo », Chase H.Q. 2 perd le mouvement 3D de ses sons. Une 3D logicielle (volume et
  balance calculés par windowsloader) serait la vraie réponse.
- **Direct3D 11 et l'affichage.** Réglé le 21/10 (demande 27) : la chaîne d'échange affiche dans une fenêtre
  enfant placée sur l'image. Pendant mes essais sur ce portable, `Present` rendait parfois `DXGI_STATUS_OCCLUDED`
  (écran éteint, session montrée nulle part : affichage « WinDisc ») : le jeu tourne alors sans synchro et
  Daytona ne démarre pas (aucune sortie écran). La capture `screen` de l'agent lit le mauvais endroit quand la
  fenêtre du jeu ignore l'échelle de l'écran (150 % ici) : `display_check.py` capture de l'extérieur.
- **`GameData\debug.txt` de Daytona** : 3 Go (2,3 Go avant mes essais, 0,7 Go ajoutés par eux avant le
  plafond), et 27 vidages `*.dmp` anciens plus un des miens (21/10 22:07, 231 Ko). Rien supprimé : à
  l'utilisateur de voir s'il les garde.
- **inpout32 et le matériel du PC.** Sega Rally 3 charge `..\Shell\inpout32.dll` et écrit sur le port
  0x1020. Vérifié le 21/10 : aucun pilote de ports n'est installé ici et le jeu tourne sans droits
  administrateur, donc les écritures des premiers essais n'ont rien atteint. Désormais la vraie DLL n'est
  jamais chargée (ports émulés).

## Jeux des bornes

Liste transmise le 21/10 par la session « Image borne (Berstett) », à la demande de l'utilisateur. Tout y
est mesuré sur les bornes : profils TeknoParrot pour la plateforme et le chemin de l'exécutable, en-têtes
PE pour l'architecture, noms de sections pour une enveloppe de protection, bibliothèques de dongle à côté
de l'exe ; « démarre » et « FFB » viennent de campagnes où chaque jeu a été lancé sous son loader actuel,
piloté par vJoy, et ses paquets de retour de force comptés. La session peut donner le chemin exact d'un
exécutable, ses imports, ses chaînes, ou copier un binaire (elle pilote les deux bornes à distance).

### Les 21 jeux propres, qui démarrent tous

| Jeu | Profil | Loader actuel | Matériel d'origine | PE | FFB (paquets) | windowsloader |
|---|---|---|---|---|---|---|
| Battle Gear 4 | BattleGear4 | OpenParrot | Taito Type X+ | 32 | non (8) | ✅ l'édition de 2006 : vidéo de démo (DirectShow redirigé), course, test d'E/S, vJoy, F10 (agent) ; FFB ⚠️ |
| Battle Gear 4 Pro | BattleGear4Pro | OpenParrot | Taito Type X+ | 32 | oui (952) | ✅ `[Game.bg4] CABINET = PRO` sur le dossier Tuned : levier en H, embrayage, grille motorisée (agent) |
| Battle Gear 4 Tuned | BattleGear4Tuned | OpenParrot | Taito Type X+ | 32 | oui (16 607) | ✅ jusqu'en course (agent), menu de service ; FFB ⚠️ (agent) ; la copie est l'édition « Professional » (option TeknoParrot) |
| Chase HQ 2 | ChaseHQ2 | OpenParrot | Taito Type X2 | 32 | oui (6 816) | ✅ jusqu'en course (agent), étalonnage au menu de service ; FFB ⚠️ (agent) |
| Crazy Ride | CrazyRide | TeknoParrot | UNIS PC (UE4) | 64 | non (0) | ✅ course (agent, vJoy), menu de test, F10 ; FFB ⚠️ (moteur supposé) |
| D1GP | D1GP | TeknoParrot | Taito Type X2 | 32 | non (0) | ✅ course (agent, vJoy), JVS, volant DirectInput, F10, service ; FFB ⚠️ (Immersion) ; lecteur de cartes non émulé |
| Daytona 3 | Daytona3 | TeknoParrot | Sega | 32 | oui (9 164) | ⚠️ joue en mode agent (demande 24) |
| Daytona 3 New Season | Daytona3NSE | TeknoParrot | Sega | 32 | oui (12 684) | ⚠️ joue en mode agent (demande 24) |
| Ford Racing | FR | TeknoParrot | Sega Arcade PC | 32 | oui (4 426) | ✅ (menu de service ⚠️ : clé iButton) |
| GRID | GRID | OpenParrot | — (`C:\Sega\Grid`) | 32 | oui (26 437) | ✅ jusqu'en course (agent), vJoy, F10 ; service ❌ (clé Dinkey de la coquille) |
| GTI Club 3 | GtiClub3 | OpenParrot | Konami PC | 32 | oui (9 573) | ✅ jusqu'en course (agent), vJoy, F10, menu de service ; FFB ⚠️ (agent) |
| Initial D 6 AA | ID6 | TeknoParrot | Sega Ringedge | 32 | oui (13 400) | ✅ jusqu'en course (agent) ; FFB, affichage ⚠️ |
| K.O. Drive | KODrive | TeknoParrot | Sega Ringwide | 32 | oui (20 854) | ✅ jusqu'en course (agent), vJoy, F10, menu de service ; FFB, affichage ⚠️ |
| Mario Kart DX v1.10 | MKDX | TeknoParrot | Namco ES3 | 32 | non (62) | ✅ le mod français : clé, lecteur de cartes NFC, authentification, course, vJoy, menu ; FFB ⚠️ |
| Mario Kart DX v1.18 | MKDX118 | TeknoParrot | Namco ES3 | 32 | oui (397) | ✅ démarrage complet, course, menu de test, F10 (agent) ; FFB ⚠️, vJoy ⏳ |
| Showdown | Showdown | TeknoParrot | Sega Arcade PC | 32 | oui (25 955) | ✅ jusqu'en course (agent) ; FFB ⚠️ (agent, port 0x378) |
| Sonic & Sega All-Stars Racing | SSASR | TeknoParrot | Sega Ringwide | 32 | oui (25 094) | ✅ jouable : démarrage (carte du volant, réseau, EEPROM du système), pièces ou jeu gratuit (option), course, vJoy, F10, menu de service |
| Sega Rally 3 | SR3 | TeknoParrot | Sega Europa R | 32 | non (63) | ✅ (retour de force par le port 0x1020) |
| Sega World Drivers Ch. | SWDC | TeknoParrot | Sega ALLS (UE4) | 64 | oui (14 261) | ✅ partie invité complète, borne complète (tableau de bord, carte LED) ou mini (option CABINET), services réseau joués sur le PC, vJoy ⚠️, F10/Tab, menu de test ; FFB ⚠️ (agent) ; pas de course en réseau (serveur de matching) |
| Virtua RLimit | VirtuaRLimit | OpenParrot | Taito Type X | 32 | non (0) | ✅ « Valve Limit R » : course (agent, vJoy), test d'E/S, étalonnage des pédales, F10 ; FFB ⚠️ |
| Wasteland Racers 2071 | WastelandRacers | TeknoParrot | Trio Tech PC | 32 | oui (23 484) | ✅ jusqu'en course (agent), vJoy, F10, menu de maintenance |

13 des 21 émettent du retour de force sous leur loader actuel. Sega Rally 3 a huit entrées (Standard,
Alpine, Artic, Canyon, Master, Safari, Tropical, Tropical478) : huit dossiers qui partagent le même
`Rally.exe`, donc un seul module. Deux jeux seulement sont en 64 bits, Crazy Ride et SWDC (Unreal Engine) ;
les dix-neuf autres sont en PE32.

### Autres jeux Windows des bornes (27/10, Image borne)

Relevés dans les profils TeknoParrot / OpenParrot et les listes AttractPlus de racecab1, bornes éteintes :
52 profils Windows, 34 déjà dans windowsloader. L'architecture est celle du profil (Is64Bit) ; l'en-tête,
la protection et la taille attendent les bornes. FFB : campagne du 17/10 sous le loader actuel.

| Jeu | Profil | Loader actuel | Matériel d'origine | PE (profil) | FFB (paquets) | windowsloader |
|---|---|---|---|---|---|---|
| Frenzy Express (`Frenzy Express - Uniana PC Based\xwin.exe`) | FrenzyExpress | OpenParrot | Uniana PC (moto) | 32 | non (0) | ✅ jouable (agent) : guidon, poussées, frein, moteur du guidon, lampes ; à voir sur borne |
| Initial D Arcade Stage Zero v2.30 (`InitialD0_DX11_Nu.exe`) | IDZv2TP | TeknoParrot (ligne désactivée) | Sega Nu | 64 | non mesuré | ✅ partie invité (agent), retour de force, lampes ; cartes Aime ⏳ (serveur en ligne du jeu) |
| Initial D Arcade Stage Zero v1.31 (`app\package\InitialD0_DX11_Nu.exe`) | IDZTP | TeknoParrot (ligne désactivée) | Sega Nu | 64 | non mesuré | ✅ partie invité (agent), retour de force, menu Tab ; cartes Aime ⏳ (comme la v2.30) |

Déjà dans windowsloader mais pas encore dans la liste AttractPlus « Windows Loader » de l'image : Initial D 7,
Initial D 8, Wangan 6 (ajout quand le propriétaire le demande). Écartés : les Raw Thrills (Batman, Cars, Cruis'n
Blast, Dirty Drivin, Fast & Furious, H2 Overdrive, Nicktoons Nitro, Snocross).

### Écartés, avec la raison

- **Linux, pour linuxloader** : Cruis'n Blast, Dead Heat, Hummer Extreme, Initial D 4 et 5, OutRun 2 SP SDX,
  R-Tuned, Sega Race TV, Tokyo Cop, Gaelco Championship Tuning Race, WMMT3 et 3DX+ (transmis à la session
  linuxloader, qui en a repris six).

## Journal de développement

### 21/10 — Démarrage, squelette et lanceur (demandes 1 à 8)

- Arborescence : `src/launcher` (exe), `src/hook` (DLL), `src/common` (réglages partagés),
  `src/minhook` (copié de `src/minhook` de linuxloader, licence BSD 2 clauses de MinHook).
- Build : `CMakePresets.json` repris de linuxloader (VS, vcpkg en manifeste, triplets `*-windows-static`,
  CRT statique `/MT`). Deux presets, `msvc-x86` et `msvc-x64` ; tout arrive dans `bin/<config>`.
  `VCPKG_ROOT` : celui de Visual Studio (`C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg`).
- Lanceur : lit la taille de bits de l'exe (en-tête PE) et passe la main à `windowsloader64.exe` pour
  un jeu 64 bits ; lance le jeu suspendu dans son dossier, dans un job object (le fermer arrête le jeu),
  injecte la DLL par `CreateRemoteThread(LoadLibraryW)`, reprend le jeu, recopie le journal de la DLL
  sur sa console, `--timeout` pour les essais.
- Réglages lanceur → DLL par variables d'environnement (`WINDOWSLOADER_LOG`, `_WINDOWED`, `_IOLIMIT`).

### 21/10 — DLL observatrice, Sega Rally 3 (demande 9)

- Jeu copié de racecab1 (`Sega Rally 3 - SEGA Europa R - Original`, 1,4 Go) dans `games/segarally3`.
  Fichiers TeknoParrot sortis du dossier du jeu vers `Rally\_teknoparrot_off` (rien supprimé) :
  `dinput8.dll` (FFBPlugin, chargé sinon par l'import DINPUT8 du jeu), `FFBPlugin*`, `MetroFramework.dll`,
  `SDL2.dll`, `OutputBlaster.*`, `teknoparrot.ini`.
- `Rally\Rally.exe` : x86, VS2005, CRT statique. Imports utiles : d3d9 (`Direct3DCreate9`), DINPUT8,
  XINPUT1_3, HID + SETUPAPI, `CreateFileA`/`ReadFile`/`WriteFile`/`SetCommState`, WSOCK32, iphlpapi.
- Hooks (MinHook) posés sur les exports de kernel32 et user32 (leurs stubs, donc seuls les appels du jeu
  et des DLL qui importent kernel32 passent par là). Garde de réentrance par thread : l'appel d'origine
  se fait « occupé », donc A→W, le `WriteFile` du journal, etc. ne sont jamais journalisés deux fois.
  Activation groupée (`MH_QueueEnableHook` + `MH_ApplyQueued`) : 80 hooks en 0,1 s au lieu de 8 s.
- Ne sont journalisés que les appels venant du dossier du jeu (exe et ses DLL), sauf les redistribuables
  Microsoft (xinput, d3dx...) : XInput interroge SetupAPI en boucle.
- Protection du PC en mode fenêtre : `ChangeDisplaySettings`, `ClipCursor`, `SetCursorPos`, hooks
  clavier globaux, `SendInput`, réglages `SystemParametersInfo` (touches rémanentes...) refusés ;
  `CreateProcess`, `ShellExecute`, arrêt et mise en veille refusés dans tous les cas.

Ce que montre Sega Rally 3 :

- Lit `.\config.ini` ([Graphics] 1920×1080, MSAA 2, VSync) et `..\ShellData\*.ini` (réglages de
  la borne : `CabinetType=Twin`, `MotionBaseOK=0`, calibrage volant/pédales 0..255, `Freeplay=1`).
- `Direct3DCreate9` puis `CreateDevice` 1920×1080 plein écran 60 Hz → forcé en fenêtre, le jeu
  tourne : vidéos d'intro (DirectShow), puis démo, 20 à 30 images/s sur ce PC.
- **Carte d'E/S : USB HID.** Le jeu parcourt les périphériques HID (SetupAPI), ouvre chacun,
  `HidD_GetAttributes` et garde celui qui a **VID 0x1043, PID 0x0000** (`Rally.exe+0x17b45e`). Puis
  `HidD_GetPreparsedData` + `HidP_GetCaps` et choisit un gestionnaire dans une table (0x70B7B8) selon
  l'usage HID. Thread « Input HID Thread ». Sur ce PC, le premier périphérique HID (Intel 8087:0ac2)
  fait échouer `HidD_GetAttributes`, et le jeu abandonne la recherche à ce moment-là.
- **Ports série :** fonction 0x62DED0, ouvre `COM1` et `COM2` à 38400 bauds 8E1, RTS en mode bascule
  (RS485 demi-duplex). Pas appelée pendant les essais : sans doute la base mobile de la version Twin
  (`MotionBaseOK`). À confirmer.
- `GetAdaptersInfo` (adresse MAC, lien réseau), `NetworkTitleID.txt` absent, mutex `Racing-`.

### 21/10 — Carte d'E/S émulée et mode agent (demandes 10, 14, 15)

- `vhid.cpp` : une carte USB HID émulée. Les énumérations HID du jeu par SetupAPI ne voient qu'elle
  (les périphériques HID du PC sont cachés au jeu, pas à DirectInput ni à SDL) ; `CreateFile` sur son
  chemin rend un vrai handle (un événement), `HidD_GetAttributes` / `HidD_GetPreparsedData` /
  `HidP_GetCaps` répondent comme la carte, `ReadFile` bloquant rend un rapport toutes les 4 ms.
- `games/segarally3.cpp` : rapport de 15 octets (octet 1 volant, 3 frein, 4 accélérateur, 6 à 8 les
  24 boutons). Plages du volant et des pédales lues dans `[CalibrationData]` de `ShellData.ini`
  (10..245, 40..210). Boutons trouvés par captures en mode agent : bit 0 Start, 1 changement de vue,
  3 rapport +, 4 rapport − ; les autres ne font rien de visible (pas de menu de test dans le jeu :
  sur Europa-R c'est le Shell de la borne qui l'a, et il n'est pas dans le dossier).
- Vérifié en mode agent : Start ouvre le choix du mode, le volant change de mode (droite = suivant),
  le frein ne valide pas, l'accélérateur valide (mode, voiture, boîte AT/MT), course : 96 km/h en 3e
  à fond, le volant braque. Captures dans `logs/sr3bits`, `logs/sr3pedals`, `logs/sr3race`.
- Mode agent : `--agent`, tube `\\.\pipe\windowsloader-<pid>` servi par la DLL, relayé par le lanceur
  (stdin → tube, tube → stdout, messages sur stderr). `ready GAME W H` à la première image, puis
  `frame`, `step N` (le jeu n'est pas bloqué entre deux pas), `shot PATH` (back buffer en BMP, lu dans
  Present), `inputs`, `set`, `setraw`, `release`, `get`, `quit`. Client : `scripts/agent/wlagent.py`
  (écrit pour windowsloader, pas copié de `llagent.py`), essais `sr3_bits.py`, `sr3_pedals.py`,
  `sr3_race_bits.py`.
- Commandes (`controls.cpp`, écrit pour windowsloader) : syntaxe et noms de `controls.ini` de
  linuxloader (`[Common]`, `[Driving]`, `KEY_`, `JOYn_`, `GCn_`, suffixes, `+`, `_DeadZone`,
  `[ControllerGUIDs]`), lues par SDL3 dans un thread du jeu ; clavier seulement quand la fenêtre du
  jeu a le focus ; pas lues en mode agent. `controls.ini` à côté du lanceur, créé avec les liaisons par
  défaut s'il manque. Vérifié : fichier créé, 7 actions liées, vJoy vu comme joueur 1.

### 21/10 — Retour de force de Sega Rally 3 (demande 11)

- DirectInput observé (`dinputhook.cpp`) : le jeu n'ouvre que le clavier et la souris (non exclusifs),
  aucun joystick, aucun effet. Il ne touche donc pas vJoy. Les sessions sm2-emu, Supermodel, Flycast et
  linuxloader ont été prévenues avant l'essai, puis à la fin.
- Ports série : `COM1`/`COM2` seulement si `[gamesettings] MotionBaseOK=1` (base mobile).
- Le moteur du volant : `LoadLibrary("../Shell/inpout32.dll")`, `GetProcAddress("Out32")` (Rally.exe
  +0x2582a8, appelé seulement quand la carte d'E/S est là), puis un octet sur le port 0x1020 (+0x1c866f) :
  0 sans force (|f| ≤ 1/15), sinon quartet bas `(int)(15 − |f|·14)` (1 la plus forte, 14 la plus faible),
  bit 4 mis quand f ≥ 0. Une dizaine d'autres endroits écrivent 0 (pause, fin de course...).
- `ioports.cpp` : `LoadLibrary` d'inpout32 / inpout32_d / inpoutx64 rend le module de windowsloader
  (référence prise, `FreeLibrary` équilibré) ; `GetProcAddress` y rend `Out32`, `Inp32`,
  `IsInpOutDriverOpen`, `DlPort*` émulés. Vérifié : « LoadLibrary "../Shell/inpout32.dll" -> emulated I/O
  ports », puis `out 0x1020 <- 0x00`.
- `ffb.cpp` : la force -1..1 (> 0 pousse à gauche, convention de linuxloader), l'historique pour la
  commande agent `ffb` ; `controls.cpp` la joue sur le volant du joueur 1 (SDL haptic, une force
  constante mise à jour quand elle change), `[Config] FFB_Strength` et `FFB_Invert`.
- Sens du bit 4 fixé par `scripts/agent/sr3_ffb.py` (course, volant tenu 90 images de chaque côté) :
  bit 4 mis = pousse à droite, donc `FFB_SIGN = -1`. Après correction : volant à droite, force moyenne
  +0,37 (jusqu'à 0,71) ; à gauche, −0,32 ; une valeur toutes les ~30 ms (à chaque image). 0x0F, écrit
  pendant les chargements, vaut une force nulle.
- Reste : essayer le rendu SDL haptic sur un vrai volant (et le sens de SDL, `FFB_Invert` au besoin).

### 21/10 — Menu F10 (demande 19)

- `src/imgui` : Dear ImGui 1.92.0 et ses backends DX9 et Win32, copiés sans changement de
  `3rdparty/imgui` de sm2-emu (même version que linuxloader), licence MIT (`src/imgui/LICENSE.txt`).
- `menu.cpp` : F10 (lu par `GetAsyncKeyState`, fenêtre du jeu au premier plan) ouvre le menu dans le
  hook de Present : l'image du jeu est copiée, puis chaque image du menu la redessine avec ImGui par-dessus
  et la présente ; le thread de rendu du jeu reste là, sa logique est donc arrêtée. ImGui travaille à la
  taille de l'image (1920×1080) et la souris est mise à l'échelle depuis la fenêtre (1674×942). Le
  clavier passe par la procédure de fenêtre du jeu, sous-classée (ANSI) ; le jeu ne reçoit ni souris ni
  clavier tant que le menu est ouvert ; F10 (touche système) n'ouvre plus le menu système de la fenêtre.
- `pause.cpp` : `QueryPerformanceCounter`, `GetTickCount` et `timeGetTime` remplacés dans la table
  d'imports de Rally.exe seulement (le reste du processus garde l'heure réelle) : figés pendant la pause,
  puis décalés du temps passé en pause. Son : les sessions audio du processus coupées (Core Audio).
- `controls.cpp` : liaisons lues et écrites dans `controls.ini` pour le menu, rechargées par le thread des
  commandes ; capture : la première touche, le premier bouton, chapeau ou axe qui change (un axe de volant
  tourné à gauche, une pédale de bout en bout ou depuis le centre, `_INVERTED`, `_POSITIVE_HALF`...).
- Essai `scripts/agent/sr3_menu.py` (session déverrouillée), une capture par page dont Affichage : menu ouvert pendant la démo, une capture
  par page (`logs/sr3menu`), 2 images du jeu pendant 4,3 s de pause, 123 après la fermeture ; « sound
  muted (1 audio session) » puis « back ». Police Segoe UI, ~31 px pour une image de 1080 lignes, accents
  corrects. En mode agent, la page Contrôles lit `controls.ini` (table des liaisons, valeurs tenues par
  l'agent) mais grise l'assignation.

### 21/10 — Aspect, mode d'affichage, résolution interne (demande 21)

- `display.cpp`, `windowsloader.ini` à côté du lanceur, `[Display]` comme linuxloader :
  `ASPECT_RATIO = ORIGINAL | 4:3 | 16:9 | 4:3_LETTERBOX`, `DISPLAY_MODE = WINDOW | WINDOW_STRETCH |
  FULLSCREEN | FULLSCREEN_STRETCH`, `WIDTH` / `HEIGHT` (`AUTO`). Fichier créé avec ses commentaires s'il
  manque. Page Affichage de F10 : aspect et mode appliqués tout de suite et enregistrés ; la résolution
  interne au prochain lancement. F11 ou Alt+Entrée : fenêtre ↔ plein écran (pas enregistré).
- Comment : la chaîne d'échange du jeu passe en `D3DSWAPEFFECT_COPY` (1 back buffer), qui accepte un
  rectangle de destination dans `Present` ; l'image y est placée, le reste de la zone client peint en noir
  (GDI, à chaque changement et toutes les 30 images). La fenêtre prend le style du mode : cadre fixe à
  l'aspect (taille du rendu élargie ou rehaussée, réduite à l'écran), cadre redimensionnable (garde sa
  taille ensuite), ou popup sans bord couvrant l'écran. 4:3_LETTERBOX : l'image entière à ses proportions
  dans le cadre 4:3, bandes calculées en parts du cadre (elles suivent l'étirement). Avec un back buffer
  multiéchantillonné (COPY impossible), l'image remplit la fenêtre. `WINDOWSLOADER_SWAP=discard` garde la
  chaîne d'échange du jeu (secours).
- La souris du menu suit le rectangle de l'image ; la procédure de fenêtre du jeu est sous-classée par
  `display.cpp` (ANSI ou Unicode selon la fenêtre), le menu s'y branche.
- Résolution interne de Sega Rally 3 : `GetPrivateProfileIntA` de `Rally\config.ini`, `[Graphics]
  HorizontalResolution / VerticalResolution`, renvoie `WIDTH` / `HEIGHT` (le fichier du jeu n'est pas
  modifié). Vérifié : « HorizontalResolution = 1280 from windowsloader (the file says 1920) »,
  `CreateDevice` en 1280×720, fenêtre 1280×720, `ready 1280 720`.
- Vérifié sur l'écran 2048×1280 (16:10) par le journal : plein écran ORIGINAL → image 2048×1152 à y=64 ;
  4:3 → 1707×1280 à x=170 ; 4:3_LETTERBOX → 1707×960 à 170,160 ; plein écran étiré 4:3_LETTERBOX → 2048×960
  à y=160 ; fenêtre 4:3_LETTERBOX → fenêtre 4:3, image 1604×902 à y=150.
- Pas vérifié à l'œil : une capture GDI de l'écran (`screen` de l'agent, ou PowerShell hors du jeu) rend la
  fenêtre du jeu noire, avec ou sans COPY, alors que le back buffer est bon et que le jeu s'affiche bien :
  la capture ne voit pas l'image Direct3D sur ce PC. Le processus du jeu n'est pas « DPI aware » (écran à
  150 %) : ses coordonnées d'écran sont virtualisées.

### 21/10 — Daytona Championship USA, les deux éditions (demande 24)

Ce que fait le jeu (désassemblé, adresses de l'originale, `Daytona.exe` x86 ; la New Season Edition
fait la même chose à d'autres adresses : mêmes constantes, même table de boutons). Il est bâti sur le code
de Sega Racing Classic (`e:\work\sai\daytona\trunk\segaracingclassic\d1a`), qui garde les entrées du
Daytona USA Model 2.

- **Carte d'E/S** : une manette DirectInput nommée « SEGA IO Controller » (+0x1d9bb0), prise par son nom,
  axes réglés à −1000..1000, `DIJOYSTATE` lu par `GetDeviceState` (+0x7cdf0). X = volant, comparé à
  `[CalibrationData] SteeringLeft / SteeringCentre / SteeringRight` de `ShellData.ini` (unités 0..255) ;
  Rx = accélérateur, Z = frein, sans calibrage (+0x1ddd80 les range dans l'ordre Model 2 : volant,
  accélérateur, frein). Boutons, par la table du jeu (+0xd5c490) : 0 Start, 1 2 6 7 rapports 1 à 4 du
  levier en H, 3 rapport +, 4 rapport −, 8 à 11 les vues VR1 à VR4 (rouge, bleu, jaune, vert). Sans
  « SEGA IO Controller », le jeu lit une manette XInput (chemin de développement).
- **Moteur du volant** : sur le port parallèle, écrit par `c:\sega\shell\InpWrapper.exe` (x64, inpoutx64)
  que le jeu lance avec `-h=<sa fenêtre> -p=<port>` (+0x128b70). InpWrapper envoie à la fenêtre du jeu un
  `WM_COPYDATA` type 3 avec son propre handle (+0xdcd80 le prend), puis le jeu lui envoie `WM_COPYDATA`
  type 0x20 avec un octet (+0x129570) qui part sur le port : 0 rien, sinon quartet bas = force (1 la plus
  forte, 15 la plus faible), bit 4 = vers la droite. Le même codage que le port 0x1020 de Sega Rally 3.
- **Shell** (`Sega\shell\Shell.exe` : pièces, crédits, menu de test, sécurité, carte HID par hidapi) :
  TCP 127.0.0.1:18888 en dur (+0x1826a0). Trames de 11 octets « DYT », commande et longueur (gros-boutiste),
  4 octets nuls, puis les données. Le jeu annonce ce qu'il fait (`GAME_MSG_*`, 55 noms dans le binaire), le
  Shell fixe les crédits (`SHELL_MSG_SETCREDIT`, un octet) et répond à un défi envoyé toutes les 10 s
  (`GAME_MSG_SECURITY_CHALLENGE`, 4 octets) : 5 défis sans réponse et le jeu quitte (+0x182a00) — d'où
  l'arrêt à 54 s du premier essai. La vérification (+0x182500) transforme la réponse : XOR 0xB1, 12 tours
  de décalage à rétroaction (masques 40 08 10 04 et 0xA5), octet 0 aux bits inversés, XOR 0xC0, XOR 0x77,
  puis compare au défi. Inversée tour par tour (un bit inconnu par tour, retour arrière) : vérifiée en
  Python sur 20 000 défis, puis par le jeu lui-même.
- **Réglages** : `[Network] NETWORK_TYPE` et `[OpenCV] ENABLE_CAPTURE` de `config.ini` n'existent pas dans
  le binaire (surcharges retirées). Le jeu lit `ShellData.ini [Network] CabinetID / NumCabinets` et tente
  les bornes voisines en TCP 192.168.1.2–8:26000.

Ce que fait windowsloader (aucun patch du jeu, aucun fichier du jeu modifié) :

- `vdinput.cpp` : la manette DirectInput « SEGA IO Controller » (6 axes, 12 boutons) jouée par
  windowsloader ; le format de données du jeu est respecté comme DirectInput le fait. Les vraies manettes
  du PC (vJoy compris) sont cachées au jeu, en DirectInput comme en XInput : il ne les ouvre jamais.
- Faux InpWrapper : le `CreateProcess` d'InpWrapper est pris par le module (`wl_system_set_process_handler`),
  une fenêtre cachée de windowsloader fait la poignée de main et reçoit les octets du port.
- Faux Shell : un serveur sur 127.0.0.1:18888 dans la DLL, qui répond aux défis, envoie les crédits (pièce
  et service), décompte un crédit à `GAME_MSG_START_PRESSED` hors free play, et note les messages du jeu.
- `system.cpp` : `connect` et `sendto` du jeu vers une adresse hors 127.0.0.0/8 refusés (chaque adresse
  notée une fois). Surcharges : `[FullHD] ENABLE_FULL_SCREEN = 0`, `[Network] NumCabinets = 1`.
- `d3d11hook.cpp` : le jeu crée sa chaîne d'échange par la fabrique de l'adaptateur de son device ; elle est
  accrochée dès `D3D11CreateDevice` (plus `CreateSwapChainForHwnd`, `MakeWindowAssociation` sans Alt+Entrée
  de DXGI). Captures du back buffer pour l'agent (texture de staging), fenêtre selon `[Display]`.
- `controls.ini` : nouvelles actions `View1`–`View4` (B N M , comme sm2-emu ; A B X Y), `Gear1`–`Gear4`
  (Q W E R).
- **Mémoire, carte graphique** : au choix de la voiture, le jeu se figeait en écrivant `GameData\debug.txt`
  (« Memoey allocation failed », puis le vidage de ses 527 000 blocs, ~70 Mo à chaque échec, ~7 Mo/s :
  le fichier, déjà à 2,3 Go des sessions TeknoParrot, est passé à 3 Go avant que j'arrête le jeu). Cause :
  l'espace d'adressage du processus 32 bits (4 Go, le jeu est LARGE_ADDRESS_AWARE) épuisé, 3,1 Go privés
  après le chargement. Le jeu crée ~2,4 Go de ressources Direct3D 11 (usage DEFAULT) ; ce PC portable a
  deux cartes (Intel UHD intégrée, NVIDIA RTX 3050 Laptop) et Direct3D prend l'Intel par défaut, dont les
  textures sont en mémoire système mappée dans le processus. Mesuré avec un petit programme 32 bits :
  300 textures de 4 Mo remplies → +1 200 Mo d'espace privé sur l'Intel, +40 Mo sur la NVIDIA. Une borne n'a
  qu'une carte dédiée. Désormais `d3d11hook.cpp` donne au jeu qui demande l'adaptateur par défaut celui qui
  a le plus de mémoire vidéo dédiée (`WINDOWSLOADER_GPU=default` pour garder celui de Windows), et
  `files.cpp` plafonne `debug.txt` à 32 Mo par ouverture (le reste est jeté, le jeu croit l'avoir écrit).
  Le fichier existant (3 Go) n'est pas touché : à l'utilisateur de décider s'il le garde.
- **New Season Edition** : elle restait sur « NETWORK DISCOVERY PLEASE WAIT ». Son écran n'en sort que
  quand la découverte réseau est finie et que le Shell a envoyé la SKU de la borne (+0xdd17e : 0x577770
  puis 0x584ac0, `SHELL_MSG_SET_SKU` lève l'indicateur ; un deuxième octet à 1 active en plus le message
  0x37, sans nom). L'originale ignore ce message. Le faux Shell envoie `SET_SKU 00 00` à la connexion.
  Ses défis valaient `00 00 00 00` tant que la SKU manquait. Vérifié : ressources chargées à 49 s (40 s pour
  l'originale), démo, poignée de main du moteur, pièce, Start, choix du mode. Elle n'a pas de
  `shell\Game.ini` : port parallèle par défaut 0x378. `[Network] Enabled = 0` (borne seule pour elle)
  essayé, sans effet sur ce blocage, retiré.

### 21/10 — Affichage vérifié à l'écran, Direct3D 11 (demande 27)

- `scripts/agent/display_check.py [day | day-nse | sr3]` : chaque couple aspect × mode, puis WINDOW_STRETCH
  avec la fenêtre réduite par le script (`SetWindowPos`). Capture de la zone client vue à l'écran par Python
  déclaré « per-monitor DPI aware » (la fenêtre du jeu ne l'est pas : Windows l'agrandit ×1,5 sur cet écran
  2048×1280 logiques, 3072×1920 physiques), image et bandes mesurées (lignes et colonnes à plus de 20 % non
  noires, sans la ligne de bord d'une fenêtre redimensionnable), placement calculé lu dans le journal.
- Daytona, avant : l'image remplissait toujours la fenêtre (le `Present` de DXGI étire sur toute la fenêtre
  de sortie). Après : `display.cpp` donne à la chaîne d'échange une fenêtre enfant (`wl_display_child`,
  transparente à la souris, jamais effacée) placée à chaque `Present` sur le rectangle de l'image ; la
  fenêtre du jeu (WS_CLIPCHILDREN) peint les bandes. Une seule mise à l'échelle, sans shader.
  `WINDOWSLOADER_SWAP=discard` la retire (secours). Direct3D 9 garde le rectangle de destination de `Present`.
- Résultat, identique pour les deux jeux (écran 16:10, jeu 16:9) : WINDOW à la forme de l'aspect ;
  WINDOW_STRETCH et FULLSCREEN_STRETCH remplis ; FULLSCREEN ORIGINAL/16:9 bandes 96/96 px, 4:3 bandes 255/256
  sur les côtés, 4:3_LETTERBOX image 16:9 entière dans le cadre 4:3 ; fenêtres réduites remplies (forme de la
  fenêtre), LETTERBOX réduit avec bandes symétriques.

### 21/10 — Essai au volant, option `--controllers` (demande 26)

- Préavis vJoy envoyé avant l'essai aux sessions Compilation, Driveboard, Multi-Board et Linuxloader FFB
  edition (Multi-Board et Linuxloader : vJoy libre, et inutile de les prévenir à l'avenir ; gardé tant que
  l'utilisateur ne change pas la règle), puis « vJoy libre » à la fin aux deux autres.
- Premier lancement normal : SDL ne voit qu'une manette, `vJoy - Virtual Joystick` (joueur 1, retour de
  force 100 %), aucun volant physique sur ce PC. Avec les liaisons par défaut, seul l'axe X de vJoy est
  branché (le volant) ; les pédales ne le sont que sur les gâchettes d'une manette.
- « Envoie-toi les commandes » : le mode agent ne démarrait pas SDL, donc aucune force ne partait vers vJoy.
  Nouvelle option du lanceur `--controllers` (avec `--agent`, variable `WINDOWSLOADER_CONTROLLERS=1`) : la
  DLL démarre les manettes et leur retour de force ; les entrées que l'agent tient (`set`) gardent la main,
  les manettes ont les autres. `python scripts/agent/day_race.py wheel` : course complète, effet de force
  constante créé et lancé sur vJoy sans erreur et mis à jour à chaque force du jeu (72 à 268 changements par
  phase ; volant à gauche → −0,39 en moyenne), neuf changements de rapport, F10. Les mises à jour de l'effet
  ne sont pas vérifiées une à une, et ce que reçoit le programme derrière vJoy n'est pas visible d'ici.

### 21/10 — Écran éteint

- Pendant la vérification de l'affichage, Daytona plantait à son démarrage (lecture de NULL à
  +0x2b4465) : l'écran du portable était éteint, Windows ne montrait plus que « WinDisc » et aucun
  adaptateur n'avait de sortie (ni l'Intel, ni la NVIDIA). Le jeu prend la première sortie de son adaptateur
  sans vérifier : sans écran il ne peut pas démarrer, sur une borne non plus. `d3d11hook.cpp` l'écrit
  désormais au journal (« no screen on any adapter… »). Écran rallumé : 2048×1280 sur l'Intel, tout repart.

### 22/10 — Ford Racing Full Blown (demandes 31 à 39)

- Copie locale (333 Mo) ; plugin FFB, `teknoparrot.ini`, dgVoodoo et jconfig mis de côté. Le jeu
  (`FordRacing\fordracing.exe`, Direct3D 9) tourne sans son Shell, en free play.
- **inpout32 importé statiquement.** Au premier essai, la vraie `inpout32.dll` du dossier s'est chargée
  (aucun pilote installé, pas administrateur : rien atteint). Désormais le lanceur, sur le processus
  suspendu, réécrit le nom de cette importation (`inpout32.dll`, `inpout32_d.dll`, `inpoutx64.dll`) par le
  chemin de la DLL de windowsloader, qui exporte `Inp32` @1, `Out32` @2, `IsInpOutDriverOpen` @3
  (`wlhook.def`), comme Detours ; la chaîne est allouée près de l'image.
- **Carte d'E/S** : Happ UGCI, USB HID VID 0x078B PID 0x0010, rapports de 8 octets (octet 1 volant,
  3 accélérateur, 5 frein, 6 boutons). Bits trouvés en course : 0 rapport +, 1 rapport −, 2 Start, 3 vue.
  Étalonnage lu dans `data\calibration.ini`.
- **Retour de force** : `Out32(0x378, b)`, n = trunc(16 · Strength · f) borné à ±15, b = 0x10 − n
  (n > 0) ou n + 0x1F (n < 0). Volant tenu à droite : force moyenne +0,12 (vers la gauche), à gauche
  −0,28. Avec `--controllers` : course complète, effet envoyé à vJoy (`fr_race.py wheel`, sessions
  prévenues avant et après).
- **Aspect** (demande 35) : nouvelle règle, voir la demande. FPS et `[nom]` dans le titre (demandes 36
  et 38) : mis par `WM_SETTEXT` une fois par seconde depuis le hook de `Present`.
- **Perte du device** : pendant un essai, la connexion Bureau à distance de l'utilisateur a changé la
  résolution de l'écran (1920×1080 → 2048×1280) ; Direct3D 9 a perdu son device (`Present` →
  0x88760868) et Ford Racing ne s'en remet pas (1 image par seconde). Rien à voir avec le titre ni vJoy :
  le même essai repassé après est juste.
- **Menu de service** : il est dans `GameShell\GameShell.exe` (« Game Shell - Windowed », Hollywood
  Gaming / Sega Europe). Lancé sous windowsloader, le module Ford Racing le reconnaît (même carte,
  bits 4 et 5 pris pour Test et Service). Il exige `/k=<CRC>` de son lanceur (« SECURITY FAILURE 2 »
  sans) : un CRC-32 MSB, polynôme 0x8E94A031, graine 0x6F5A87D5, sur tout `GameShell.exe` (0x727DB500
  ici) ; le module l'ajoute à la ligne de commande. Il s'arrête ensuite sur « SECURITY FAILURE 4 » : pas
  de pilotes iButton (`IBFS32.DLL`, clé DS1991). Non émulé : il faudrait le contenu de la clé.
- **Protections ajoutées** (GameShell les appelle) : `SetLocalTime` / `SetSystemTime` refusés ; les clés
  ouvertes sous HKLM le sont sans droit d'écriture (il écrit l'adresse IP de la carte réseau). Déjà
  refusés : `ExitWindowsEx`, programmes lancés (son `fordracing.exe` aussi).

### 22/10 — Initial D Arcade Stage 6 AA, la plateforme RingEdge

- Copie locale (4,2 Go, 3 840 fichiers, 5 min 30 : c'est le nombre de fichiers qui fixe la durée sur ce
  partage). `id6_dump_.exe` est l'exe déchiffré ; il n'a plus de table des noms d'importation :
  `wl_patch_iat` reconnaît désormais l'entrée à l'adresse de la fonction (le DLL plantait au chargement).
- Nouveaux cadres, réutilisables : `vdev.cpp` (périphériques `\\.\…` et ports série émulés : handle à
  nous, ReadFile / WriteFile / DeviceIoControl / réglages série servis par le module), `pcp.cpp` (serveurs
  PCP, le protocole texte d'amLib : invite `>`, `clé=valeur&…`), `jvs.cpp` (carte d'E/S JVS : paquets
  E0/D0, commandes F0, F1, 10–15, 20–22, 2F, 30–32 ; ligne de détection par GetCommModemStatus comme
  OpenParrot), `driveboard.cpp` (carte de pilotage Sega : trames de 4 octets, octet d'état ; 0x84 =
  couple), `glhook.cpp` (SwapBuffers : images, captures par glReadPixels, titre).
- Pilotes : `mxsram` (géométrie de segatools, Unlicense), version d'ABI 0x100 ; `columba` lit la mémoire
  physique : amOemstring cherche l'ancre `_DMI_` en F0000–FFFFF et veut 64 Ko entiers à l'adresse de la
  table (table placée en E0000) ; plateforme par la chaîne OEM n° 3 (« AAL » RingEdge, « AAM » RingWide).
- Services PCP : mxmaster (40100 : `foreground.getcount`…), réseau (40104 : `request=…`, réponse
  `response=…&result=success&…`), clé (40106), gcatcher (40110 : `isupdate`). La réponse reprend la
  commande et sa valeur. Le `connect` non bloquant du jeu lit WSAEWOULDBLOCK : les accroches de sockets
  gardent maintenant le code d'erreur (le jeu fermait et recommençait).
- **La clé** : « Error 0949 Game Program Not Found » venait de ses défis. `keychip.ds.compute`
  (7 octets, page 0–3) et `keychip.ssd.proof` (16 octets) sont vérifiés contre `SBUU_Table.dat`, que le
  jeu charge par blocs : 10 000 enregistrements DS de 87 octets (défi, puis 4 réponses de 20 octets, une
  par page) puis 10 000 SSD de 32 octets (défi, réponse de 16). Les octets des réponses sont permutés dans
  le fichier ; l'ordre a été trouvé en comparant la mémoire du jeu au fichier. Une fois sur dix le défi
  est tiré au hasard et non vérifié.
- **L'image** : noire sur l'Intel UHD (shaders Cg/ARB de 2007). Le lanceur met `SHIM_MCCOMPAT=0x800000001`
  (sélection NVIDIA d'Optimus, par processus, sans réglage du système ; `WINDOWSLOADER_GPU=default` l'évite) :
  RTX 3050, 60 i/s, le jeu s'affiche.
- Jeu : réglage du volant, « Network error 8301 » (hors ligne), carte (non), marque, AE86, boîte, course.
  Retour de force : couple 0x84 en course. Menu de service : `id6_dump_.exe -t` (Test dans le jeu le
  quitte vers ce programme : `mxmaster.foreground.next=3`).
- Protections ajoutées : `SetTimeZoneInformation` refusé (le jeu l'importe).

### 22/10 — GRID

- Copie de tout `C:\Sega` (Grid, shell, ShellData). GRID créait `C:\Sega\ShellData\DataCache\replay` à la
  racine du disque de ce PC (dossiers vides, mis à la Corbeille) : `wl_files_redirect` (files.cpp) fait passer les
  chemins sous un dossier fixe vers un autre (CreateFile, CreateDirectory, GetFileAttributes(Ex), FindFirstFile,
  DeleteFile, RemoveDirectory, SetFileAttributes, CopyFile, SetCurrentDirectory, les .ini).
- Le jeu trouvait vJoy par DirectInput, y créait un effet de force constante, puis plantait (+0x79ce93) : la
  manette virtuelle « SEGA IO Controller » de Daytona (vdinput) le remplace et cache les vraies.
- Ensuite il s'arrêtait exprès (+0x1ab9e5, `mov [eax], eax` avec eax = 0) : correctif repris d'OpenParrot
  (`Europa-R/GRID.cpp`, GPL v3), posé par `wl_patch_code`, qui vérifie les octets d'origine.
- Rendu à la taille du bureau, 3072×1920 : manque de mémoire en course (captures refusées) ; EnumDisplaySettings
  accroché par le module. Retour de force : port 0x1020, comme Sega Rally 3.

### 22/10 — DiRT Showdown

- Même organisation que GRID, module commun `codemasters.cpp`. Le pilote audio Rapture3D plantait sans ses
  réglages dans le registre : `wl_system_refuse_dll` le refuse (OpenAL prend DirectSound). La coquille Sega
  Europe est partagée avec Daytona (`segashell.cpp`, trames « DSW », accusé de LAUNCHED_OK). Image 10 bits
  convertie pour les captures.
- Commandes non faites : le jeu énumère « SEGA IO Controller » sans jamais créer de périphérique.

### 22/10 — K.O. Drive, la plateforme RingWide

- `ringedge.cpp` devient une table de jeux (exes, gameid, table de la clé, plateforme, version des pilotes,
  ports JVS, entrées). K.O. Drive : gameid SBXA, plateforme AAM, `M-DriveR_RingWide.exe` et `TestMode.exe`
  (reconnu quand l'exe du jeu est à côté).
- amLib plus ancienne : version des pilotes 1 dans le mot bas (amSramInit +0x7bb2f3), taille de secteur de la
  SRAM (0x9C406004, une division par zéro sinon), Super I/O : amHmGetLPCChipId lit l'identifiant de la puce
  (registres 0x20-0x21 : 0xA020 ou 0xA23x) et l'adresse du moniteur matériel (registre 0x48, 0x10 à 0x6F).
- JVS par `\\.\mxjvs` : DeviceIoControl 0x9C402000, la requête en entrée, la réponse en sortie (en recouvrement :
  la longueur est lue dans OVERLAPPED.InternalHigh) ; `TestMode.exe` passe par COM4 (`wl_jvs_add_port`).
- SMBus `mxsmbus`, trouvé par SetupDi (interface {5C49E1FE-3FEC-4B8D-A4B5-76BE7025D842}) : la recherche
  d'interfaces de devices.cpp sert maintenant tout périphérique émulé (`wl_vdev_interface`), plus seulement la
  carte HID. Requêtes de 37 et 39 octets {état, protocole, adresse, commande, longueur, données} : 3 sonde,
  4/5 écriture/lecture d'octet (PCA9535 à l'adresse 0x20), 8/9 écriture/lecture de bloc (EEPROM, offset sur
  16 bits, gardée dans `wl_eeprom.bin`). Les enregistrements système (AM_SYSDATA_*) y sont : « broken, use
  default » au premier lancement. PCA9535 : port 0 = DIP switches (lus inversés), port 1 bits 2 et 3 = boutons
  Test et Service de la carte, actifs à 0 (la valeur 0 les montrait enfoncés : le menu de service ne
  répondait pas).
- **La clé** : « Error 0949 Keychip not found » sans `SBXA_Table.dat` (absente de racecab1 comme des copies
  TeknoParrot). amDongleSetAuthConfig (+0x7a4da0) accepte le chemin « toolmode » : le pointeur du jeu vers son
  chemin de table est déplacé sur ce mot, trouvé dans l'image (motif `C7 05 … <adresse du chemin>`), seulement
  si la table manque.
- **La carte de pilotage** : même trame que Initial D, mais le jeu lit 4 octets de réponse (`80 00 00 00`
  suffit). Son test de démarrage (+0x4a50d0, états FFBW_INIT…) donne un couple +8 puis −8 et compare le signe
  de la position du volant : sans volant qui tourne, « Error 1001 Force Feedback Error ». Entre la remise à zéro
  (FF) et le gain (83) qui le terminent, windowsloader joue un volant libre (couple positif → volant à gauche).
  En course : 84 (chocs), 85 1E 0A (vibrations d'une ou deux trames, rendues en alternant le sens), 86 06 09
  (amortisseur, journalisé). Le journal de la carte note maintenant chaque changement de valeur par commande.
- Entrées : le bouton unique START/WEAPON est l'octet 0 bit 1 (trouvé en essayant chaque bit, confirmé par le
  test d'entrées du menu de service). Course complète au clavier de l'agent et avec vJoy.
- Affichage : les captures d'écran de ce PC montrent les fenêtres Direct3D 9 noires, y compris un petit
  programme d'essai (fond rouge) lancé sans windowsloader ; le placement est vérifié dans le journal. Une
  boîte du pare-feu Windows pour « DiRT Showdown Executable » attend une réponse au milieu de l'écran (laissée à
  l'utilisateur : autoriser ou refuser change un réglage de sécurité).

### 22/10 — Sonic & Sega All-Stars Racing

- Bloqué d'abord à 7 s : il lit la carte du volant tant que ClearCommError annonce des octets ; la carte en
  annonçait toujours un. Elle garde maintenant une réponse par trame reçue (une lecture sans réponse en attente
  rend encore l'état, ce qu'attendent Initial D et K.O. Drive, qui ne demandent pas ClearCommError).
- Son test « KICKBACK » (+0x41b280) : état 1 attendu après 81/82, état 0 après FD, couple +8 jusqu'à la butée
  (position stable 32 images), −8 jusqu'à l'autre, écart ≥ 0x80, puis gain 83 et centrage. Échoué, il essayait
  COM2, COM3 puis COM4… le port JVS, où il écrivait le protocole du volant.
- Réseau : état DHCP mémorisé (update_dhcp_status), lien de la carte réseau par jeu (K.O. Drive et Initial D
  gardent 1). Les sockets de liaison sur l'adresse de la borne : `bind` passe sur 127.0.0.1 (system.cpp), les
  envois vers ce réseau sont jetés.
- Crédits : les enregistrements système de l'EEPROM (adresse 0x57) ont un CRC32 sur leurs octets 4 et suivants
  (32 octets pour CREDIT, 0x50 pour celui du jeu « HVBS » en 0x1000). Jeu gratuit = octet 0x0A de CREDIT
  (lu dans `gametest.exe`, +0x404357).
- Reste : pourquoi les pièces ne sont pas comptées et Start ignoré.

### 22/10 — Battle Gear 4 Tuned (Taito Type X+)

- Image borne (session Berstett) donne un feu vert permanent pour copier depuis racecab1 (une copie à la
  fois) ; copie de BG4 : 11 min 45 à 7,3 Mo/s, plus lente que son modèle (21 Mo/s).
- OpenParrot (`TypeX2/TypeX2Generic.cpp`, GPL v3) lu comme référence : lecteurs D: et E:, carte du volant sur
  COM1 (protocole Taito : 0x20 remise à zéro → A0 00 ; 0x11 positions du volant ; 0x1F arrêt du moteur →
  1F 00 ; sinon 8C A0), correctifs IntroFix et « windowed off ».
- Trouvé en suivant l'initialisation (sondes provisoires sur chaque étape) : la sortie immédiate venait de la
  caméra RenderWare, dont le raster ne tient plus dans une zone client rétrécie par la barre de menu.
- JVS Taito : CTS à 0 tant que la carte n'a pas d'adresse ; requête « 20 01 03 21 02 22 08 32 03 … 33 08 … 65 00 ».
- Reste : écran noir après le chargement des polices (le jeu tourne à 33 i/s, interroge le JVS, rien d'autre).

### 22/10 — Chase H.Q. 2 (Taito Type X2)

- Même famille que Battle Gear 4 (RenderWare, JVS COM2, D:\). Requête JVS : « 20 01 03 32 03 … 33 06 …
  21 01 22 0a ». Back buffer A2R10G10B10 : la capture Direct3D 9 le ramène à 8 bits.
- Sans le saut de l'étalonnage (+0x107e3, d'OpenParrot), image noire à 12 i/s ; avec, le jeu écrit ses
  fichiers RTC, crée DirectPlay8 (Peer, Address), puis plante (+0x344f) : un modèle chargé par +0x67c50 est nul.

### 22/10 — Les jeux qui ne s'affichaient pas (demande 40)

- Recopie GDI pour Direct3D 11 aussi : back buffer lu par une texture de transfert (MSAA résolu ; RGBA,
  BGRA, 10 bits convertis), dessiné dans la fenêtre du jeu ; la chaîne d'échange présente dans sa fenêtre
  enfant cachée, sans attente (DwmFlush par intervalle demandé) et le jeu reçoit S_OK au lieu de
  DXGI_STATUS_OCCLUDED (en Direct3D 9, S_OK au lieu de S_PRESENT_OCCLUDED). DiRT Showdown passe de 23 i/s,
  fenêtre blanche, à 60 i/s, image vue.
- Battle Gear 4, noir sans aucun dessin (sonde : Clear seulement, ni Draw ni DrawUP) : remonté de tâche en
  tâche par les classes RTTI (`bg4::CPreLoad` jamais lancé, la tâche de démarrage en état 2 attend
  `bg4::CAppSound`). Le son ne démarrait pas : d'abord GetForegroundWindow (+0x255bf0 exige que la fenêtre
  du jeu soit devant), puis DSERR_ALLOCATED sur le format primaire en 8 voies (GetSpeakerConfig donne 7.1 ici).
  OpenParrot a d'ailleurs son `DSoundWrapper.cpp` pour BG4 (7.1 annoncé, format forcé en 2 voies).
- Puis « I/O ERROR » et TEST MODE : le jeu envoie la commande JVS 0x34 (texte de l'afficheur) ; la carte
  répondait « commande inconnue », le jeu fermait COM2 et recommençait 3 s plus tard. Ajoutée.
- La carte du volant (COM1) suit le protocole Taito d'OpenParrot (2 octets par commande, 2 en réponse, position
  en 1024..2044) : la carte de Sega ne lui répondait pas et le jeu quittait 5 s après l'avoir ouverte.
  `BattleGear4InputMisc.cpp` d'OpenParrot (BG4General) : drapeau de pause +0x42d29c tenu une fois l'écran 8 vu
  (repris), pistes toutes ouvertes (+0x4165c0, pas repris : option de TeknoParrot).
- Chase H.Q. 2 : ses données sont ouvertes par `E:.\data\…` ; la redirection de lecteur ne prenait que
  `E:\…`. Avec ses polices et modèles, il ne plante plus. Puis 1 i/s : un échantillonneur de piles (fils du
  jeu suspendus, adresses de retour rangées par module) montre le fil principal dans
  IDirectSound3DListener::CommitDeferredSettings → MMDevAPI → AudioSes → RPC, plus d'une seconde par appel ;
  au-delà de 100 ms les validations suivantes sont sautées : 60 i/s.
- Outils de l'enquête (bloc-notes de la session) : `sampler.py` (piles des fils d'un jeu lancé),
  `screen_game.py` (capture de l'écran réel), `cls.py` / `rel.py` / `writes.py` (constructeurs, appelants,
  écritures d'un champ).

### 22/10 — Commandes de Battle Gear 4 et Chase H.Q. 2

- Méthode : le test d'E/S de chaque jeu, une entrée tenue à la fois (`bg4_io.py`, `chq2_cal.py`, sondes provisoires
  sur les bits et les voies analogiques). Pièges : l'écran de BG4 ne suivait pas parce que le bus JVS
  recommençait (commandes Taito 0x6D..0x70 inconnues) ; pour l'agent, `set Steering +1` est le volant tout à
  **gauche** (inputs.cpp) : un essai de BG4 a d'abord semblé montrer le volant inversé, c'était ma lecture.
- Les deux jeux lisent 3 octets d'interrupteurs ; Chase H.Q. 2 lit 10 voies analogiques et commande son moteur
  par les sorties générales (couple sur 4 bits et sens). Sorties analogiques (0x33) et générales (0x32) passées au
  module du jeu.
- Chase H.Q. 2 étalonné dans son menu de service (volant, pédales, SAVE AND EXIT) ; `rtc.dat` du dossier du jeu
  (copie locale) réécrit par le jeu.
- Restent : vJoy pour les deux, les boutons non vus dans un jeu réel (levier H de BG4 si la borne en a un).

### 22/10 — DiRT Showdown : commandes et retour de force

- La manette « SEGA IO Controller » énumérée mais jamais ouverte : sonde sur la mise à jour des périphériques
  (+0x9706c0, adresse relative au module : Showdown est relogé, ASLR) : la liste restait vide. Le rappel
  d'énumération ne garde que ce que son test XInput (+0x961df0) accepte : il cherche dans raw input un HID de
  même VID/PID sans « IG_ » et rend −1 s'il n'en trouve pas. Réponse générique dans `vdinput.cpp`.
- Les appuis sur Start partaient bien vers la coquille (GAME_MSG_EVENT 12 01 / 12 00), sans partie : avec un
  crédit annoncé (essai : 9), CREDITUSED puis GAMESTART. La coquille annonce désormais un crédit en jeu gratuit
  pour Showdown.
- Moteur : Out32 port 0x378 (et non 0x1020 comme GRID), même codage ; sens vérifié par corrélation avec le volant.
- Sonic mis de côté : les pièces arrivent au jeu (compteur JVS de l'emplacement 1 à 2) sans être comptées, et Start
  est ignoré en jeu gratuit ; amCredit 1.04 est lié dans l'exe, sans symboles. À reprendre.

### 22/10 — GTI Club 3, préparation (copie pas encore faite)

- Demandé par l'utilisateur (« As tu déjà fait gti club ? ») : pas encore ; les copies depuis racecab1 sont
  suspendues par la session Image borne pendant sa campagne link (environ 2 h 30), jusqu'à son signal.
- Référence lue : OpenParrot `TypeX2/GTIClub3.cpp` (GPL v3). Pas d'émulation de carte : un crochet sur la fonction
  d'échange de la carte d'E/S Konami (`ac_io_hbhi_COMUNICATE`, +0x409c0) écrit les entrées en mémoire à chaque appel :
  boutons +0x67e784 (mot ; 0x200 volant à gauche, 0x400 à droite, 0x20 accélérateur, 0x40 frein, en plus des
  boutons), valeurs du menu de test +0x518c8c/90/94 (mots), volant / accélérateur / frein en flottants
  +0x518c80/84/88 (−1..1, 0..1), nouveaux appuis en 0xa7e788 ; quatre pointeurs de fonction (+0x4c0e0c, 0e14, 0e1c,
  0e3c) remplacés par une fonction qui « active les entrées » (+0x67e75b = 1) ; chemins `.\EU` et `.\FU` (dossiers
  EUP, FUP) ; réseau EA3/AVS (`libavs-win32-ea3.dll`) gardé, un échec de bind toléré. Rien sur le retour de force :
  à trouver dans le jeu (le plugin FFB de TeknoParrot lit sans doute la mémoire).

### 22/10 — GTI Club 3

- Image borne confirme la fin de sa campagne et rend son feu vert permanent. Copie en 3 min 29 (16 Mo/s).
- Premier lancement sans module : « ERROR CODE : 5 1500-0000 » (libacio cherche sa carte sur COM2). Le plan
  d'OpenParrot appliqué (quatre vérifications passées, entrées écrites à chaque échange) : démo, menus.
- Le menu de service ne bougeait pas : les entrées étaient écrites avant l'échange, et la remise à zéro des E/S
  du menu éteint le drapeau « entrées actives ». Après l'échange et le drapeau remis : il répond. Piège : un appui
  tenu 1 s fait défiler plusieurs lignes (répétition) ; navigation à 0,15 s.
- Une connexion Bureau à distance de l'utilisateur (pour la boîte du pare-feu) pendant un démarrage a fait
  échouer la création du device Direct3D (« Could not find any compatible Direct3D devices ») : relancé.
- Le retour de force d'après FFBArcadePlugin (adresses lues, sens vérifié par corrélation avec le volant).
- `race_check.py` : une image manquante (écran de chargement) n'arrête plus l'essai.

### 22/10 — Réglages de linuxloader : recadrage, courbure, fichier par jeu, `--set` (demande 43)

- Repris de linuxloader (`blitStretching.c`, `config.c`, `guiCommon.cpp`), mêmes noms et même sens pour que le
  front-end passe les réglages de la même façon :
  - `ASPECT_RATIO` prend aussi `4:3_STRETCH`, `16:9_STRETCH`, `4:3_CROP`, `16:9_CROP` (« 4:3 crop » avec une
    espace est lu aussi) ; `CROP_POSITION` −100..100 ; `CROP_BARREL`, `BARREL_CENTER` (60), `BARREL_RADIUS` (50).
  - La courbure : les calculs de linuxloader (`barrelSetup`, `barrelArc`, `barrelSource`, marqués) ; au lieu des
    bandes de quads texturés, chaque colonne (ou ligne) du cadre prend la colonne de l'image que donne la formule,
    puis `StretchDIBits` met l'autre axe à l'échelle. Seulement dans la recopie GDI.
  - Fichier : `settings.cpp`. `<nom court>.ini` à côté de `windowsloader.ini` est lu à sa place s'il existe et
    si le fichier s'appelle `windowsloader.ini` (comme linuxloader : un `-c` d'un autre nom est lu tel quel).
    Le lanceur prend `-c`/`--config` et `--set` (répétable, aussi `--set=`), passés à la DLL par
    `WINDOWSLOADER_INI` et `WINDOWSLOADER_SET`. Les réglages sont lus après la reconnaissance du jeu.
  - Menu F10 : la liste des sept choix, le curseur de position, la case de courbure et ses deux curseurs,
    appliqués à l'image suivante ; boutons « Sauvegarder », « Sauvegarder pour ce jeu », « Fermer » en bas du
    menu. Les changements ne sont plus écrits tout de suite (comme linuxloader) ; F11 ne change pas le
    `DISPLAY_MODE` enregistré.
- Le menu au-dessus d'une image recadrée ou courbée : sa partie de l'image est redessinée sans recadrage ni
  courbure, à l'échelle du recadrage et centrée sur le cadre, là où la souris est lue ; le menu reste entier.
- Agent : `display` prend aussi `CROP_POSITION=…`, `CROP_BARREL=…`, `BARREL_CENTER=…`, `BARREL_RADIUS=…` et
  n'enregistre plus rien ; `menu save [game]` appuie sur les boutons du menu.
- Trouvé en essayant : Battle Gear 4 refait son device à la taille de sa fenêtre (à la façon de DXUT :
  `GetClientRect` après chaque `WM_SIZE`). Avec un rapport d'aspect différent du sien, la fenêtre grandissait à
  chaque tour (800x600 puis 1067x600, 1067x800… jusqu'à l'écran). Défaut antérieur, montré par l'essai : le
  `GetClientRect` du jeu (ses imports) et le `WM_SIZE` qu'il reçoit donnent maintenant la taille de son image.
- Essais (captures de l'extérieur, la fenêtre du jeu remontée sans prendre le clavier) : les 16 cas et le menu
  sur GTI Club 3, Battle Gear 4 et DiRT Showdown ; `--set` en minuscules, « Sauvegarder pour ce jeu » qui
  copie `windowsloader.ini` en `gticlub3.ini` puis y écrit, ce fichier relu au lancement suivant, `-c autre.ini`
  lu tel quel. Le `gticlub3.ini` d'essai mis à la Corbeille.

### 22/10 — Wasteland Racers 2071, préparation (copie interrompue)

- Trio Tech, 2006, `Arcade.exe` 32 bits (Direct3D, fmod, SDL2). Copie depuis racecab1 dans `games/wasteland`,
  arrêtée à 2529 fichiers sur 4860 (1,47 Go sur 1,7 Go) pour un redémarrage du PC : relancer robocopy (depuis
  PowerShell) la termine, après avoir prévenu Image borne.
- Carte d'E/S : `SDC2.dll` (exports `Init`, `Shutdown`, `IsConnected`, `GetAxisData`, `GetDigitalIn`,
  `GetCoin`, `SetCoin`, `ClearCoin`, `SetForce`, `SetSteeringPos`, `SetDigitalOut`, `SetDGain`, `GetModel`,
  `GetSerial`, `GetFirmware`, `GetErrorCode`), un volant à retour de force sur COM3 (`ForceFeedbackWheel.xml`,
  `SerialPort 3`). `Inputs.xml` : axe X volant, Y accélérateur, Z frein ; boutons 0 BOOST, 1 CAMERA, 2 à 5
  CHEAT1-4, 7 GEAR, 8 COIN, 11 MAINTENANCE, 12 STOP ; sorties BOOST, CAMERA, CHEAT1, ACTIVEBOOST, BRAKES.
- Retour de force : FFBArcadePlugin (`Game Files/WastelandRacers2071.cpp`) remplace `SetForce` (force =
  argument / 32768) et `SetSteeringPos` (position / 128000) de `SDC2.dll`. Le module de windowsloader pourra
  remplacer les exports de `SDC2.dll` de la même façon, entrées comprises, sans COM3.
- Le `dinput8.dll` du dossier est celui de FFBArcadePlugin (avec `FFBPlugin.ini`, GameId 83) : à tenir à
  l'écart (comme l'import d'inpout32 réécrit par le lanceur, ou un `LoadLibrary` redirigé vers celui du système).

### 22/10 — Wasteland Racers 2071 (demandes 48 et 49)

- Copie : reprise après le redémarrage ; les deux sessions s'étaient effacées l'une devant l'autre, Image borne a
  tranché (« celui qui a une copie commencée la termine »). 4 860 fichiers, 0 échec.
- Lanceur : un composant de Windows dans le dossier du jeu (`dinput8.dll`, `d3d9.dll`, `winmm.dll`…) est le relais
  d'un autre loader : l'import du jeu est réécrit vers celui de Windows (comme pour inpout32). Ici le relais de
  FFBArcadePlugin, qui vise vJoy.
- Premier lancement : le jeu a ouvert vJoy par DirectInput (créé, jamais acquis : Acquire 0x80070005, aucun effet,
  aucune force). Le volant DirectInput de windowsloader est maintenant le seul listé.
- Direct3D 8 : un crochet minimal (fenêtre, images comptées) a montré une fenêtre noire : comme Direct3D 9 en mode
  blt, rien ne s'affiche sur ce PC. Avec l'accord de l'utilisateur, d3d8to9 (BSD 2 clauses) téléchargé, relu (une
  boîte et une page web ouvertes si D3DX manque : retirées), compilé dans la DLL 32 bits : l'image s'affiche par la
  recopie GDI.
- Défaut trouvé : la vidéo d'attraction (DirectShow, quartz.dll) crée son propre appareil Direct3D 9 sur la fenêtre
  du bureau ; windowsloader l'a pris pour celui du jeu et a voulu placer la fenêtre du bureau (refusé par Windows :
  sa taille et son style sont restés normaux, vérifié). Corrigé : un appareil créé par Windows lui-même est laissé
  tel quel, et windowsloader n'adopte jamais une fenêtre qui n'est pas au jeu.
- Commandes : rien ne démarrait. Trois causes, trouvées dans le code et les scripts du jeu (`Data\Menu\Main\main.dsf`) :
  le mot des boutons est actif à l'état bas (le jeu l'inverse) ; les compteurs de pièces sont par bit
  (`GetCoin(8)`, le bit COIN), et le jeu gratuit y met le prix d'une partie ; les bornes élisent un serveur par
  diffusion UDP, refusée pour garder le jeu hors du réseau : la diffusion va maintenant à 127.0.0.1 (option du
  module, `wl_system_loopback_broadcast`) ; le jeu passait une longueur d'adresse fantaisiste (7 449 552).
- Course atteinte : écran titre → serveur → voiture → circuit → chargement (≈ 60 s) → course. Menu MAINTENANCE
  (bouton Test) vu. Retour de force : un ressort (asservissement en position), sens vérifié.
- La voiture plafonnait à 12 km/h : les pédales de la carte sont à l'envers (le jeu prend 1 − valeur) ; corrigé,
  75 à 106 km/h, le volant tourne du bon côté. F10 (cinq pages, pause) et vJoy (`--controllers`, forces envoyées)
  vus, après le « vJoy libre » de linuxloader et en l'annonçant.
- Battle Gear 4 Pro (profil TeknoParrot à part) : d'après Image borne, un doublon exact du dossier Tuned, seul
  `teknoparrot.ini` diffère (options « Professional Edition » d'OpenParrot, et pas de `[FFB Blaster]`) : pas de
  copie, à faire sur la copie de Tuned. OpenParrot (`TypeX2Generic.cpp`) : largeur 1366 en +0x1f4c6d et +0xa0536
  (ou une résolution choisie), « Hold Gear » = 10 octets NOP en +0x8adff.

### 22/10 — Mario Kart Arcade GP DX 1.18 (Namco System ES3), commencé

- Copie : 18 715 fichiers, 6,19 Go, 8 min 46, 0 échec (12 Mo/s ; Image borne : ce partage va de 6 à 16 Mo/s).
- `MK_AGP3_FINAL.exe`, x86 relogeable (ASLR : adresses depuis la base), Direct3D 10 (par d3d10.dll, sur
  Direct3D 11) et un appareil Direct3D 9Ex, DirectInput, JVS série, lecteur Banapass (`bngrw.dll`), ALL.Net
  (AMAuthd, Mucha), webcam (photos des joueurs), ShareK (contenus par le réseau).
- Garde-fous ajoutés avant tout lancement (généraux) : la caméra par Media Foundation refusée
  (`MFEnumDeviceSources`, `MFCreateDeviceSource`) ; un port série ou parallèle non émulé refusé au jeu (le jeu
  essaie COM0 à COM15, et ce PC a une paire com0com COM10/COM11 qui mène à un autre programme) ; la table de
  routage du PC refusée (`Create/Set/DeleteIpForwardEntry`) ; WinHTTP limité à ce PC (`WinHttpConnect` : le jeu
  vise `amk3-cn01.nbgi-amnet.jp`).
- Premier lancement : le jeu a pris vJoy comme volant DirectInput (exclusif, environ 2 s, avant de planter tout
  seul) ; linuxloader prévenue. Module `namco.cpp` : un volant DirectInput de windowsloader, vJoy caché.
- Plantage : ShareK passe par une classe COM .NET d'`ipccl.dll` que la borne enregistre (`RegAsm` dans
  `project.ps1`) ; windowsloader ne touche pas au registre : le client ShareK n'est pas démarré (+0x7e80d).
- Lecteurs F:\ et G:\ (system, Bookkeeping, DongleData, TestMode) : les dossiers et fichiers `F*` et `G*` de la
  copie (redirection par préfixe : `F:\system\x` → `Fsystem\x`).
- Affichage : la recopie GDI lit aussi une chaîne d'échange Direct3D 10 (ici le tampon se lit déjà en Direct3D 11).
  Le jeu dessine du noir à 60 i/s : il attend sa carte d'E/S.
- Port COM1, 19200 bauds : `BuildCommDCB "COM1: …"` refusé par Windows pour un port absent (lu sans le nom pour un
  port émulé) ; lectures et écritures par `ReadFileEx`/`WriteFileEx` (routines de fin par APC, maintenant servies
  pour les appareils émulés, une lecture sans données restant en attente jusqu'au délai de 600 ms). Le jeu lit des
  en-têtes de 3 octets et n'écrit rien : la carte parle la première. Ce n'est pas le JVS classique mais la carte
  « WinArc » de Namco (classe `clWaJvIo`, identité « NBGI.;WinArc;Ver2.01;JPN ») : protocole à retrouver dans le code
  du jeu (rien de public).

### 23/10 — Battle Gear 4 Pro (demande 52)

- Pas de copie : le dossier de Tuned sert. Le type de borne se lit dans le code : l'état 1 de la remise à zéro
  usine (+0x50f464) choisit « WIDE MONITOR & CLUTCH TYPE » quand `[[0x8d92a4]+0x4d8] > 1` ; cette valeur est
  le nombre de cartes JVS moins 1 (+0x600d8c ← `[0x8df904] - 1`), compté par l'adressage du bus (+0x60cb37 :
  F1 tant que la ligne de détection, CTS, reste basse). Une borne Pro a donc deux cartes.
- `jvs.cpp` accepte une seconde carte sur le même bus (`wl_jvs_add_node`) : l'adresse diffusée va à la première
  carte sans adresse, CTS monte quand toutes en ont une. Nœud 2 interrogé par « 26 02 » (2 octets d'entrées
  générales, nouvelle commande) et « 32 02 » ; le nœud 1 reçoit en plus la commande Taito 0x66 (2 octets).
  Les options d'un jeu lues après les réglages : `wl_game_on_settings` (appelé avant que le jeu ne tourne).
- Avec deux cartes et l'ancien `config.bin`, le jeu réécrit deux octets (0x74, 0x75) et sa somme, puis quitte
  seul après 3,5 s ; au lancement suivant il est en Pro (1360×768, « ITEM SELECT: SHIFT-SW ») ; sans remise à
  zéro usine. Retour en standard : de même dans l'autre sens.
- Test d'E/S (une entrée tenue à la fois, puis la table du test lue dans l'exe, +0x3ab0b8 : nom, type, masque
  du mot d'entrées) : SHIFT LEFT 0x08000000 et RIGHT 0x10000000 sur le nœud 2 (0x80, 0x40), SEN1 0x02000000 et
  SEN2 0x04000000 (0x20, 0x10, actifs bas), SHIFT UP et DOWN sur les bits 8 et 7 du nœud 1 mais actifs bas en
  Pro : au repos le jeu voyait les deux rangées, c'est-à-dire la 4e.
- Décodage du levier (+0x48adf2) : DOWN+LEFT 1, UP+LEFT 2, DOWN 3, UP 4, DOWN+RIGHT 5, UP+RIGHT 6 (marche
  arrière si la voiture n'a que 5 rapports) ; aucune rangée : point mort (le « Hold Gear » d'OpenParrot retire
  justement ce stockage, +0x8adff).
- Embrayage : rien sur les voies libres au test d'E/S. Le code des entrées (+0x41e80a) lit les mots analogiques
  du JVS depuis +0xde (accélérateur +0xe4 = voie 3, frein +0xe6 = voie 4, embrayage +0xea = voie 6) et
  l'embrayage seulement en course ; étalonné par ADJUSTMENT. En course : enfoncé, 0 km/h plein gaz ; relâché,
  la voiture part.
- Course en « 6速 MT » d'abord bloquée sur « シフト変更中 » (changement de grille, ne touchez pas le levier)
  40 s : la grille est motorisée ; le jeu pousse 0x80 puis 0x40 (sorties du nœud 2), 4 s chacune, jusqu'à lire
  SEN1 seul ou SEN2 seul (+0x41df70, +0x41e0a0), puis arrête le moteur. Grille émulée : elle suit la dernière
  commande ; changement immédiat ensuite, rapports 1 à 5 vus au tableau de bord.
- Revérifiés après les changements du JVS : Tuned en standard (AT, 100 km/h, retour de force) et Chase H.Q. 2
  (course, 137 mph). `config.bin` d'origine (standard) remis dans la copie ; celui de Pro, étalonné avec
  l'embrayage, gardé dans le bloc-notes de la session.

### 23/10 — La suite de la liste (demande 53), la nuit sans écran

- Copies (Image borne prévenue, « fini » à chacune) : Battle Gear 4 « Taito Type X+ » (2 895 fichiers, 3,6 Go, 8 min 28),
  D1GP (1 809 fichiers, 1,4 Go, 4 min 26), Valve Limit R (509 fichiers, 206 Mo, 1 min) ; OpenParrot, outputblaster,
  dgVoodoo et le plugin FFB laissés sur racecab1.
- **Plus d'écran depuis environ 3 h** : DXGI ne liste aucune sortie sur les trois cartes (Intel, NVIDIA, Basic Render),
  EnumDisplayDevices aucun écran attaché (un moniteur 1920×1080 virtuel seulement), et Windows met `apphelp.dll` devant
  MessageBox et IDirect3D9. Mario Kart DX dit « Could not find any compatible Direct3D devices », Battle Gear 4 Tuned
  (qui roulait à 1 h) quitte 0,1 s après Direct3DCreate9. Essais de jeux arrêtés, Linuxloader prévenu ; reste le travail
  sans écran.
- **Battle Gear 4 (2006)** : un autre exe que Tuned (horodatage 0x446d0efa contre 0x4675ef6f), les correctifs de taito.cpp
  étaient aux adresses de Tuned (ceux à octets vérifiés refusaient, mais le drapeau de pause de BG4General s'écrivait
  à l'aveugle). taito.cpp reconnaît maintenant l'édition par l'horodatage (`bg4` Tuned, `bg4eng` la première, nom
  d'OpenParrot « BG4_Eng ») ; pour la première : barre de menu +0x1b8030, IntroFix +0x46e3e / +0x46e57 / +0x471b5
  (le même motif que Tuned, retrouvé dans l'exe) ; ni BG4General ni la borne Pro. Premier lancement (écran déjà
  absent) : plantage +0x1a6247 à l'ouverture de COM2, à revoir avec un écran.
- **vJoy pris par Battle Gear 4** : les deux éditions énumèrent les manettes DirectInput et ouvrent la première en
  exclusif (vJoy ici) ; c'était déjà le cas dans mes essais de Tuned. `wl_dinput_hide_controllers()` (dinputhook.cpp) :
  les manettes du PC cachées au jeu sans manette de windowsloader à la place ; appelé pour Battle Gear 4.
- **Mario Kart DX 1.18** (étude de l'exe) :
  - la bibliothèque JVS du jeu (+0x8ab550, son hôte « NBGI.;WinArc;Ver2.01;JPN », WinArc est donc l'hôte et non la
    carte) ouvre **COM3** à 115 200 bauds (+0x8ac1b0), CTS bas tant qu'une carte n'a pas d'adresse (comme Taito) ;
    elle connaît les cartes DE-JV, FCA-1, MINI-JV, NeoJV, TSS-I/O… et vérifie « namco ltd. » et « Ver6 ». Démarrée
    par l'initialisation +0x601ec0, une fois un chargement fini (+0x75eb89) : jamais atteinte dans les essais de 15 s ;
  - **COM1** à 19 200 bauds est la carte du moteur du volant (classe clWaJvIo, fil +0x5f57d0) : elle parle en 3
    caractères (« C01 » moteur prêt, sans quoi le jeu ne bâtit pas ses commandes ; « C06 » arrêté ; « E00 »..« E20 »
    erreurs) ; le jeu envoie toutes les 8 ms, une fois prêt (+0x602ec4), 10 octets : FF FF, une position visée sur 14
    bits (0x2000 au milieu), la raideur d'un ressort 0..127, 0, un amortisseur 0..127, 0, une vibration −128..127, 0 ;
  - namco.cpp : la carte JVS sur COM3, la carte du moteur sur COM1 (« C01 » à la première lecture, ses commandes
    jouées en ressort vers la cible plus la vibration ; le sens à vérifier). Pas encore lancé avec un écran.
- **D1GP** (Taito Type X2, 2009) : JVS sur COM2, lecteur de cartes sur un autre port COM, `d:\bk_info\` et
  `d:\card_backup\` ; volant, pédales et boutons par DirectInput, réglés par un fichier `<nom du produit>.txt` (les
  espaces en « _ » ; boutons « x », ACCEL et BRAKE numéros d'axe, STEER0/STEER1, FFB ; le Logitech Driving Force Pro
  traité à part, +0x46c1b0) ; retour de force par Immersion (IFC23, `data/game.ifr`, `steer_align.ifr`), c'est-à-dire
  des effets DirectInput. Le jeu importe AddIPAddress/DeleteIPAddress.
- **Valve Limit R** (Taito Type X, 2005) : `launcher.exe`, Direct3D 9, DirectInput, COM1 et COM2, AddIPAddress /
  DeleteIPAddress, SetLocalTime (déjà refusé), et le registrar d'ATL (RegDeleteKey, RegDeleteValue).
- Garde-fous ajoutés (system.cpp) : AddIPAddress / DeleteIPAddress refusés comme la table de routage ; HKEY_CLASSES_ROOT,
  HKEY_USERS et HKEY_CURRENT_CONFIG traités comme HKLM (lecture seule) ; RegDeleteKey(Ex), RegDeleteTree refusés
  partout, RegDeleteValue sous ces racines.
- **Retour de force du volant DirectInput de windowsloader** (vdinput.cpp, `WlVirtualJoystick.ffb`) : effets
  constants, rampes, périodiques et ressorts tenus et joués ensemble sur la force de windowsloader (le ressort tire vers
  son point selon la position du volant ; amortisseurs, inertie et frottement laissés de côté) ; le volant se dit à
  retour de force (DIDC_FORCEFEEDBACK, X actionneur) et se montre aux énumérations « force feedback ». Pour D1GP. Pas
  encore essayé.
- **Crazy Ride** (UNIS, Unreal Engine 4, 64 bits) copié : 22 228 fichiers, 4,85 Go, 40 min (108 ms par fichier ; selon
  Image borne le débit en Mo/s ne dit rien sur ce partage) ; laissés sur racecab1 : les installateurs de pilotes
  `DrvWiz3` et le flasheur de la carte `M3BootLoader_Burn`. Le dossier `SysStart\Plugin` contient les scripts de la
  borne (EWF, ResetSys, résolution, réseau…) : à ne jamais lancer (windowsloader refuse déjà les programmes lancés
  par le jeu). Le jeu charge `Plugins\UsbPluginsDll_R.dll` par son nom et y demande 71 fonctions `BIND_*` (carte
  d'E/S Unis sur port série, volant HID à part, pièces, lampes, clé « SSNK », mais aussi `BIND_SetLocalTime`,
  `BIND_ResetMachine`, `BIND_SHDeleteFolder`, `BIND_SetSystemVolume`) : à fournir par windowsloader comme le SDC2 de
  Wasteland, celles qui touchent au PC sans effet. `BIND_GetIoData` (+0x1c13e0 dans le jeu) rend 17 octets, la
  dernière trame de la carte ; le jeu (+0x1b3b90) en lit des boutons dans les octets 2 et 3, deux valeurs 0..255
  dans les octets 6 et 7 (les pédales, sans doute) ; le volant par `BIND_GetSteeringWheelAngle`, son moteur par
  `BIND_SetSteeringWheelData` (4 entiers). À finir avec le jeu en marche (le sens de chaque bit).

### 23/10 — La clé de Crazy Ride, les options propres à chaque jeu (demandes 55 à 57)

- Options propres à chaque jeu (demandes 56 et 57) : forme convenue avec Linuxloader, qui fait la même chose de son côté :
  section `[Game.<nom court>]` (et non `[Game]`, qu'un fichier commun partagerait entre jeux), `--set Game.CLE=VALEUR`
  vers la section du jeu lancé, onglet « Options du jeu » (titre du jeu, « [Game.bg4] de windowsloader.ini, pour ce jeu
  seulement », « Aucune option pour ce jeu. »), case / liste / curseur entier, « (au prochain lancement) » et un rappel
  quand une valeur diffère de celle du lancement, les deux boutons d'enregistrement écrivent aussi la section. Chez
  windowsloader la table est dans le module du jeu (`wl_game_set_options`, game.h), chez linuxloader dans
  `src/loader/config/gameOptions.c`. Première table : Battle Gear 4 Tuned (CABINET, HOLD_GEAR). Vérifié par un lancement
  bref (sans écran) : les deux `--set` lus dans `[Game.bg4]`, borne Pro et patch posés, `config.bin` intact.

### 23/10 — Écran revenu ; Mario Kart DX en JVS ; passthrough série (demandes 58 à 61)

- Écran revenu à 10 h 22 (attente en arrière-plan). Page « Options du jeu » vue à l'écran sur Battle Gear 4 ; la liste
  plus étroite (45 % de la largeur), libellés « Standard » / « Pro », le détail dans l'aide. Un `--set Game.CABINET=PRO`
  d'essai a basculé le `config.bin` de la copie en Pro (le jeu le fait seul, puis se ferme) : remis en standard.
- Mario Kart DX 1.18 : la carte du moteur répond « C01 » ; à 51 s la bibliothèque JVS ouvre COM3, adresse la carte, lit
  son identité et ses fonctions, donne son hôte « WinArc », puis envoie des commandes de Namco : 0x70 suivi d'une
  sous-commande (0x18 « PL » puis 02 et 2 octets, ou 80 et 1 octet ; 0x15 et 0x16 et 2 octets), 0x78, 0x79, 0x7A et
  14 octets chacune (valeurs de 12 bits par trois : les lampes, semble-t-il), 0x7F seule. Le JVS de windowsloader passe
  maintenant les commandes 0x60..0x7F d'abord au module de la carte (`WlJvsBoard.vendor`) : namco.cpp les répond
  « fait » (rapport 01). Reste : la première, « 70 18 50 4C 14 D0 », envoyée 8 fois à l'initialisation (réessayée) ; à
  voir avec le jeu en marche. Rien de public sur ce protocole (seul indice : 0x70 0x02 date de la carte FCA, 0x70 0x03
  ses interrupteurs, forum arcade-projects).
- Passthrough série (demande de l'utilisateur, forme convenue avec Linuxloader, qui a déjà ces clés) : `[Emulation]
  EMULATE_JVS = false` et `[Paths] JVS_PATH = COM5` envoient le port JVS du jeu à une vraie carte ; `EMULATE_DRIVEBOARD
  = false` et `SERIAL_<n>_PATH` (n : le COM du jeu) les autres ports émulés (carte du volant, du moteur) ; « COM5 » ou
  `\\.\COM10`. Le jeu ouvre alors le port du PC lui-même, refus des ports non émulés levé pour lui. Menu : partie
  « Cartes » de « Options du jeu », une liste par port du jeu (« Émulée » ou « Vraie carte sur COMn », les ports que
  Windows connaît), au prochain lancement, écrite par les deux boutons. Vérifié : la carte du volant de Battle Gear 4
  (COM1 du jeu) envoyée sur COM20, absent : `CreateFile "COM1" -> \\.\COM20` (erreur 2), le JVS (COM2) toujours émulé.
  Pas d'essai avec une vraie carte : ce PC n'en a pas.
- Passthrough essayé sur la paire virtuelle COM10 / COM11 (l'utilisateur la prête pour ça ; créneau convenu avec
  Linuxloader, qui l'utilise aussi). Battle Gear 4 sur COM10, un script Python sur COM11 joue la carte :
  - carte du volant (`SERIAL_1_PATH = COM10`) : la même séquence de démarrage que la carte émulée (`20 00`, `1F 00`,
    `12 18` … `1A 05`), puis l'interrogation `00 00` (741 trames), le jeu jusqu'à l'attract ;
  - carte JVS (`JVS_PATH = COM10`) : la ligne de détection passe par les lignes de modem de la paire (RTS de COM11 →
    CTS de COM10) ; le jeu remet à zéro, adresse la carte, lit son identité et ses fonctions, puis l'interroge
    (1 766 requêtes en 60 s), l'attract normal. Avec un script qui répond 50 ms trop tard, le jeu redemande la réponse
    (`2F`, 520 fois) : c'est le jeu qui tolère, pas une erreur du passthrough. Une pièce donnée par le script à 40 s ne
    s'est pas vue à l'écran : pas creusé.

### 23/10 — Mario Kart DX, son démarrage (demande 65) ; les cartes dans F10 (66) ; le plein écran fenêtré (67)

- Les cartes dans F10 (demande 66) : chaque module déclare deux lignes (`wl_game_set_boards`, game.h), montrées sous le
  nom du jeu dans la page « Jeu » et notées au démarrage : « Controls on JVS board, Sega 837-14572, JVS 3.0 (COM4) »,
  « Motor board: Sega-like servoboard (COM1) » ; les autres familles : « Sega PC parallel-port driver » (Sega Rally 3,
  GRID, DiRT Showdown, Daytona, Ford Racing : un octet, la force en 4 bits et le sens), « Taito steering board, Battle
  Gear protocol », « Namco motor board », les sorties JVS de Chase H.Q. 2, l'ACIO de Konami, la SDC2 de Trio Tech, la
  carte de UNIS. Vu à l'écran sur Battle Gear 4. Linuxloader reprend les mêmes libellés.
- Plein écran fenêtré (demande 67) : `DISPLAY_MODE = BORDERLESS` et `BORDERLESS_STRETCH`, à la suite des quatre
  modes (mêmes noms et libellés chez Linuxloader). C'est ce que faisait jusqu'ici `FULLSCREEN` : une fenêtre sans
  bordure qui couvre l'écran, à la résolution du bureau. `FULLSCREEN` change maintenant la résolution de l'écran pour
  son plus petit mode qui contient l'image du jeu, comme SDL chez Linuxloader (`ChangeDisplaySettingsEx`, `CDS_FULLSCREEN`, rendue en revenant à une fenêtre et par
  Windows à la fin du jeu ; une taille refusée : la résolution du bureau). F11 va de la fenêtre au plein écran choisi
  (`BORDERLESS` si le mode choisi est une fenêtre). Vérifié sur Battle Gear 4 : BORDERLESS 2048x1280, l'image 1707x1280
  entre deux bandes, BORDERLESS_STRETCH l'image pleine. FULLSCREEN pas essayé à l'écran (il passerait celui de ce
  portable en 800x600 pendant l'essai). La capture « screen » de l'agent prend les pixels physiques d'un écran mis à
  l'échelle (150 %) : à corriger à part.
- Mario Kart DX 1.18 : il dessinait du noir parce qu'il restait dans sa vérification du démarrage (`clBootCheck`,
  douze étapes, +0x2044e0), sans rien charger (2 fichiers lus en 2 min). Sondes posées sur les étapes :
  - réponses JVS d'après ce que la bibliothèque du jeu lit (+0x4adf90, +0x4aefa0) : `70 18 PL` rapport 01 et un
    octet ; « 70 18 PL 14 D0 » du départ suivi de 6 octets restés dans son tampon (longueur fixe) ; un lien série dans
    la carte (`70 16` réglage, `70 05` envoi, `70 15` réception, compte 0) ; 3 octets de contacts par joueur (le bouton
    Mario, bit 0x02 du troisième, d'après TeknoParrot) ; la table des commandes de TeknoParrot (Entrée bouton 1, carte
    bouton 2, objet bouton 5, haut / bas) ;
  - étape 2, « ドライブ筐体 » : la clé de sécurité de la borne (un périphérique USB, interface
    {3ABF6F2D-71C4-462a-8A92-1E6861E6AF27}, données signées) ; +0x4b17b0 remplacée : « présente, vérifiée », numéro
    « 271011020001 » (2710 le jeu, puis un chiffre, un chiffre de 0 à 3, « 02 », 4 chiffres) ;
  - étapes 4 et 5, le lecteur Banapass (`bngrw.dll`) : ses requêtes rappellent une fonction (n° du lecteur,
    résultat, contexte) et le jeu attend ce rappel ; émulé (attaché, aucune carte), OpenBanapass d'OpenParrot ne
    rappelant pas ;
  - étape 8, la liaison entre bornes : diffusion UDP 20199, renvoyée sur 127.0.0.1 ; le jeu lance aussi `cmd.exe /c
    ipconfig /release` et retire la passerelle (refusés). Il veut en fait le réseau des bornes : son adresse en
    192.168.112.11..20 et une passerelle, prises dans `WSAIoctl(SIO_GET_INTERFACE_LIST)` et `GetAdaptersInfo` ; puis il
    change la route par défaut et pingue le routeur 192.168.112.254. Pour ses propres appels seulement (ses imports) :
    une interface 192.168.112.11, une passerelle, la route « changée » sans rien toucher, le routeur et la borne
    répondent au ping → « リンクOK » ;
  - étapes 9 et 10, le serveur : son authentification (`F:/system/last_auth.bin`, absente) prise pour récente, la
    limite de 36 h hors ligne (`boot_limit.bin`, novembre 2024) pour non atteinte → « オフライン », démarrage fini :
    `clSequenceDataStay` (chargement), `clSequenceNotice`, `clSequenceGameDemo`.
- Toujours noir : le jeu dessine en Direct3D 10 (le 9Ex ne dessine rien) et la recopie GDI lisait sa mémoire d'image
  par un contexte Direct3D 11, qui passe avant les commandes du jeu. Avec `PRESENT = DIRECT` l'attraction se voyait :
  lecture maintenant par l'appareil Direct3D 10 (recopie et images de l'agent). Le menu F10, dessiné par un contexte
  Direct3D 11, ne se voyait pas non plus : backend DX10 de Dear ImGui (le même 1.92.0, de sm2-emu), menu dessiné par
  l'appareil du jeu.
- Menu de test : Test est un interrupteur (un appui court entre et sort aussitôt) ; sélecteur haut / bas, Entrée.
  Commandes trouvées au test des contacts : objet 0x0020 (0x0010 aussi), Mario 0x0002 (pas un 3e octet comme le
  disait la table de TeknoParrot), le reste comme elle. Les voies analogiques, contre l'étalonnage du menu (fait dans
  la copie, volant centré, pédales relâchées) : -512..+512 pour le volant, gauche négatif. Piège de mes essais : la
  commande `set` de l'agent prend -1..1 (+1 la butée gauche), pas une valeur brute.
- Les réglages du menu ne s'enregistraient pas : `testmode_<n>.bin.tmp` jamais renommé, `_wrename` → `MoveFileW` sans
  la redirection de F:\ (seulement CreateFile, DeleteFile…). MoveFile(Ex)A/W redirigés (files.cpp).
- Retour de force : « ハンドル反力の使用 » était OFF dans la copie (réglages du jeu) : mis à ON, la carte moteur reçoit ses
  trames en course. Leur visée est au centre en 0x200 (pas 0x2000 : corrigé ici et dans DRIVEBOARD.md).

### 23/10 — Battle Gear 4 de 2006 (demande 65, suite)

- Les images de l'agent échouaient au premier lancement (« no frame ») : passager, plus revu ensuite ; le jeu présente
  à 30 images/s.
- « $WAIT FOR A WHILE » / « $ OUT OF SERVICE » en alternance sur l'afficheur JVS de 5 à 9 s : le démarrage qui attend la
  carte du volant, rien à corriger (le trafic JVS semblait s'arrêter : la limite de 500 lignes du journal par port).
- Plantage à 50 s dans la démo, lecture à l'adresse 0 (+0xeb838, IMediaControl::Stop sur un pointeur nul). Le lecteur
  vidéo du jeu (+0xebcb0) est l'exemple Texture3D9 de Microsoft : un rendu « TEXTURERENDERER » (RGB24, textures
  RenderWare), AddSourceFilter, FindPin « Output » ou sinon RenderFile. Programme d'essai à part (DirectShow, 32 bits) :
  le fichier se décode sur ce PC (lecteur ASF, « WMVideo Decoder DMO », WMV3). Sonde sur la fonction du jeu : elle
  reçoit `E:/data/Scene/demo/attract/attract_demo.wmv` et échoue en 0x80070003 (chemin introuvable) : le lecteur ASF
  ouvre le fichier depuis les DLL de Windows, sans passer par les crochets de fichiers, et E:\ n'existe pas. Tuned n'a
  pas ce fichier (il ne lit pas de vidéo).
- Corrigé dans `system.cpp` : à la création d'un FilterGraph, IGraphBuilder::RenderFile et AddSourceFilter reçoivent le
  chemin redirigé (`wl_files_redirectedW`, files.cpp), journalisés. La vidéo passe (400×256), le jeu tourne 90 s sans
  plantage, puis jusqu'en course. Sonde retirée.
- Course en mode agent (constructeur, voiture, AT, mode course, circuit, grille, départ). Menu de service : SHIFT UP
  descend, SHIFT DOWN remonte (aller sur EXIT et START ferme le jeu, comme une borne qui redémarre) ; test d'E/S :
  volant, pédales, VIEW, HAZARD, SIDE BRAKE. F10 : pause et pages. La page Jeu disait la seconde carte JVS de la borne
  Pro : texte propre à cette édition, sans elle.
- vJoy : le premier essai a chevauché celui de la session Linuxloader (Maximum Tune 3), les menus bougeaient seuls ;
  refait seul : course, 34 changements de force au joueur 1. `race_check.py` connaît maintenant `bg4eng`.

### 23/10 — D1GP (demande 65, suite)

- Reconnu dans `taito.cpp` (game.exe, `IFC23.dll`, `game.inf`). Premier lancement : réseau (35 s d'échec), « DANGER!
  Handle initialize », attraction. COM2 ouvert deux fois : la bibliothèque « Wdt » (chien de garde, `05 03 e8` au
  nœud 0, commande inconnue de notre JVS, le jeu continue), puis « Xio », du JVS ordinaire. COM1 est le lecteur de
  cartes (refusé : « CARD VENDOR / ERROR », on joue sans carte).
- Le jeu crée DirectInput 8 en Unicode (`IDirectInput8W`), énumère les manettes à retour de force seulement
  (`DIEDFL_FORCEFEEDBACK`), prend le nom du produit et lit `<nom>.txt` (boutons « x » nommés comme une manette PS2,
  ACCEL et BRAKE des numéros d'axe, STEER0/STEER1 zone morte et saturation ; un fichier par défaut écrit s'il manque) ;
  le Logitech Driving Force Pro a un traitement à part. Le volant DirectInput de windowsloader n'existait qu'en ANSI :
  `VirtualDeviceW` par-dessus (les structures à noms converties), listé et ouvert par `IDirectInput8W`.
- Les boutons de la borne sont sur le JVS. Bit 15 (Start de Battle Gear 4) sans effet ; des bits de sonde B0..B23
  appuyés un à un dans TEST MODE puis tenus dans le test d'E/S : START bit 3, VIEW CHANGE 2 (descend dans le menu),
  SHIFT DOWN 8, SHIFT UP 9, SIDE BRAKE 10, SERVICE 14 ; COIN s'allume sur un bit du 3e octet, les pièces passent par
  les compteurs. Sondes retirées. Volant ±0,8 → HANDLE ∓1,000 (étalonnage du jeu), ACCELE et BRAKE 1,000.
- Retour de force : `IFC23.dll` essaie l'API TouchSense par COM (absente), puis `CoCreateInstance(CLSID_DirectInput,
  IID_IDirectInputA)`, `Initialize(0x700)`, EnumDevices(joystick, retour de force), CreateDevice, QueryInterface
  `IDirectInputDevice2A`. Ce DirectInput 7 ne passe pas par les crochets de dinput8 : accroché à sa création
  (system.cpp → `wl_dinput_legacy`), il liste et ouvre le même volant, avec le type joystick/volant de DirectInput 7
  (même place des méthodes que DirectInput 8, la même classe sert). Effets créés : constantes, ressort, amortisseur,
  frottement, périodiques ; le ressort mis à jour à chaque image.
- La force alternait avec 0 à chaque trame : sonde sur la somme des effets (jamais nulle), puis trouvé : les sorties
  JVS de D1GP (lampes) passaient par le calcul du moteur de Chase H.Q. 2 (`jvs_output`, `!g_bg4`). Réservé à Chase H.Q. 2.
- Course en mode agent puis avec vJoy (512 changements de force au joueur 1), F10, menu de service. `race_check.py`
  connaît `d1gp`. Une erreur de ma part : une commande `python -` vide laissée en fin de ligne a tourné en boucle en
  arrière-plan (sortie de 64 Mo dans le dossier de la session), arrêtée par son identifiant de tâche.

### 23/10 — Valve Limit R (demande 65, suite) ; le point des jeux (demande 68)

- Reconnu dans `taito.cpp` (`launcher.exe`, `game.inf`, `data\bin2`). JVS sur COM2 par la « BG3 I/O Libraly » de Taito,
  carte du volant de Battle Gear 4 sur COM1 (même protocole). Premier lancement : images de l'agent refusées, le fond
  d'image en R5G6B5 : conversion 16 bits ajoutée à la capture Direct3D 9.
- « PLEASE CALIBRATE I/O DEVICE / PRESS TEST BUTTON » ; ni le bit Test ni aucun des 24 bits du joueur 1 n'ouvrait le
  menu. Lu dans le code : la bibliothèque range la réponse 0x20 (octet système puis joueurs) dans un nœud (0x99dc70 +
  0x1b8 par nœud), l'objet d'E/S (+0x41aab0, un fil qui interroge toutes les 16 ms) la traduit par une table
  (0x4c3c78) en mot de boutons du jeu (Test 0x100, valider 8, haut/bas 0x20/0x10), recopié à chaque image avec la
  valeur d'avant pour les fronts. Une sonde dans la boucle JVS (état des objets toutes les 2 s, et serré autour d'un
  appui) : Test arrivait bien, l'écran passait à son menu ; mes appuis tombaient avant l'écran qui l'attend (le
  démarrage alterne avec la démo). Un script qui réappuie jusqu'à voir « TEST MODE » en rouge règle le minutage.
- Test d'E/S (I/O CHECK) avec les bits de sonde : START 15, SERVICE 14, NITRO 13, VIEW 12, SIDE BRAKE 11, SHIFT UP 10,
  SHIFT DOWN 9. Pédales : voies 1 et 2 (code), affichées à 0 tant que non étalonnées. Étalonnage minuté (5 s par phase)
  fait et sauvé dans la copie (`expdat`, `extdat`) ; le démarrage suivant n'a plus le message.
- Carte du volant : le jeu envoie des paramètres d'effets à la manière de Battle Gear 3 (`01` couple constant en chocs,
  `03` ressort, `04` amortisseur, `05`/`06` vibration) et interroge par `00 00` ; joués pour lui seul (`vlr_effects`).
  Course en mode agent puis avec vJoy (362 changements de force), F10. `race_check.py` connaît `vlimitr`.
- Demande 68 (« le tableau des jeux faits et de ceux qui restent ») : donné d'après BILAN.md, pendant le travail.

### 23/10 — Crazy Ride, premiers lancements (demande 65, suite)

- `CrazyTaxiGame-Win64-Shipping.exe` (Unreal Engine 4.19, 64 bits) lancé par `windowsloader.exe`, qui le passe à
  `windowsloader64.exe`. Fenêtre 2048×1280, Direct3D 11, première image à 5-15 s, puis le chargement : de 16 s à plus
  de 200 s selon les lancements. Les images de l'agent échouent tant qu'il charge (aucun Present en 5 s).
- Échantillonneur de fils 64 bits (`sampler64.py`, contextes x64, adresses de code trouvées sur les piles, noms par le
  dbghelp des Debugging Tools : le PDB du jeu ne se charge pas, ntdll et le pilote NVIDIA si) : un lancement bloqué
  montre le fil de rendu dans le Present de windowsloader, sur StretchDIBits de la recopie GDI, le fil principal en
  attente.
- Correction (display.cpp) : Present tourne ici sur le fil de rendu, pas sur celui qui possède les fenêtres ;
  `SetWindowPos` ou un changement de style depuis là attend les messages du fil propriétaire, qui peut lui-même attendre
  le rendu. Le mode est maintenant appliqué par la procédure de fenêtre (message posté), la fenêtre fille placée en
  `SWP_ASYNCWINDOWPOS`. Ensuite 17 lancements de suite sans blocage (10 s à 2 min 30 de chargement : le cache du disque).
- Le jeu demande à supprimer `AppData\Local\FB` et `AppData\Local\Unreal Engine` (BIND_SHDeleteFolder) : refusé ;
  il cherche une caméra (MFEnumDeviceSources, refusé chaque seconde) et appelle des serveurs en 443 (refusé).
- Les entrées : A2 (octet 2 bit 2) ouvre le menu de test ; les trois boutons ronds de ce menu s'allument avec A0
  (rouge, valider), A1 (noir « D », suivant), A2 (vert). Start est B0 (octet 3 bit 0) : il lance l'avertissement, la
  ceinture, les consignes, le choix de la voiture, puis le Nitro ; B1 est « Pause Seat Motion » (Input Test). B2 a
  d'abord semblé lancer la partie : c'était le B0 appuyé juste après. Le code (+0x1b3b90, la trame copiée à +0x4b9 de
  l'objet) lit aussi les bits 2 et 7 de l'octet 3, usage non vu.
- Le volant : `Setting_Test_Input.ini` (étalonnage fait sous TeknoParrot) attend 1..127, milieu 64 ; windowsloader
  donnait 0..4095. Le sens vu au choix de la voiture, puis à l'Input Test : 1 à gauche.
- Les pédales : la voiture ne bougeait pas (« Push Gas Pedal ») ; l'accélérateur est l'octet 7 et le frein le 6 (la
  voiture part octet 7 à fond, freine octet 6 à fond).
- Le moteur du volant : « 3 64 0 0 », « 0 108 0 0 », « 3 108 0 0 » et « 3 108 5 0 » aux chocs ; joué comme ressort et
  secousse (supposé). Course en mode agent puis avec vJoy (512 changements de force), F10.

### 23/10 — Sonic & Sega All-Stars Racing, la partie (demande 65, suite ; demande 71)

- Pourquoi pièces et Start étaient ignorés : amCredit (+0x414ed0) ne démarre que si la région de la carte
  (+0x414950) est 1, 2, 4 ou 8. Elle vient du premier enregistrement de l'EEPROM, AM_SYSDATA_STATIC (octet 0x0C ;
  le numéro de série de la carte en 0x0F), que le système de la borne écrit, pas le jeu : il était à zéro.
  windowsloader l'écrit quand son CRC32 ne va pas (région export, comme la clé) ; « PRESS START BUTTON » s'affiche,
  Start mène au choix du mode.
- Ensuite le jeu ne présentait plus d'image après le choix du mode : le JVS et la carte du volant continuaient, le
  fil principal tournait dans l'interpréteur des écrans (+0x23cfa0, octets lus un à un, 93 codes) appelé par
  +0x17a230 (échantillonneur de fils 32 bits, attaché au jeu). Le journal montrait plus tôt `Y:\ssr_save.bin`
  introuvable (erreur 3) : la sauvegarde, sur un disque de la borne. La copie a ce fichier dans le dossier du jeu
  (329 728 octets, de 2021) ; `Y:` y est maintenant redirigé (champ `saveDrive` de la table RingEdge), et le
  choix du pilote, du circuit, le chargement et la course passent.
- Les fichiers `fe_character_select_sub_en.stz` et `fe_character_select_metalsonic_en.stz` manquent à la copie
  (erreur 2) : sans effet vu.
- Crédits : le menu de test du jeu n'a pas les réglages de pièces. Option du jeu `FREE_PLAY` (« Jeu gratuit »,
  page « Options du jeu » de F10, cochée par défaut) : l'octet 0x0A d'AM_SYSDATA_CREDIT (0x20) mis à 1 ou 0 au
  lancement, l'enregistrement par défaut du jeu écrit s'il manque. Vérifié des deux côtés, en partant d'une
  EEPROM vide : pièces comptées (CREDIT(S) 1, 2, 3), Start en prend une ; jeu gratuit, Start direct. L'ancien
  `wl_eeprom.bin` réglé à la main est mis de côté dans le dossier de travail.
- Retour de force : en course, le couple (84) de −16 à +20 sur 127, l'amortisseur (86 2e 14, 86 2f 14…) noté sans
  être joué, comme pour les autres jeux Sega. Avec vJoy : jusqu'à 173 changements de force en 5 s.
- Le sens du volant vu en course : +0,8 tourne à gauche, −0,8 à droite.
- Des images blanches avec le logo du jeu en pleine course : la transition du jeu quand la voiture tombe dans
  l'eau (elle revient en piste transparente) ; elles viennent bien du Present du jeu, une seule fenêtre.
- vJoy et l'écran pris en accord avec la session Linuxloader ; elle a copié R-Tuned puis Initial D 4 depuis
  racecab1 pendant ce temps.

### 23/10 — Sega World Drivers Championship (ALLS), premiers pas (demande 71, suite)

- Copie de racecab1 dans `games/swdc` (1 804 fichiers, 12,5 Go, 95 min à 2,3 Mo/s) ; le plugin FFB (`d3d11.dll`),
  SDL2, `teknoparrot.ini` et le dossier `TeknoParrot` d'AMDaemon mis de côté dans `_teknoparrot_off`. Puis copie de
  Mario Kart DX v1.10 lancée (16 497 fichiers, 5,8 Go).
- Deux programmes x64 : `AMDaemon\amdaemon.exe`, le service de la plateforme (clé, carte d'E/S USB, lecteur de cartes,
  réseau, crédits), et `Todoroki-Win64-Shipping.exe` (Unreal Engine 4), qui lui parle par mémoire partagée. Les scripts
  de la borne ne sont pas lancés : `99_StartApp.bat` efface `Y:\` quand le dossier du jeu y manque, `10_PortSetting.bat`
  importe un fichier dans le registre.
- Le lanceur démarre maintenant un programme compagnon avant le jeu (`g_companions` de launcher.cpp) : amdaemon depuis
  `App\Todoroki` avec ses fichiers `config*.json`, 5 s avant le jeu, sa sortie vers NUL (en mode agent, la sortie du
  lanceur est le protocole), sans manettes ni agent, son journal `<journal>-companion.log`, arrêté avec le jeu.
- Avant de le lancer, des gardes de plus dans system.cpp : amdaemon importe de quoi libérer l'adresse du PC
  (`IpReleaseAddress`), changer de Wi-Fi (`WlanConnect`, `WlanSetProfile`), monter ou défaire un lecteur réseau
  (`WNetAddConnection2W`, `WNetCancelConnection2W`), arrêter un service, vider le journal d'événements, effacer un
  dossier par le shell (`SHFileOperationW`, permis dans son propre dossier seulement) : tout refusé et noté.
- Fait pour amdaemon, d'après segatools (`common/platform`, `common/board`, Unlicense ; ses valeurs, pas son code) :
  - le registre `HKLM\SYSTEM\SEGA\SystemProperty` inventé en mémoire (nouveau dans system.cpp : clés et valeurs
    lues et écrites pour la durée du lancement) : la plateforme (`static`), la clé (`keychip` : jeu SDDS, plateforme ACA4,
    région 1, numéro A69E-01A88888888), `Master`, les lecteurs (`mount` : AMFS `E:\`, APPDATA `Y:\`), le pilote
    d'affichage (`amVideo`) et les réglages des écrans ;
  - la clé `\??\FddDriver` (ses requêtes DeviceIoControl), le moniteur `\.\sghwmonitor`, le numéro de la carte
    (`GetComputerNameA`), le pilote d'affichage `$amvideo` joué par windowsloader (une DLL virtuelle, nouvelle dans
    system.cpp, ses fonctions par ordinal) ;
  - les lecteurs `D:`, `E:` et `Y:` dans des dossiers de la copie (`D`, `AMFS`, `Y`) ; amdaemon y crée lui-même
    `sysfile.dat` et `Y\SDDS\appfile.dat` ; `SHCreateDirectoryExW` est redirigé aussi (le shell crée les dossiers
    lui-même, sans passer par nos crochets) ;
  - la carte d'E/S USB « IO4 » 837-15257 (HID, VID 0CA3 PID 0021, sa chaîne de produit, rapports d'entrée et commandes) ;
    ses lectures en attente doivent finir plus tard (nouveau dans vhid.cpp) : amdaemon vide la file de la carte en lisant
    jusqu'à une lecture en attente, il bouclait sans fin ;
  - l'afficheur VFD (COM4), le lecteur de cartes Aime (COM3, trames E0/D0, sans carte posée) ;
  - une carte réseau de borne à la place de celle du PC (nouveau `netenv.cpp`) : 192.168.172.11/24, passerelle .1,
    pings répondus.
- Côté jeu : la carte du moteur du volant sur COM1 (le protocole des cartes de pilotage Sega, `driveboard.cpp`), la
  carte des lampes 837-15070 sur COM2 (adresse 00), les entrées passées à amdaemon par une mémoire partagée.
- Le jeu en plein écran se réduisait (notre refus de `SetFullscreenState` répond S_OK) : avec `-windowed` il s'affiche.
  Étapes vues : « Core Setup », « Aime Setup », puis les erreurs une à une : 0087 (stockage) partie avec la comptabilité
  ALL.Net coupée, 8002 (réseau de la borne) partie avec la carte réseau, 6501 (lecteur de cartes) parti avec le lecteur.
  Pour l'essai, une config de plus passée à amdaemon (`WINDOWSLOADER_COMPANION_ARGS=-c <fichier>`) coupe l'authentification
  et la comptabilité ALL.Net et la monnaie électronique ; couper le réseau est refusé par amdaemon (erreur 4104).
- Reste : « Error 6401, I/O ボードを認識していません » (carte d'E/S non reconnue). amdaemon n'envoie à la carte que les
  commandes 03 (effacer l'état) et 04 (lampes), jamais 01 ni 02 ; ses accusés de réception se lisent dans les rapports
  d'entrée (`amUsbiopAnalyzeAcknowledge`). À suivre. Rien de SWDC n'est encore commité.

### 24/10 — SWDC, la carte d'E/S reconnue ; Mario Kart DX 1.10 jouable (demande 74)

- Après l'exclusion de Defender posée par l'utilisateur (demande 75), le lanceur reconstruit.
- SWDC, « Error 6401 » (carte d'E/S non reconnue) : le journal d'amdaemon (`AMDaemon\amdaemon.log`) disait
  « no acknowledge packet » (`usbio::Node::recvInputData`). amdaemon vide la file de la carte jusqu'à une lecture en
  attente et exige au moins un rapport déjà là : une vraie carte en envoie sans cesse, le pilote HID de Windows en garde
  32. vhid.cpp tient maintenant cette file (les rapports s'y ajoutent au rythme de la carte, une lecture en prend un ou
  attend le suivant), avec une horloge précise (QueryPerformanceCounter, timeBeginPeriod(1) pour son fil) et une
  période de 1 ms pour l'IO4 → 6401 parti.
- SWDC, ensuite « Error 3003 » (communication avec la carte des lampes indicatrices) : la carte des lampes 837-15070 sur
  COM2 répond maintenant à l'adresse que le jeu lui donne (00 et non 11), mais le jeu ne lui écrit rien pendant les
  13 s avant l'erreur ; les quatre commandes vues ensuite sont celles du mode d'erreur. Le greffon série d'Unreal parle
  d'un « COM20 » jamais ouvert. À suivre.
- Mario Kart DX 1.10 (le mod français) : le même module que la 1.18, qui les distingue par `bngrw.dll`. Pas à pas :
  - « E19-10 USB dongle error » : la fonction de la clé de sécurité retrouvée à +0x43aef0 (le GUID de son interface,
    le même début que celle de la 1.18) ;
  - « E07 IC card R/W not connected » : le lecteur de cartes sur COM4, 38400 bauds, en trames du PN532 de NXP (réveil
    55, acquittements `00 00 FF 00 FF 00`, `D4` commande, réponses `D5` commande + 1) : vitesse (18), réglages radio
    (32), écriture de registre (08, réponse avec un octet d'état), lecture de huit registres (06, sans octet d'état),
    GPIO (0C, 0E), sans carte → « IC card R/W test OK » ;
  - « E05 Initial network authentication error » : l'âge de l'authentification (+0x5b060, la première lecture de
    « last_auth.bin », le même début que dans la 1.18) et la limite hors ligne (+0x5f630, les mêmes octets) ;
  - puis attraction en français, partie, course (Château de Peach) ; « Reaction force » mis à ON dans le menu de test
    de la copie : forces en course (raideur 22 à 28), vJoy (accord de Linuxloader), F10.
- SWDC, suite : la liste des ports série de Windows (`HARDWARE\DEVICEMAP\SERIALCOMM`) inventée pour le jeu (COM1,
  COM2 ; les clés inventées savent maintenant énumérer leurs valeurs) : le jeu ne la lit pas. Le « COM20 » vient du
  même greffon série d'Unreal que COM2 (ses fonctions d'ouverture et de fermeture sont voisines), un second port
  jamais ouvert. Tenir Test levé au démarrage n'ouvre pas de menu de test. L'erreur 3003 reste ; le déroulé du
  démarrage est dans les Blueprints du jeu, pas lisible ainsi.

### 24/10 — SWDC : le tableau de bord, la borne principale, l'ICF ; la touche Tab ; la release (demandes 76 à 78)

- Le tableau de bord (838-15415, « Indicator BD ») : le Blueprint `BP_DashBoard` (sorti du .pak) cherche son port de
  COM20 à COM29 dans la liste de Windows (`GetPortNames` du greffon SerialPort d'Unreal). Le greffon interroge la clé
  par `RegQueryInfoKeyW` avant d'énumérer ses valeurs : les clés inventées y répondent maintenant (system.cpp), et
  COM20 est dans la liste. Le protocole est du texte : `pr\r\n` demande l'état, la carte répond `ps` et six chiffres
  (ses boutons), puis `\r\n` ; les commandes d'affichage sont `:sp` (vitesse), `:rp` (régime), `:gr` (rapport),
  `:le` (lampes). → 3003 partie.
- Ensuite « CONNECT MAIN CABINET » puis 8404 (« serveur d'installation LAN introuvable »). Le jeu se sait borne
  principale par `Y:\SDDS\LanServer.dat` (son `MainDetector`), et le script de la borne lance alors amdaemon avec
  `config_LanServer.json` (TeknoParrot : « Main Cabinet=1 »). Le lanceur prend désormais cette configuration ; le
  fichier vide est créé dans la copie. Sans installation LAN : 4103 ; avec la livraison réseau : 4104.
- 8404 reste : amdaemon lit `E:\ICF1` (AMFS), absent de cette copie. L'ICF décrit les images installées (plateforme,
  application, options). Lu dans amdaemon : chiffré en AES-128-CBC (clé `09ca5efd…`, IV `b155c22c…`, recomposés par
  amdaemon depuis trois tables de 32 octets) ; en-tête de 64 octets (CRC32 de tout le reste, taille, nombre d'entrées,
  « SDDS », « ACA », CRC des entrées actives), puis des entrées de 64 octets.
- La touche du menu passe de F10 à Tab (demande 77), comme Flycast, linuxloader et Supermodel ; Alt+Tab est laissé à
  Windows. Pas d'essai en jeu possible : depuis ~6 h l'écran du PC est déconnecté (Windows ne voit qu'un « WinDisc »),
  Sonic et Mario Kart DX ne trouvent plus de carte graphique.
- Release (demande 78) : les deux builds (32 et 64 bits) et une archive à plat comme celle de linuxloader, avec
  README, BILAN, LICENSE et un RELEASE.txt (jeux et programme à donner, ligne de commande, ce qui manque :
  pas de `--write-dir`, pas de sortie réseau pour BackForceFeeder). Réponses à Image borne.

### 24/10 — Les jeux écartés : Storm Racer G, Sega Racing Classic ; la touche Tab vérifiée (demandes 81 et 82)

- Copies : Image borne faisait tourner une campagne d'essais (linuxloader) sur les deux bornes en même temps ; les
  copies ont été arrêtées puis reprises à sa demande. Storm Racer G : `assets` seul arrivé (4,8 Go sur 6,3).
- Storm Racer G (`bin\racing.exe`, x86, Direct3D 9) : tout le matériel par `usb_io.dll` (noms C++) et
  `usb_cam.dll`, remplacés dans la table d'import (`stormracer.cpp`). Lu dans `usb_io.dll` : les entrées (deux mots
  de boutons, volant centré à 0x80, accélérateur, frein, levier de boost bouton sous un seuil), le compteur de
  pièces, le moteur en servo de position (`SetSteeringStatu` : force, fenêtre 0..255, mode ; `ForceFeedback.urc`
  donne 127..130 et 400), joué en ressort. La caméra des photos de joueurs est déclarée absente (la webcam du PC
  n'est jamais ouverte). Pas de dongle dans `racing.exe` ; `SRG_FS.exe` est un script AutoHotkey ajouté.
- Sega Racing Classic (`C:\Sega Racing Classic\d1a.exe`, RingEdge 2009) : ajouté à la table de
  `ringedge.cpp`. Les boutons d'après son INPUT TEST puis son code (+0xdb690, la fonction +0xbf030 lit des numéros :
  0 Test, 1 Service, 2 Start, 3 à 6 les vues) : vues sur le joueur 1 bits 13..10, levier en H sur le joueur 2
  (gauche+haut = 1, gauche+bas = 2, haut = 3, bas = 4), rapport tenu par windowsloader (`Gear1..4`, `GearUp`,
  `GearDown`). La carte moteur n'est pilotée ni par JVS (ses sorties : les lampes), ni par un port : sa commande
  d'un octet (Model 2/3) est lue dans les données du jeu (VA 0x834C19, l'adresse de FFBArcadePlugin) et jouée.
  Partie jouée jusqu'en course ; retour de force cohérent en mode agent.
- La touche Tab : vérifiée par une vraie frappe (`keybd_event`, seulement la fenêtre du jeu au premier plan)
  sur Sega Racing Classic : menu ouvert puis refermé.

### 24/10 — Storm Racer G, Wacky Races et Let's Go Safari jouables (demandes 83 à 85)

- Storm Racer G : jusqu'en course. Il a fallu :
  - couper l'anticrénelage, que le jeu demande et que le pilote refuse ;
  - sauter la fonction du tableau de bord des points de passage quand ses textures manquent ;
  - répondre « hors ligne » pour `NetDbSync.dll` ;
  - envoyer au journal les messages de sa console (`printf`) ; `_getch` répond « c ».

  Les vidéos sont des WMV3 en AVI : sans le décodeur Vidéo pour Windows, des images noires (pas de téléchargement
  sans accord). Moteur : servo de position, joué en ressort.
- Wacky Races (`Launcher.exe` + `wr.ini`, Taito Type X2) : les boutons d'après son I/O CHECK, le volant et les
  pédales comme Chase H.Q. 2, le moteur par les sorties JVS. `LINKID` manque dans `wr.ini` : il vaut 0 par un
  crochet sur la lecture des réglages. Le fichier n'est pas touché : la première tentative (une ligne ajoutée) avait
  collé la ligne à « REGION U », et le fichier a été restauré depuis l'original. Les fonds des menus 2D sont en
  800×600 en haut à gauche, à toute résolution.
- Let's Go Safari (RingWide, `GameSampR_RingWide.exe`) :
  - pas de table de clé : le chemin est construit par un format, remplacé par « toolmode » (`use_toolmode`, qui ne
    savait faire que le chemin écrit en entier de K.O. Drive) ;
  - le port PCP 40114 (stockages) servi, avec 40112 ;
  - l'erreur 6402 : le jeu compare l'identité de la carte JVS (champs 3 « 837-14895 » et 6 « FE MODE ») et veut un
    joueur, 14 boutons, 16 sorties. L'identité est celle de TeknoParrot (`JVSIdentifiers.cs`, TeknoParrotUI, open
    source), lue sur GitHub ; `WlJvsBoard.switches` annonce un nombre de boutons qui n'est pas un multiple de 8 ;
  - les boutons trouvés en allumant les bits un à un dans son INPUT TEST, les sorties dans son OUTPUT TEST. Le jeu
    ne lit pas de voie analogique : le volant n'est vu que par deux contacts.
- Les fichiers FFBPlugin : demande transmise à Image borne pour les jeux complets (Sega Rally 3, Daytona et NSE, Ford
  Racing, GRID, Wasteland Racers).
- Erreur de manipulation : dans le menu de test de Let's Go Safari, une suite d'appuis trop longue a changé la langue
  et le son de démo, puis vidé la sauvegarde. Ce n'étaient que les fichiers émulés de la copie (`wl_sram.bin`,
  `wl_eeprom.bin`, créés le jour même) : mis à la corbeille, le jeu repart des valeurs d'usine.
- Copies : racecab1 libéré par Image borne ; Initial D 7, puis 8, puis les Wangan une à une.

### 24/10 — Initial D 7 et 8 (demande 84)

- Copies depuis racecab1, au signal d'Image borne : Initial D 7 (4,9 Go), puis 8 (5,4 Go), sans échec. Ensuite,
  le partage a été laissé à Linuxloader pour ses trois copies ; les Wangan attendent son « fini ».
- Erreur 6401 : ces deux jeux cherchent la carte JVS par `\\.\mxjvs`, pas par COM4.
- Erreur 5101 (le lecteur de cartes ne répond pas). L'option « CARD R/W USE » du menu de test n'y change rien.
  Le lecteur est derrière `picodaemon.exe` : une mémoire partagée avec le jeu, le lecteur sur COM2.
  - Lu dans picodaemon : une table de 40 commandes du jeu, et le protocole du lecteur (trames STX, ACK, ENQ ;
    trois octets d'état). YACardEmu (GPL-2.0) a servi de repère pour le sens des octets d'état ; rien n'en est
    repris.
  - Le lanceur démarre picodaemon à côté du jeu, et `cardrw.cpp` y joue le lecteur.
- Trois corrections, trouvées en journalisant la mémoire partagée :
  - répondre à chaque ENQ (picodaemon redemande tant qu'une commande tourne : le nettoyage restait affiché) ;
  - « pas de carte » vaut 0x20 dans l'octet R (0x30 signale une carte à la fente : « prenez votre permis »
    sans fin) ;
  - une lecture répond « en attente de carte », sinon le jeu lit une carte vide (erreur 5110, version
    différente).
- Initial D 8 : le `_dumped_` n'a pas de manifeste pour le runtime VC 2008 (0xC0000135). Le `redumped_`, celui
  que lance TeknoParrot, marche.
- Une erreur 0949 (« Keychip Not Found ») au bout de quelques minutes sur une partie d'Initial D 8. Le seul
  défi de la clé de cette partie était absent de la table, et windowsloader y répondait par des zéros. Ces
  défis reçoivent maintenant des octets non nuls tirés du défi ; la partie suivante a été jusqu'en course.

### 24/10 au soir — Wangan Midnight 5 commencé ; les mesures d'Image borne (demandes 88 et 89)

- Copies : Linuxloader a fait ses trois copies depuis racecab1, puis Image borne a rouvert le partage. Wangan
  Midnight 5 (18,5 Go, 46 544 fichiers) en 4 h 06, à environ 1,4 Mo/s en moyenne. 5DX+ (15,2 Go) et 6 (16,3 Go)
  attendent de la place : il reste 12 Go, et la corbeille ne se vide que par le propriétaire.
- Wangan Midnight 5 : voir BILAN.md.
  - `wl_patch_iat` sait maintenant remplacer une fonction importée par son numéro.
  - Le module rangé dans `namco.cpp`, pour la carte JVS, la carte moteur et les lecteurs F: et G: de Mario Kart
    DX.
  - Trois étapes passées : la clé, la carte d'entrées, le réseau local. Arrêt sur le terminal de salle.
- Image borne a mesuré les 32 jeux de la release 4 sur racecab2.
  - Retour de force fort ou net : Sega Rally 3, Crazy Ride, GRID, Showdown, Wasteland Racers, K.O. Drive.
  - Faible : Initial D 6, Wacky Races, Sonic.
  - Zéro sur une quinzaine : son protocole (départ puis cinq coups de gaz) ne mène probablement pas en course
    les jeux à nombreux écrans. Les séquences de mes essais lui ont été envoyées, jeu par jeu (relevées dans les
    scripts d'essai par un agent).
  - Six éditions de Sega Rally 3 se ferment d'elles-mêmes après environ 510 paquets : son journal demandé.

### 25/10 — Les copies terminées à la corbeille (demande 90)

- La corbeille de C: est limitée à 48,5 Go : au-delà, Windows effacerait définitivement les éléments les plus
  anciens. Les copies vont donc à la corbeille en deux fois, la seconde après que le propriétaire l'a vidée.
- Let's Go Safari refusait : une de ses polices (`dat\font\FOT-SeuratPro-EB.otf`) restait chargée par Windows,
  le jeu l'ajoutant à toute la session (AddFontResource) à chaque lancement, 11 fois en tout, sans jamais la
  retirer. Retirée de la session (RemoveFontResource), puis le dossier mis à la corbeille.
- Correction dans `system.cpp` : une police que le jeu ajoute devient la sienne (FR_PRIVATE), retirée par Windows
  à la fin du jeu. Pas essayée sur Let's Go Safari, sa copie étant à la corbeille.

### 25/10, nuit — Les séquences rejouées sur la borne ; `--shots`

- Image borne a rejoué mes séquences sur racecab2 (release -4) : toujours zéro paquet de retour de force. Ses captures
  (PrintWindow) montrent :
  - Daytona et NSE bloqués sur « NETWORK DISCOVERY » (chez moi, ils passent) : son --log demandé ;
  - Mario Kart DX 1.18 en course : ce jeu n'envoie rien au moteur tant que son réglage « ハンドル反力の使用 » est
    OFF (mis à ON dans ma copie) ;
  - Storm Racer G sur l'écran photo, que Start passe ;
  - les jeux en Direct3D 9 en blanc ou en noir : PrintWindow ne voit pas leur rendu.
- `--shots <dossier>` ajouté au lanceur : l'image du jeu y est enregistrée toutes les 15 s (40 au plus), par la même
  prise que le mode agent. Pas encore essayée : l'écran du PC est de nouveau déconnecté (plus aucune sortie
  attachée, « Could not find any compatible Direct3D devices »).
- Image borne passe windowsloader en fenêtre sans bordure sur les deux bornes (DISPLAY_MODE = BORDERLESS,
  BORDERLESS_STRETCH pour les écrans de super-résolution).
- Daytona bloqué sur « NETWORK DISCOVERY » sur racecab2 : son ShellData.ini dit CabinetID = 2 (l'identité de link
  héritée de TeknoParrot), le jeu attend donc la borne 1 (trouvé par Image borne dans le --log). windowsloader fait
  lire aussi CabinetID = 1, sans toucher au fichier : le même fichier sert au solo et au link. Pas essayé (écran
  déconnecté, copie de Daytona plus ici).
- Mario Kart DX 1.18 : « ハンドル反力の使用 » est un réglage de la borne ; Image borne attend l'accord du propriétaire
  pour le changer.

### 25/10 — Sega Rally 3 : les éditions qui plantent

- Cinq éditions (Artic, Canyon, Master, Safari, Tropical) plantent 4 s après le départ, à Rally.exe+0x1ac4a5. Original,
  Standard, Alpine et Tropical478 tiennent. Les neuf ont le même Rally.exe ; ce sont des modifications qui échangent
  les circuits (`Main_release\tracks\repl par artic 1 2 3.txt`).
- Le minidump (1,6 Go, rapatrié à la demande du propriétaire), lu avec cdb : le code fautif est le suivi des points
  de passage (CSR_Checkpoint). Son objet (0x9df528) prévoit 22 points, mais leur tableau est vide et le n° du point
  courant aberrant (460 391). Au premier point de la course, le jeu lit hors mémoire. La démo ne fait pas ce suivi.
- Défaut des données de ces éditions sur l'emplacement Tropical4, pas de windowsloader. Pas de correctif : sauter ce
  suivi supprimerait les rallonges de temps.
- Le minidump n'était pas dans C:\ : Rally.exe, programme 32 bits sans manifeste, voit ses écritures sous C:\
  virtualisées dans AppData\Local\VirtualStore.

### 25/10 — Le sélecteur de jeux (demandes 97 à 99)

- `windowsloader.exe` sans argument : `wl_picker_run()` (`src/launcher/picker.cpp`), Dear ImGui sur Win32 et
  Direct3D 11 (les backends déjà dans `src/imgui`). Mise en page de linuxloader : liste à gauche, fiche à droite, aide
  en bas. La console disparaît quand le lanceur est le seul à l'avoir (double clic dans l'Explorateur).
- Les jeux : `src/launcher/gamelist.cpp`, 26 entrées reconnues comme le font les modules de la DLL (nom du programme,
  fichiers à côté, date PE pour Battle Gear 4 et Tuned). Chaque dossier du dossier des jeux est fouillé sur six niveaux,
  le programme le moins profond gagne ; deux dossiers du même jeu sont distingués par le nom du dossier.
- Le lancement : `windowsloader.exe --quiet <jeu> [arguments de la borne]` dans le dossier du lanceur, sans console ;
  sa sortie passe par un tube, et si le jeu s'arrête dans les 10 s avec un code non nul, sa fin est montrée dans le
  sélecteur (le journal du jeu n'existe pas quand c'est le lanceur qui échoue).
- Corrigés pendant l'essai : la relecture du dossier au milieu d'une image (elle libérait l'image du jeu encore à
  dessiner), les boutons pris par la navigation au clavier (Entrée sur « Quitter » aurait aussi lancé le jeu), Tab
  qui déplaçait la sélection, la boîte de message Windows qui laissait la fenêtre blanche (remplacée par une fenêtre
  ImGui).
- Essai : un premier envoi de touches par SendKeys est parti vers la fenêtre au premier plan, le visualiseur VNC de
  racecab1 (deux flèches bas) ; ensuite les touches sont postées à la fenêtre du sélecteur seulement.

### 25/10 — Wangan Midnight 5 : le terminal de salle, E0505 levée ; la version dans le titre (demandes 96, 100, 101)

- E0505 « Network disconnection error / Disconnected from local network » vers 35 s : faite à +0x5814cc quand le
  lien du terminal (objet de +0x8a6f70, état à +0x14) vaut 4. Trouvée en traçant les `error_code` fabriqués
  (+0x1eba10, `make_error_code(505)`), et non dans `GetIfEntry` (sondé chaque seconde après l'erreur, il répond
  « opérationnelle ») ni dans le gestionnaire de réception (+0x87b9b8, seulement sur une erreur de réception).
- Le terminal : la borne écoute 0.0.0.0:50765 (boost::asio, port de complétion, multidiffusion 225.0.0.1 de sa
  table d'adresses). Ses paquets : en-tête 01 04 et longueur, message protobuf (numéro de série 272211990002,
  numéro du paquet à l'octet 33, réglages), CRC-32 MSB d'abord (0x04C11DB7, depuis 0xFFFFFFFF, gros-boutiste)
  vérifié à +0x87bb42. Les six paquets « free play » viennent de `OpenParrot64.dll` de la copie locale de
  TeknoParrot (GPL-3.0, l'origine notée dans `namco.cpp`) : rien de téléchargé. Les jeux de paquets 1 à 3 de la
  DLL sont identiques (terminal 272211990002) ; 4 et 5 ont un autre terminal (280811401532, sans doute WMMT6).
- Envoyés vers 127.0.0.1:50765 (rien ne sort du PC), ils arrivent (≈ 140 réceptions par seconde) mais E0505
  reste : le jeu range ses terminaux par (octet 1, adresse de l'expéditeur), +0x87bc65. L'adresse de
  l'expéditeur est réécrite en 192.168.92.20 : `WSARecvFrom` note où asio l'attend, `GetQueuedCompletionStatus`
  la corrige au retour. E0505 levée, démonstration « FREE PLAY ».
- Sans le saut +0x58e70f : « TERMINAL UNIT LINK NG » (le terminal entendu, numéro 272211-990002), arrêt sur
  « GAME SERVER CONNECT CHECK » : gardé. Les trois NOP de la recherche de terminal (+0x87bd6e) ne servent plus
  maintenant qu'un terminal parle : retirés. Il reste deux correctifs au jeu (identifiant de la clé, lien du
  terminal).
- Copies : WMMT5DX+ (52 870 fichiers, 15,18 Go) puis WMMT6 (33 715 fichiers, 16,39 Go) depuis racecab1, sans
  échec, « fini » donné à Image borne après chacune.
- Titre : « [wmmt5] - 60 fps - windowsloader 0.1.0-20261025-njz3 FFB Edition » après le titre du jeu ; la version
  écrite à chaque build (`cmake/build_version.cmake`, `wl_build.h` réécrit seulement quand il change : un second
  build ne recompile rien). Aussi le titre du sélecteur, « À propos », le journal, la console.
- Prudence : la suppression des captures d'essai par `Remove-Item` a été refusée (règle : corbeille seulement) ;
  dossiers d'essai nouveaux à la place.

### 25/10 — Dossier d'écriture, sorties réseau, menu Tab désactivable (demandes 102 à 105)

- Repris de linuxloader (`writeDir.c`, `netOutputs.c`, `SETTINGS_MENU`), mêmes clés et même sens. Le sélecteur
  lit maintenant `[Launcher] GAMES_DIR` comme celui de linuxloader (`[Picker]` de la release du 25/10 encore lu,
  effacé quand Tab en choisit un autre).
- Dossier d'écriture. Le lanceur le résout (`src/launcher/writedir.cpp`, partagé avec le sélecteur), copie
  `windowsloader.ini` et `controls.ini` à sa racine, y met le journal, et le passe à la DLL
  (`WINDOWSLOADER_WRITE_DIR`). La DLL, une fois le jeu connu, fait `WRITE_DIR\<jeu>` et superpose : chemin
  complet du fichier (après les redirections D:, E:, F:...) recopié sous `WRITE_DIR\<jeu>\<lecteur>\...`, ce
  qui prend aussi les jeux qui écrivent au-dessus de leur programme. Ajoutés aux crochets : `CopyFileW`,
  `SetFileAttributesW`, `FindFirstFileExW`, `FindNextFileA/W`, `FindClose` (les listes fusionnées ont leur
  propre poignée).
- Trois corrections en essai : l'exclusion de tout le profil de l'utilisateur écartait les jeux du bureau
  (réduite à AppData et au dossier temporaire) ; les chemins de plus de 260 caractères (dossiers et copies
  ratés, le jeu bloqué) passent maintenant par la forme `\\?\` et des dossiers faits un à un, avec repli sur
  place si le dossier ne peut être fait ; les appels .ini ANSI ne prennent pas la forme `\\?\` : ils passent
  par les appels W quand le fichier est dans le dossier d'écriture.
- Sorties réseau : `src/hook/outputs.cpp`, sockets ouverts dans le fil du serveur (pas dans DllMain), lampes
  depuis la commande JVS 0x32, `RawDrive` depuis `driveboard.cpp` (octets tels quels) ou depuis `wl_ffb_set`
  (trames 80/81/83 puis 84, comme linuxloader le fait pour Dead Heat et Gaelco), `mame_stop` dans les crochets
  d'ExitProcess et de TerminateProcess (le « quitter » de l'agent et du menu compris).
- Essais : Wangan Midnight 5 avec les trois réunis, un programme d'essai 32/64 bits pour les appels A et W
  (16 contrôles), Wangan sans dossier d'écriture (inchangé). Les dossiers d'essai mis à la corbeille.
- Ensuite (demande 107, disque sous UWF sauf le dossier d'écriture) : les dossiers du système ne sont plus
  écartés mais rangés à part dans `WRITE_DIR\<jeu>` : `appdata`, `temp`, `programdata`, `windows` ; le dossier
  le plus précis décide (le dossier temporaire est dans AppData\Local). Un seul réglage : pas de clé par
  dossier (quatre clés `WRITE_DIR_*` essayées puis retirées à la demande du propriétaire). `LoadLibrary`
  charge depuis le dossier d'écriture une DLL que le jeu y a écrite. Les images de l'agent écrites hors de la
  superposition (elles étaient parties dans `temp`, le dossier d'essai étant dans le vrai dossier temporaire).
  Vu : le pilote NVIDIA écrit `ProgramData\NVIDIA Corporation\Drs\nvAppTimestamps` pour le jeu, rangé dans
  `programdata`.

### 25/10, soir — Wangan Midnight 5 jouable, son retour de force (demande 116)

- Commandes : la table de Mario Kart DX ne convenait pas. Des contacts d'essai (Bit0..Bit15, sous
  `WL_WMMT5_PROBE`) passés un à un dans le test des contacts (スイッチテスト) ont donné les bits : vue 0x0002,
  interruption 0x0001, Service 0x4000, haut 0x2000, bas 0x1000, Entrée 0x0200 ; le levier en H sur quatre
  contacts (0x80, 0x40, 0x20, 0x10), essayés seuls et par paires : 1 = A0, 2 = 60, 3 = 80, 4 = 40, 5 = 90,
  6 = 50. Joué par Gear1..Gear6 ou par ShiftUp / ShiftDown, option `HOLD_GEAR`.
- Course : l'accélérateur valide, le volant choisit ; la partie libre va jusqu'à la course de l'épisode 1
  (Yokohama) en une quinzaine d'appuis. Rien n'est écrit dans la copie du jeu : les réglages vont dans le
  dossier d'écriture d'essai.
- Retour de force : « ステアリング反力の使用 » à ON (OFF dans la copie), le jeu ouvre COM1 et échoue
  (« [STR3] initialize: open: Descripteur non valide ») : boost::asio rattache le port série à son port de
  complétion, et notre handle émulé est un événement. `vdev.cpp` : crochet de `CreateIoCompletionPort` (le
  port et la clé gardés), lectures et écritures en attente puis terminées par un paquet (`NtSetIoCompletion`,
  état d'annulation compris), une lecture sans données gardée en attente jusqu'à ce que la carte réponde.
- Le protocole lu dans `wmn5r.exe` (classe `FFBIo`, états Initial, InitialWait, PowerOff, Standby, PowerOn,
  Idle) : réponses de 3 octets (+0x723e20). La carte parle d'abord (« E00 ») ; le jeu coupe alors le moteur par
  la sortie JVS 0x80 du premier octet (`cMOTOR_BIT`) et attend « C06 », puis le rallume et attend « C01 » (au
  démarrage, puis en début de partie). Il envoie ensuite les commandes de 10 octets de Mario Kart DX, une à la
  fois : chacune attend « H » et la position du volant sur 10 bits (+0x72426a), sinon E20 puis E2212 « STR PCB
  Link error » 3 s plus tard. Les essais l'ont montré dans l'ordre : sans E00 et sans C01, E2212 au bout de
  60 s ; avec, une seule commande envoyée puis E2212.
- Vérifié en course (agent) : raideur jusqu'à 67, amortissement 63, vibrations ; volant à +0,5 force −0,24,
  à −0,5 force +0,26 (vers le centre), la voiture tourne du côté du volant. `RawDrive` sur le serveur réseau
  (148 trames). L'agent prend le volant en −1..1 et les pédales en 0..1 : les premières valeurs passées en
  unités brutes avaient mis le volant en butée.
- Mario Kart DX n'est plus dans `games` : son chemin `ReadFileEx` (partagé avec ce changement) n'a pas été
  rejoué, relu seulement.

### 26/10 — SWDC jouable en invité (demandes 148 à 153)

- Le serveur de titre est dans le processus du jeu (127.0.0.1:40181) : il lui faut la clé AES-256 que le jeu crée
  (`BCryptGenerateSymmetricKey`). Réponse = en-tête de 16 octets (01 01, statut, CRC32 du JSON non compressé,
  longueur, 4 octets au hasard ; c'est aussi l'IV) + AES-256-CBC(zlib(JSON)), PKCS7. Sans CRC ni longueur :
  « response data (-8) ».
- free_buckets : chaque valeur est une chaîne contenant du JSON, dont les valeurs sont aussi des chaînes (« 1 »),
  les heures en secondes Unix (UTC). Sans `event_setting1` (les manches) : plantage en +0x30f7f5 ; sans
  `update_time` dans les réglages recommandés : division par zéro en +0x3949f2 (DecideTimeSale).
- Aime : en-tête de 32 octets (A13E, version, commande, longueur, statut, jeu, salle, keychip), AES-128-ECB, clé
  « Copyright(C)SEGA » ; la réponse porte la commande + 1, statut 1 ; joueur inconnu : -1 ; 66 (au revoir) ferme.
  ALL.Net, Aime, UserDB et NetBattle « Online » ; la partie invité ne demande que 0b puis 66.
- Mode Cabinet : 8404 (installation LAN, `ICF1` absent) ou 4103 ; mode MiniCabinet : rien de tout ça. Le jeu cherche
  la borne principale par une diffusion UDP (40257 vers 192.168.172.255), rendue en boucle locale.
- La connexion : Start ouvre une question « ゲストログイン » (はい / いいえ, いいえ choisi) ; le premier Start semblait
  ignoré. Gauche puis Start : partie invité (1 crédit), NewcomerRegister, PostEntrance, TopMenu, CourseSelect,
  Matching, Race, Result, Continue, GameOver.
- Le lanceur (`g_companions`) : `gameArgs`, l'argument ajouté au jeu sauf s'il en a un du même nom ; `{temp}` dans
  les arguments du compagnon, le dossier temporaire (le fichier de réglages d'amdaemon). Avec `--write-dir` (essai
  jusqu'à la connexion) : le module l'écrit dans le vrai dossier temporaire (ses propres appels ne sont pas
  redirigés), amdaemon l'y lit, rien dans le dossier d'écriture ; sous UWF, il est dans la couche du filtre.
- La langue suivie : amdaemon charge `am_resources\error.<langue>.dll`, `error.common.<langue>.dll` et
  `warning.<langue>.dll` d'après `LANGUAGE` (english, japanese, chinese vérifiés).
- Retour de force mesuré par l'agent (`ffb`) : le jeu ramène le volant vers le centre ; au centre, la force reste
  près de 0, de forts à-coups (±0,96) aux chocs, l'accélérateur tenu sans piloter.

### 27/10 — SWDC : Error 0919, le lecteur de DVD des bornes (demande 157)

- Les erreurs d'amdaemon (`am_resources\error.english.dll`) : 0917 « USB Flash Drive Remain Error », 0919
  « DVD Drive Remain Error » : un support d'installation resté branché sur la borne.
- amdaemon importe `FindFirstVolumeW` / `FindNextVolumeW`, `QueryDosDeviceW` (la chaîne `\Device\CdRom`),
  `GetDriveTypeW`, `GetVolumeInformationW` ; SetupAPI seulement pour les HID sur ce PC.
- La redirection de E: (vers AMFS) se fait sur les chemins de fichiers : elle ne cache pas le lecteur. Ce PC n'a
  pas de E: : rien à trouver, d'où l'absence d'erreur jusque-là.
- Essai sans lecteur : une image ISO 9660 minimale (21 secteurs, `make_iso.py` du bloc-notes), montée par
  Mount-DiskImage sans droits d'administrateur : E:, type 5 (CD-ROM), puis démontée.
- Le Z: de ce PC (amovible, sans nom) n'a jamais donné 0917 : amdaemon ne le prenait pas pour une clé USB ; il est
  caché aussi maintenant.

### 27/10 — Initial D Zero v1.31, menu Tab en OpenGL, servoboard (demandes 188 à 191)

- Initial D Zero v1.31 (copie d'Image borne, `games/idz131`, le jeu dans `app\package`) : arrêtée à « ALL.Net Auth
  CHECKING » du POWER-ON NETWORK TEST. Son amdaemon (mars 2018) cherche `anbzvarg.wc`, puis `nvzr.anbzvarg.wc` :
  naominet.jp et aime.naominet.jp en ROT13. `allnet.cpp` décode le ROT13 avant de comparer : PowerOn `ver=1.31`
  répondu, Aime (0b puis 66) comme la v2.30.
- Ses `ICF1`/`ICF2` sont dans le `amfs` de segatools à côté de `app\` ; son `TeknoParrot\AMFS` n'a que `emoney`
  (amdaemon : `ICF1` introuvable). E: prend le `amfs` quand `ICF1` manque à l'endroit habituel.
- Le reste sans changement : réglage du volant, attract, deux pièces, invité, personnage, voiture, boîte, mode
  (Prologue), STAGE 0, course (170 km/h), retour de force (couples jusqu'à ±0,42). Le sélecteur distingue les deux
  versions par l'horodatage de `InitialD0_DX11_Nu.exe` (0x5BAB5915 pour la v1.31).
- `wl_sram.bin` et `wl_eeprom.bin` (la SRAM et l'EEPROM de la carte AMEX) étaient ouverts par le module hors des
  crochets de fichiers : à côté du jeu même avec `--write-dir`, repris d'un essai à l'autre (des crédits restés, une
  ERROR 8304 « Failed in connect(GAME) » passagère au démarrage). `wl_files_write_path` (files.cpp) donne leur place
  dans le dossier d'écriture (copiée de la première au besoin ; au-delà de MAX_PATH, la forme `\\?\`).
- Menu Tab en OpenGL : backend OpenGL 2 de Dear ImGui (1.92.0, repris de linuxloader), `opengl32.dll` chargé en
  différé (`/DELAYLOAD`) pour que les jeux Direct3D ne le chargent pas. Au Tab, l'image du jeu (le tampon arrière
  avant l'échange) est relue dans une texture, dessinée sous le menu à chaque image, puis une dernière fois pour
  l'échange du jeu. Sur Initial D 6 le menu sortait noir, retourné et décalé : le jeu laisse des programmes Cg (ARB),
  des tampons, des framebuffers actifs. Autour de chaque dessin : `glPushAttrib`/`glPushClientAttrib`, programmes ARB,
  textures cube/3D/rectangle, génération de coordonnées, plans de coupe, sRGB, stencil coupés ; programme GLSL,
  tampons (sommets, indices, pixels), framebuffers, unité de texture active gardés, mis à zéro, puis rendus.
- La servoboard (DRIVEBOARD.md) : 85 a la fréquence (demi-hertz) dans l'octet 1 et l'amplitude dans l'octet 2 ;
  80 01 : mode court, la force retombe 240 ms après la dernière trame ; 83 : gain. Un fil rejoue le sinus et le mode
  court toutes les 10 ms. Commune aux jeux Sega à servoboard série par `wl_driveboard_register`.

### 27/10 — L'affichage des jeux OpenGL (demande 193)

- Avant : Initial D 6 à 8 gardaient leur fenêtre telle quelle ; Frenzy Express avait ses viewports déplacés et
  agrandis (`glViewport`, `glGetIntegerv(GL_VIEWPORT)`), ce qui ne vaut pas pour un jeu qui dessine aussi dans ses
  propres framebuffers, relit son image ou fait des découpes (`glScissor`) : Initial D 6 charge `glBindFramebuffer`,
  `glBlitFramebuffer`, les pbuffers et `glRenderbufferStorageMultisample` par `wglGetProcAddress`.
- Maintenant (`glhook.cpp`) : au premier `SwapBuffers` d'un contexte dans une fenêtre, un framebuffer object de la
  taille de la zone cliente et du format de la fenêtre (couleur RGB8 ou RGBA8 selon les bits alpha, profondeur 24,
  stencil 8, échantillons ; multiéchantillonné : résolu dans un second avant la copie) prend la place du 0. À chaque
  `SwapBuffers` : la fenêtre effacée en noir, l'image (tout le framebuffer, ou le coin donné par `wl_gl_picture`)
  copiée par `glBlitFramebuffer` dans `wl_display_picture`, coupée à `wl_display_frame` (le recadrage).
- Ce que le jeu voit : `glBindFramebuffer(..., 0)` (ses pointeurs de `wglGetProcAddress`) lie celui de
  windowsloader ; `glGetIntegerv` rend 0 pour lui et `GL_BACK` pour sa couleur ; `glDrawBuffer`, `glReadBuffer`,
  `glDrawBuffers` des tampons de la fenêtre (`GL_BACK`...) donnent sa couleur ; un pbuffer rendu courant avec le même
  contexte retrouve le sien (`wglMakeCurrent`) ; `wglDeleteContext` libère tout. Les appels de windowsloader passent
  outre (`wl_busy`). Sans framebuffer objects, la fenêtre reste comme le jeu l'a faite.
- Le menu Tab redessine l'image du jeu depuis ce framebuffer (le jeu en pause ne l'écrit plus) là où l'affichage la
  met à chaque image : la page Affichage et F11 s'appliquent sous le menu.
- Frenzy Express : sa fenêtre limitée à 640×480 hors tout (`WM_GETMINMAXINFO`), la zone cliente restait à 634×451
  en mode WINDOW et ne grandissait pas en 16:9 ; `display_wndproc` lui rend les limites de Windows.
- Agent : `screen` d'un jeu OpenGL relit la fenêtre (tampon arrière après la copie, `wl_window_shot`) ; la copie
  de l'écran par GDI tombait à côté sur ce portable mis à l'échelle (2048×1280 logiques). `shot` reste l'image du jeu.
- Vu en passant, sans rapport : Initial D 6 a parfois une exception (lecture de 0x574C4844 dans setupapi) quand
  DirectInput énumère ses périphériques avec les SetupDi émulés (`\\.\mxsmbus`) ; le jeu continue. À regarder.

### 27/10 — La servoboard : son nom, les relevés de vraies bornes (demande 194)

- `src/hook/driveboard.cpp` et `.h` deviennent `servoboard.cpp` et `.h` : c'est la servoboard série de Sega
  (Lindbergh, RingEdge, RingWide, ALLS, Nu). « Drive board » reste le nom des cartes moteur décodées dans les
  modules des jeux (Sega Rally 3, les jeux au port parallèle, Sega Racing Classic), et de `EMULATE_DRIVEBOARD`.
- Les mesures de linuxloader sur cinq relevés COMDump (OutRun 2 SP, Harley-Davidson, Hummer, R-Tuned, Race TV) :
  - la recherche d'index (FC) dure 4,03 à 4,06 s : 4 050 ms ;
  - FF répond dans l'état d'avant le reset (Race TV en mode 14 : 94 ... ; Harley 94, puis 11) ;
  - FA 00 00 à 0F : les réponses relisent la dernière commande 80 à 8F, 0x80 | xx, ses deux valeurs, la somme
    (Harley : FA 00 00 puis 80 01 01 donne 80 01 01 00).
- Le mode de réponse est maintenant l'adresse de FA (1F court, 10 et 14 statuts longs, 00 à 0F relecture) ; FC
  lance la recherche d'index dans tous les modes ; une lecture rend les octets en attente jusqu'à la taille
  demandée (la réponse de 4 octets d'un reset lue en mode court).
- Banc d'essai (bloc-notes, `servotest.cpp` : `servoboard.cpp` seul, les autres fonctions remplacées) : les
  exemples des relevés retrouvés. Initial D 6 et Initial D Zero (FF, FA 00 10, FC, FA 00 1F 17 s après) : course
  et retour de force comme avant.

### 28/10 — Les langues de l'interface, catalogues .po (demande 208)

- Le français reste la langue du code. Un texte affiché passe par `wl_tr("…")` ; un texte d'une table affichée
  plus tard (options des modules, onglets, libellés des actions, notes de `gamelist.cpp`) est marqué `WL_N("…")`
  et traduit à l'affichage. Chaque autre langue est un catalogue gettext `lang\<code>.po` à côté du lanceur, lu
  tel quel au démarrage (lanceur et DLL) : msgctxt, msgid, msgstr (msgstr[0] d'un pluriel), lignes continuées,
  échappements C, entrées fuzzy et #~ laissées de côté, table de hachage FNV-1a. Les catalogues lus restent en
  mémoire : un texte rendu par `wl_tr` reste valable après un changement de langue.
- Sécurité : une traduction dont les conversions `%` diffèrent de celles du texte français est écartée (le
  français gardé, une ligne au journal) ; un catalogue mal fait ne peut pas donner à `snprintf` un format qui lit
  de mauvais arguments. Les textes qui ne sont pas des formats n'ont pas de `%` seul (« en pourcentage du cadre »).
- `[Launcher] LANGUAGE` (défaut AUTO) : le lanceur le lit dans son fichier de réglages (avec `--set
  Launcher.LANGUAGE=`) et le passe à la DLL par `WINDOWSLOADER_LANGUAGE` ; la DLL charge la langue dans
  `wl_init`, avant la détection du jeu (les modules déclarent alors leurs sorties, traduites à ce moment). AUTO :
  `GetUserDefaultUILanguage` (fr-FR : français ; pt-BR : pt_BR.po puis pt.po ; sinon en.po). Le nom d'une langue
  dans les listes : `X-Language-Name` de l'en-tête (comme les catalogues de linuxloader), sinon le nom Windows.
- Choix à chaud : le combo du pied du sélecteur (souris) et la page À propos du menu ; appliqué aussitôt, écrit
  dans `[Launcher] LANGUAGE` du fichier du lanceur (`wl_settings_launcher_file`, même quand le .ini propre au jeu
  est lu). Les onglets et les popups ont des identifiants ImGui fixes (`###about`, `###assign`, `###failed`) :
  la page reste affichée quand la langue change. Seules les descriptions des sorties déclarées par les modules
  attendent le lancement suivant.
- `scripts/lang_update.py` : extraction (littéraux concaténés, échappements, références `#:`, `#, c-format`),
  `lang/windowsloader.pot`, fusion de chaque `.po` (traductions gardées, nouvelles vides, disparues en #~),
  `--check` (texte manquant, vide, fuzzy ou autres formats : code 1), `--new CODE "Nom"`, `--unmarked` (textes à
  l'air français non marqués). `lang/` est copié à côté des programmes après le build ; la release doit contenir
  `lang\en.po` et `lang\windowsloader.pot`.
- La console du lanceur (`--help`, `--list-games`, erreurs) et les descriptions des cartes (`wl_game_set_boards`)
  étaient déjà en anglais : laissées telles quelles.
- Vérifié : sélecteur en anglais, en AUTO (français sur ce PC) et changement à chaud (titre de la fenêtre
  compris) ; les sept pages du menu en anglais sur Sega Rally 3 et Mario Kart DX 1.18 (options, cartes, sorties
  des modules), en français sur Sega Rally 3 ; changement à chaud dans le menu (la page À propos reste, le fichier
  écrit) ; aucun « no en translation » au journal ; les deux bitness sans avertissement.
