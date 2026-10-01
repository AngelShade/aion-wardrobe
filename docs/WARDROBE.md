# Wardrobe

Wardrobe replaces **Transmog** in **Game Menu → Additional Functions**. It opens
inside Aion; `/wardrobe` opens the same window.

## Unlock and apply an appearance

1. Obtain an **Appearance Unlock** ticket (item **168100001**) from your server operator
   or the shop/rewards they configure. This public package does not include the private Cash Shop.
2. Keep the equipment whose appearance you want in Inventory or equip it.
3. Open Wardrobe, choose **Ready to unlock**, and select its appearance.
4. Choose **Unlock Appearance** and confirm. This consumes **one Appearance
   Unlock**. The equipment stays with you.
5. Select equipment on the right and choose **Try On** on an appearance in the
   left collection. Add appearances to several equipment items, then choose
   **Apply Changes** and confirm. Applying unlocked skins costs no item or Kinah.

Unlocks belong to the account and remain available after the source equipment
is sold or removed. Unlocking the same appearance again does not consume a ticket.
The source's current appearance is collected, including an existing permanent
remodel. Enchantment, sockets, fusion, tempering and other equipment attributes
stay intact. Applying a skin keeps the target equipment's dye.

**All appearances** shows eligible client-supported equipment skins; **Locked**
shows skins not collected yet. Search and category buttons narrow the list.
Selecting equipment also filters by compatible equipment type. Race and gender
restrictions still apply. Permanent appearance items and equipment with stats
can both be collected, including items excluded by normal NPC remodeling.
Weapons accept skins of the same weapon type. Armor accepts Cloth, Leather,
Chain, Plate and costume skins on matching body slots. Shield skins work on
shields; headwear and wings use their respective slots. The Costumes category
includes generic appearance armor and clothing. Changing categories clears the
equipment filter so a selected weapon cannot hide the costume collection.
Temporary appearances, test items, Power Shards, Stigmas and accessories without
a visible appearance are excluded.

Hover an item icon for Aion's item tooltip. The center panel contains Aion's
existing native character preview, including its normal, zoom and Aethertech
views. **Left**, **Right**, **Zoom**, **Helmet** and **Combat** use the native
preview controller. Trying on appearances does not change equipment or require
a server request. Locked skins can be tried on but must be unlocked before apply.
**Wings** uses the client's wing-display pose. Trying on a wing appearance
switches to that pose automatically. Wings can be previewed without equipped
wings; applying a wing appearance still requires a wing item.
**Reset Preview** discards staged changes. **Restore Appearance** stages the
selected equipment's original skin; choose **Apply Changes** to save it.

## Saved outfits

Equip the items you want, apply their collected appearances, then choose
**Saved Outfits → Save Current Outfit**. Up to 20 named outfits are saved per
account. Saving with an existing name asks to replace it.

**Try On Outfit** stages appearances on the currently equipped items in its saved
slots. **Apply Changes** saves the whole selection in one transaction.
Every slot must exist and match the stored appearance. If any slot is incompatible,
the whole operation is rejected. Outfits contain appearances, not equipment,
stats or dyes. Original appearances are stored as “use this item's original skin”.

## Server files and database

The GameServer creates four dedicated InnoDB tables at startup from
`config/wardrobe/schema.sql`:

- `wardrobe_accounts`: account transaction locks.
- `wardrobe_skins`: permanent account appearance collection.
- `wardrobe_outfits`: named equipped appearance sets.
- `wardrobe_requests`: completed request receipts to prevent repeated consumption.

The existing `inventory` table must also use InnoDB. Ticket consumption and the
collection insert commit in one transaction. Equipment appearance changes commit
before the live item packets are sent; a failed item update rolls back the entire
appearance batch. One equipment appearance broadcast follows each committed batch.
A refresh failure after a committed update
disconnects the character and blocks stale inventory saves until its next login.

The dedicated item is **168100001**, Appearance Unlock. The server template is in
`data/static_data/items/item_templates.xml`. The client builder installs the matching
item and English strings. Operators distribute it through their existing rewards
or shops. An already authorized administrator can use `//add 168100001 1` for testing. The existing Pattern Reshaper item remains available
for standard NPC remodeling.

Deploy the compiled GameServer JAR, `config/wardrobe/`, `config/central-market/`
and item templates together. Restart GameServer after characters are offline. No account
collection is populated automatically and no existing equipment is consumed.

## Client installation and rollback

Use `client-mods/wardrobe/build_package.py` and its `Install.ps1` installer,
following the root README. This standalone sharing builder requires a clean,
matching original 4.8 NA English 64-bit client. It refuses a customized Game.dll
and does not combine the local project's Inventory, Warehouse, Graphics or
Journey patches. Package preparation never changes the installed client.
The manifest includes Wardrobe, its matching client item, native icon index
and signed addon package. Fully close Aion before installation.
The stock `Pub.key` remains unchanged; addon archives use `Addon.key`.

Restore the exact recorded client backup with `Restore.ps1` while Aion is closed.
It restores the old client item archive and icon index together. Restore the
matching server deployment backup if reverting the service. Keep the new database
tables when reverting so unlocked collections are not lost.

The Wardrobe menu and `/wardrobe` open the collection. Existing NPC remodeling
retains its original material and cost rules.

## Verification

Appearance browsing caches short-lived account collection reads and compatible
race/gender catalogs. The client keeps a short browsing cache and redraws only
changed panels. Selecting or trying on a skin stays local. Native preview updates
are combined after a short delay; item icons retain the 512-texture native cache.

The native integration reparents the client's three character views while Wardrobe
is open and restores them when it closes or either dialog is destroyed. It does
not recreate a character model. Confirmation dialogs temporarily hide the native
views so the model cannot cover the confirmation text. Other Item Preview windows
retain their existing callback and detach the Wardrobe preview before opening.

Mouse controls: left-drag to rotate, right-drag to reposition, and scroll over
the character to zoom. Reset Preview restores the camera and clears staged
appearances. Mouse input over other panels keeps its normal behavior.

The build, isolated database transaction checks, native route/fullscreen/preview
checks and browser fixture checks are automated. Browser checks exercise search,
unlock confirmation, repeated clicks, saved outfits and 1024–5120 pixel layouts.
These fixtures do not unlock anything on a real account.

After installation, verify in Aion: menu entry, fullscreen layout, cursor-following
item tooltips, native preview, one ticket consumed for one unlock, source equipment
retained, free apply, outfit apply and persistence after relogging. Also summon a
pet and open Additional Functions to confirm stock model and addon signing together.
