# Aion 4.8 Wardrobe

An account-wide appearance collection, native character preview and saved outfits for the English Aion 4.8 NA 64-bit client. Open **Game Menu > Additional Functions > Wardrobe**, or type `/wardrobe`.

Unlocking consumes one **Appearance Unlock** ticket (item **168100001**) and keeps the source equipment. Collected appearances remain available across the account; applying them costs no extra ticket or Kinah. Try on several compatible appearances, then apply the selection together. Headwear, shields, wings, costumes and race/gender restrictions are supported. Up to 20 named outfits can be saved per account. See the [player guide](docs/WARDROBE.md).

## Download

Choose **Code > Download ZIP** on [GitHub](https://github.com/AngelShade/aion-wardrobe) and extract it, or:

```powershell
git clone https://github.com/AngelShade/aion-wardrobe.git
```

This is a complete server source repository based on [Aion Central Market](https://github.com/AngelShade/aion-central-market), commit `de3f62975`. Wardrobe uses that server's authenticated browser session, client-item catalog and transactional inventory safeguards. Those dependencies are included. The [Beyond Aion upstream README](docs/UPSTREAM_README.md), Git history and GPL-3.0 license are retained.

The Wardrobe client builder adds its own menu, native preview and appearance item. The local project's enlarged Inventory, expanded Warehouse, private Cash Shop, Graphics menu and Journey modifications are not added. Original client DLLs, client archives, extracted artwork, compiled server JARs, player data and credentials are not included.

## Which folders to use

| Input | Choose this folder | Example |
| --- | --- | --- |
| Server build | Downloaded repository root containing `pom.xml` and `game-server/src` | `D:\Servers\aion-wardrobe` |
| Active server deployment | The GameServer folder used by your startup script, containing `libs` and `config` | `D:\Servers\Live\game-server` |
| `--client-path` / `-ClientPath` | Aion game root containing `bin64`, `Data`, `L10N` and `Plugin` | `D:\Games\Aion 4.8 NA` |
| `--output` / `-PreparedPath` | A new staging folder outside Aion | `D:\Wardrobe-Staged` |
| `--java` | JDK 25's `bin/java.exe` file | `C:\Program Files\Java\jdk-25\bin\java.exe` |

Do not select `bin64` as the client root. If the DLL is `D:\Games\Aion 4.8 NA\bin64\game.dll`, enter `D:\Games\Aion 4.8 NA`. Paths are examples; use your own locations. Copy a folder's path from File Explorer's address bar with **Ctrl+L**, then **Ctrl+C**.

Players install the prepared client files after their server operator deploys the service. Server source, Maven, the database setup and ticket distribution are the operator's responsibility.

## Requirements

- JDK 25, Maven and MySQL/MariaDB for the included server.
- The original English **Aion 4.8 NA 64-bit client**. The builder verifies Game.dll SHA-256 `5334cf2164468678e45fe1a5decf58a0fbc4fd7f22cfdcbb87d28edce8d2c11c`, CrySystem and Awesomium. A customized Game.dll is refused; use a separate clean matching client to prepare this standalone package.
- Python 3.12 and Visual Studio 2022 **C++ Build Tools** with the Windows SDK to build the native preview/icon bridge. No extracted PNGs or external Python modules are required for package preparation.
- Client and GameServer on the **same computer**: the supplied URL is `http://127.0.0.1:8091/market/wardrobe`. Remote hosting requires coordinated changes to the native URL checks, browser URL, server listener and transport. Changing only the URL is insufficient.

## Server build and deployment

1. Open PowerShell in the repository root and run:

   ```powershell
   mvn -pl game-server -am clean package '-Dassembly.skipAssembly=true' '-Dmaven.test.skip=true'
   ```

2. Wait for **BUILD SUCCESS**. The result is `game-server/target/game-server-4.8-SNAPSHOT.jar`. Build your own JAR; do not replace a custom server with someone else's compiled binary.
3. Back up your database, current server JAR, active configuration and item templates. If Central Market has been used, keep `inventory`, `item_stones` and all `central_market_*` tables in one consistent backup, alongside `wardrobe_*`.
4. Have players log out, then stop GameServer normally. In the **active deployment folder**, replace `libs/game-server-4.8-SNAPSHOT.jar` with the newly built JAR. Copy this repository's `game-server/config/wardrobe` to `config/wardrobe`, and `game-server/config/central-market` to `config/central-market`.
5. Deploy the Appearance Unlock item template from `game-server/data/static_data/items/item_templates.xml`. When merging into an existing customized catalog, add only item **168100001**, retaining the other item definitions. This repository's full source build uses its supplied matching data.
6. In the active server's `config/mygs.properties`, set:

   ```properties
   gameserver.centralmarket.enable = true
   gameserver.centralmarket.bind = 127.0.0.1
   gameserver.centralmarket.port = 8091
   ```

7. Start GameServer. Check **Central Market ready**, **Wardrobe ready** and the listener on port 8091. Wardrobe starts with the included Central Market service. The schema creates four dedicated InnoDB tables automatically; the existing inventory tables must also use InnoDB. An unauthenticated `/market/wardrobe/state` returning **403** is expected.

An existing server developer can also merge the Wardrobe sources, startup/route additions and unlock item from this branch into their corresponding Central Market build. Do not blindly overwrite customized server source, configuration or catalogs.

## Client package and installation

From the repository root, with your clean matching client available:

```powershell
python client-mods/wardrobe/build_package.py --client-path 'D:\Games\Aion 4.8 NA' --java 'C:\Program Files\Java\jdk-25\bin\java.exe' --output 'D:\Wardrobe-Staged'
```

Preparation reads the original client and writes a separate staged package. It compiles the native bridge locally, adds the Appearance Unlock definition and English strings, and builds an index into the recipient's own Items.pak. Item artwork stays inside that client archive.

Review `D:\Wardrobe-Staged\manifest.json`, **fully close Aion**, then install:

```powershell
./client-mods/wardrobe/Install.ps1 -ClientPath 'D:\Games\Aion 4.8 NA' -PreparedPath 'D:\Wardrobe-Staged'
```

The installer checks input/output hashes, backs up all replacements, and prints the backup path. It preserves the stock model key while isolating addon archive signatures. Launch the **64-bit client** and reconnect to the matching server. Each player needs the client changes; a staged manifest belongs to the exact client path and original files used to prepare it.

## Appearance Unlock tickets

Provide item **168100001** through your server's own shop, rewards or distribution process. An administrator with existing permission can use `//add 168100001 1` for a controlled test. The public package includes the server/client item definitions; the local private Cash Shop and its 30,000 Kinah offer are not bundled. No accounts receive tickets or skins automatically.

## Verification and rollback

See [validation results and commands](docs/WARDROBE_VALIDATION.md). Before opening access to players, check the Wardrobe menu, item tooltip, mouse rotation/zoom, helmet/combat/wings controls, unlock confirmation, exactly one ticket consumed, source equipment retained, free apply, saved outfits and relogin persistence. Also summon a pet and reopen Additional Functions to confirm stock model validation and addon loading together.

To restore the client, close Aion and use the exact backup path printed by installation:

```powershell
./client-mods/wardrobe/Restore.ps1 -ClientPath 'D:\Games\Aion 4.8 NA' -BackupPath 'D:\Games\Aion 4.8 NA\TransmogMenu-backups\signed-DATE-ID'
```

To revert the server, log players out, stop normally, and restore the matching JAR/configuration/catalog backup. Keep the Wardrobe tables so account collections are retained. Client restoration does not undo unlocked collections or server equipment appearance changes. Central Market custody must be reconciled before rolling back to a server that lacks that service.
