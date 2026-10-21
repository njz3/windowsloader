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
| 24 | 21/10 | Ajouter le support des deux versions de Daytona Championship USA | ⚠️ `games/daytona.cpp`, `vdinput.cpp`, `d3d11hook.cpp`, menu F10 en Direct3D 11. Vérifié en mode agent sur les deux éditions, de la démo à la course : défis de sécurité tenus (le jeu quittait à 54 s), SKU (la NSE l'attend), pièce → crédit, Start, choix au volant (à droite → Championnat) et à l'accélérateur, circuit, voiture, course : accélérateur, frein (57 → 18 mph en 2 s), rapports, retour de force (menus : volant à droite → +0,71 vers la gauche ; course : volant à gauche → −0,45 à −0,49 en moyenne), F10 (pages Contrôles et Retour de force), réseau local refusé. Corrigé en route : la carte graphique du portable (le jeu manquait d'espace d'adressage sur l'Intel). Reste à essayer à la main : volant réel et sens du retour de force en course, levier en H, F11, bandes noires en plein écran |
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
- **Mes essais et vJoy.** Les essais en mode agent ne lisent pas les manettes et n'envoient rien à vJoy.
  Un lancement normal ouvre vJoy (joueur 1) avec le retour de force. `SDL_JOYSTICK_IGNORE_DEVICES=0x1234/0xBEAD`
  ne l'empêche pas (essayé le 21/10 : vJoy ouvert 15 s, en démo, forces nulles, vJoy annoncé libre par la
  session Flycast). À faire : une option du lanceur pour couper manettes et retour de force.
- **Direct3D 11 et l'affichage.** Le `Present` de DXGI remplit toute la fenêtre : en Direct3D 11 (Daytona),
  le rapport d'aspect ne tient que dans les modes fenêtre (la fenêtre prend la forme de l'image) ; en plein
  écran, une image d'une autre forme que l'écran est étirée. À faire : une fenêtre enfant à la place de
  l'image, comme sortie de la chaîne d'échange. Pendant mes essais, `Present` rend `DXGI_STATUS_OCCLUDED`
  (fenêtre jugée invisible, Bureau à distance réduit ?) : le jeu tourne sans synchro, à 50–190 i/s.
- **`GameData\debug.txt` de Daytona** : 3 Go (2,3 Go avant mes essais, 0,7 Go ajoutés par eux avant le
  plafond), et 27 vidages `*.dmp` anciens plus un des miens (21/10 22:07, 231 Ko). Rien supprimé : à
  l'utilisateur de voir s'il les garde.
- **inpout32 et le matériel du PC.** Sega Rally 3 charge `..\Shell\inpout32.dll` et écrit sur le port
  0x1020. Vérifié le 21/10 : aucun pilote de ports n'est installé ici et le jeu tourne sans droits
  administrateur, donc les écritures des premiers essais n'ont rien atteint. Désormais la vraie DLL n'est
  jamais chargée (ports émulés).

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
