# Wardrobe sharing validation — 2026-10-01

These checks validate the public sharing branch and isolated fixtures. They do not establish live in-game acceptance on a recipient's computer.

## Checks completed

- Full Maven reactor build: server parent, Commons and GameServer all **BUILD SUCCESS**, using JDK 25. The new Wardrobe services and Central Market session/route integration compile together.
- Standalone client preparation from copied original inputs: **12 verified files**, native bridge compiled with MSVC, archive contents verified, original stock signatures verified and model key preserved. Preparation did not change the installed client.
- Native lifecycle fixture: browser event pump, nine lifecycle calls, preserved argument registers, exact stack allocation/destructor vtable relocation and unsupported-client rejection passed.
- Native character fixture: original Game.dll camera/rotation calculations, unopened-camera initialization, Win32 drag/wheel input, invalid-camera recovery, zoom bounds, modal blocking, ownership, queued-thread guards and preview controls passed.
- Actual Aion WebKit fixture at **1024×740**, **1920×1052** and **3440×1412**: confirmation flows, tabs, preview controls, queued acknowledgement and polling stop passed. Additional checks cover headwear, locked-skin apply blocking, removal of a blocked change, wing preview without equipped wings, and exactly one fixture ticket consumed.
- Catalog check: **45,275 client-backed appearances**, including 212 wings and 2,092 costumes. It recovered 5,597 eligible permanent appearances across 47 item groups and checked every weapon-group compatibility pair.
- Database fixture: **46 transaction and appearance checks** passed using a temporary schema that copies only the inventory structure. No live character data changed.
- Client install guard correctly refused installation while an Aion process was running. Installation and restoration are tested only against copied client files; the fixture supplies a controlled client-closed state. The real install guard remains unchanged.

## Reproduce the checks

From the repository root, build the server and prepare the client package using the root README. Keep the resulting native DLLs, archives, JARs and client inputs outside Git.

Native lifecycle fixture:

```powershell
python client-mods/native-icon-bridge/verify_wardrobe_patch.py --game-dll 'D:\OriginalClient\bin64\game.dll'
```

Browser fixture using the recipient's original Awesomium libraries:

```powershell
python client-mods/native-icon-bridge/verify_wardrobe_browser.py --browser-bin 'D:\OriginalClient\bin64'
```

Native character fixture, from a Visual Studio x64 Native Tools terminal:

```powershell
cl /nologo /std:c++17 /EHsc /O2 client-mods/native-icon-bridge/verify_wardrobe_native.cpp /link user32.lib
./verify_wardrobe_native.exe 'D:\OriginalClient\bin64\game.dll'
```

The Java catalog fixture takes the matching server item templates and the newly staged `AionIconBridge.index`. The database fixture takes an existing deployment folder with database settings, creates an isolated `aion_wardrobe_check_*` schema, copies the inventory table structure and drops that schema afterward. Run it only with a database account already authorized to create/drop that fixture schema. Its working directory is the repository root because it reads `game-server/config/wardrobe/schema.sql`.

Before opening Wardrobe to players, complete the live client checks listed in the root README, including pet visibility, Additional Functions, ticket consumption, source-equipment retention, free application, outfits and relogin persistence.
