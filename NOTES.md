# Suivi des demandes et du développement

Dépôt `windowsloader` (https://github.com/njz3/windowsloader.git), branche `main`, rien de commité :
règle « pas de commit avant que Sega Rally 3 tourne ». Identité git locale au dépôt :
`njz3 <benjamin.maurin@gmail.com>`.

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
| 16 | 21/10 | Un menu F10 pour mettre le jeu en pause et le configurer, comme linuxloader ? | ⏳ oui, faisable : Dear ImGui dessiné dans le hook de Present (backend DX9), pause par gel des threads du jeu et du temps (QPC, timeGetTime, GetTickCount) ; pages Contrôles, Affichage, Retour de force |

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
| Battle Gear 4 | BattleGear4 | OpenParrot | Taito Type X+ | 32 | non (8) | ⏳ |
| Battle Gear 4 Pro | BattleGear4Pro | OpenParrot | Taito Type X+ | 32 | oui (952) | ⏳ |
| Battle Gear 4 Tuned | BattleGear4Tuned | OpenParrot | Taito Type X+ | 32 | oui (16 607) | ✅ jusqu'en course (agent), menu de service ; FFB ⚠️ (agent) ; la copie est l'édition « Professional » (option TeknoParrot) |
| Chase HQ 2 | ChaseHQ2 | OpenParrot | Taito Type X2 | 32 | oui (6 816) | ✅ jusqu'en course (agent), étalonnage au menu de service ; FFB ⚠️ (agent) |
| Crazy Ride | CrazyRide | TeknoParrot | UNIS PC (UE4) | 64 | non (0) | ⏳ |
| D1GP | D1GP | TeknoParrot | Taito Type X2 | 32 | non (0) | ⏳ |
| Daytona 3 | Daytona3 | TeknoParrot | Sega | 32 | oui (9 164) | ⚠️ joue en mode agent (demande 24) |
| Daytona 3 New Season | Daytona3NSE | TeknoParrot | Sega | 32 | oui (12 684) | ⚠️ joue en mode agent (demande 24) |
| Ford Racing | FR | TeknoParrot | Sega Arcade PC | 32 | oui (4 426) | ✅ (menu de service ⚠️ : clé iButton) |
| GRID | GRID | OpenParrot | — (`C:\Sega\Grid`) | 32 | oui (26 437) | ✅ jusqu'en course (agent), vJoy, F10 ; service ❌ (clé Dinkey de la coquille) |
| GTI Club 3 | GtiClub3 | OpenParrot | Konami PC | 32 | oui (9 573) | ⏳ |
| Initial D 6 AA | ID6 | TeknoParrot | Sega Ringedge | 32 | oui (13 400) | ✅ jusqu'en course (agent) ; FFB, affichage ⚠️ |
| K.O. Drive | KODrive | TeknoParrot | Sega Ringwide | 32 | oui (20 854) | ✅ jusqu'en course (agent), vJoy, F10, menu de service ; FFB, affichage ⚠️ |
| Mario Kart DX v1.10 | MKDX | TeknoParrot | Namco ES3 | 32 | non (62) | ⏳ |
| Mario Kart DX v1.18 | MKDX118 | TeknoParrot | Namco ES3 | 32 | oui (397) | ⏳ |
| Showdown | Showdown | TeknoParrot | Sega Arcade PC | 32 | oui (25 955) | ✅ jusqu'en course (agent) ; FFB ⚠️ (agent, port 0x378) |
| Sonic & Sega All-Stars Racing | SSASR | TeknoParrot | Sega Ringwide | 32 | oui (25 094) | ⚠️ démarre (carte du volant, réseau), menu de service ; une partie ne démarre pas |
| Sega Rally 3 | SR3 | TeknoParrot | Sega Europa R | 32 | non (63) | ✅ (retour de force par le port 0x1020) |
| Sega World Drivers Ch. | SWDC | TeknoParrot | Sega ALLS (UE4) | 64 | oui (14 261) | ⏳ |
| Virtua RLimit | VirtuaRLimit | OpenParrot | Taito Type X | 32 | non (0) | ⏳ |
| Wasteland Racers 2071 | WastelandRacers | TeknoParrot | Trio Tech PC | 32 | oui (23 484) | ⏳ |

13 des 21 émettent du retour de force sous leur loader actuel. Sega Rally 3 a huit entrées (Standard,
Alpine, Artic, Canyon, Master, Safari, Tropical, Tropical478) : huit dossiers qui partagent le même
`Rally.exe`, donc un seul module. Deux jeux seulement sont en 64 bits, Crazy Ride et SWDC (Unreal Engine) ;
les dix-neuf autres sont en PE32.

### Écartés, avec la raison

- **Enveloppe Sentinel HASP** : toute la gamme Raw Thrills, huit titres qui marchent pourtant (Batman, Cars,
  Dirty Drivin, H2 Overdrive, Fast & Furious, FnF SuperCars, FnF Tokyo Drift, Nicktoons Nitro). Binaire
  renommé `sdaemon.exe` partout, section `.protect` jusqu'à 2 Mo ; quatre ont la chaîne `HASP` en clair,
  deux embarquent la bibliothèque d'exécution. Réserve : chaînes cherchées dans les 4 premiers Mo
  seulement, deux binaires font 51 et 139 Mo.
- **Bibliothèque de dongle HASP à côté de l'exe**, sections propres : les trois Wangan Midnight, WMMT5
  (`hasp_windows_x64_109906.dll`), WMMT5DX+ (`…_106482.dll`), WMMT6 (`…_28756.dll`). Ils démarrent et
  donnent beaucoup de retour de force (68 769, 68 622, 67 819 paquets) : le dongle ne les bloque pas en
  l'état. À juger.
- **Section `.mackt`** : Initial D 7 AAX et Initial D 8 Infinity, la même section que sur les binaires Raw
  Thrills, mis de côté par prudence. Initial D 6 est propre (liste ci-dessus) ; les noms des fichiers
  racontent leur histoire : `id6_dump_.exe`, `InitialD7_…_dumped_.exe`, `InitialD8_…_redumped_.exe`.
- **Ne fonctionnent pas**, quelle que soit la protection : Let's Go Safari, Sega Racing Classic, Storm
  Racer G, Wacky Races.
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
