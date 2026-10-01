package com.aionemu.gameserver.services;

import java.nio.file.Files;
import java.nio.file.Path;
import java.sql.*;
import java.util.*;
import java.util.stream.Collectors;
import java.util.concurrent.ConcurrentHashMap;

import com.alibaba.fastjson2.JSON;
import com.aionemu.commons.database.DatabaseFactory;
import com.aionemu.gameserver.GameServer;
import com.aionemu.gameserver.dao.InventoryDAO;
import com.aionemu.gameserver.dataholders.DataManager;
import com.aionemu.gameserver.model.gameobjects.Item;
import com.aionemu.gameserver.model.gameobjects.Persistable.PersistentState;
import com.aionemu.gameserver.model.gameobjects.player.Player;
import com.aionemu.gameserver.model.items.storage.StorageType;
import com.aionemu.gameserver.model.templates.item.ItemTemplate;
import com.aionemu.gameserver.network.aion.serverpackets.SM_UPDATE_PLAYER_APPEARANCE;
import com.aionemu.gameserver.services.item.ItemPacketService;
import com.aionemu.gameserver.services.item.ItemPacketService.ItemDeleteType;
import com.aionemu.gameserver.services.item.ItemPacketService.ItemUpdateType;
import com.aionemu.gameserver.utils.PacketSendUtility;
import com.aionemu.gameserver.utils.idfactory.IDFactory;
import com.aionemu.gameserver.world.World;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/** Permanent account appearances. Unlocks and ticket consumption commit together. */
public final class WardrobeService {

	private static final Logger log = LoggerFactory.getLogger(WardrobeService.class);
	private static volatile List<Entry> catalog = List.of();
	private static volatile Map<Integer,ItemTemplate> byId = Map.of();
	private static volatile Map<Integer,Entry> entries = Map.of();
	private static final Map<String,List<Entry>> permittedCatalog = new ConcurrentHashMap<>();
	private static final Map<Integer,CollectionRead> collectionReads = new ConcurrentHashMap<>();
	private record CollectionRead(Object connection, long expires, Set<Integer> unlocked, List<Map<String,Object>> outfits) {}
	private static volatile boolean ready;
	private record Entry(ItemTemplate template, String search, String category) {}
	private WardrobeService() {}

	public static void start() throws Exception {
		try (Connection c = DatabaseFactory.getConnection(); Statement s = c.createStatement()) {
			for (String sql : Files.readString(Path.of("config/wardrobe/schema.sql")).split(";"))
				if (!sql.isBlank()) s.execute(sql);
			try (ResultSet r = s.executeQuery("SELECT TABLE_NAME,ENGINE FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND (TABLE_NAME='inventory' OR TABLE_NAME LIKE 'wardrobe_%')")) {
				while (r.next()) if (!"InnoDB".equalsIgnoreCase(r.getString(2)))
					throw new SQLException("Wardrobe requires InnoDB: " + r.getString(1));
			}
		}
		catalog = DataManager.ITEM_DATA.getItemTemplates().stream()
			.filter(t -> WardrobeRules.eligible(t) && CentralMarketHttpService.hasClientItem(t.getTemplateId()))
			.sorted(Comparator.comparing(ItemTemplate::getName).thenComparingInt(ItemTemplate::getTemplateId))
			.map(t -> new Entry(t,t.getName().toLowerCase(Locale.ROOT),WardrobeRules.category(t))).toList();
		byId = catalog.stream().collect(Collectors.toUnmodifiableMap(e -> e.template().getTemplateId(),Entry::template));
		entries = catalog.stream().collect(Collectors.toUnmodifiableMap(e -> e.template().getTemplateId(), e -> e));
		permittedCatalog.clear(); collectionReads.clear();
		if (DataManager.ITEM_DATA.getItemTemplate(WardrobeRules.UNLOCK_ITEM) == null)
			throw new IllegalStateException("Appearance Unlock item template is missing.");
		ready = true;
		log.info("Wardrobe ready: {} permanent appearances; account collection enabled", catalog.size());
	}

	private static void available(Player p) {
		if (!ready) throw new IllegalStateException("Wardrobe is unavailable.");
		if (!p.isOnline() || World.getInstance().getPlayer(p.getObjectId()) != p || p.getClientConnection() == null)
			throw new IllegalArgumentException("Log in, then reopen Wardrobe.");
	}

	private static List<Item> equipment(Player p) {
		List<Item> items = new ArrayList<>(p.getEquipment().getEquippedItems());
		items.addAll(p.getInventory().getItems());
		return items.stream().filter(i -> WardrobeRules.eligible(i.getItemTemplate())
			&& i.getExpireTime() == 0 && i.getPendingTuneResult() == null).toList();
	}

	static Set<Integer> unlocked(Connection c, int account) throws SQLException {
		return rows(c,"SELECT item_id FROM wardrobe_skins WHERE account_id=?",account).stream()
			.map(r -> (int)number(r,"item_id")).collect(Collectors.toSet());
	}

	public static Map<String,Object> snapshot(Player p, Map<String,String> args) throws Exception {
		available(p);
		Object guard = p.getClientConnection();
		if (guard == null) throw new IllegalArgumentException("Log in, then reopen Wardrobe.");
		synchronized (guard) {
			available(p);
			try (Connection c = DatabaseFactory.getConnection()) {
				int account = p.getAccount().getId();
				long now = System.currentTimeMillis();
				CollectionRead collection = collectionReads.get(account);
				if (collection == null || collection.connection() != guard || collection.expires() < now || args.containsKey("refresh")) {
					if (collectionReads.size() > 1024) collectionReads.entrySet().removeIf(e -> e.getValue().expires() < now);
					collection = new CollectionRead(guard, now+3000, unlocked(c,account),
						rows(c,"SELECT name,skins_json,updated_at FROM wardrobe_outfits WHERE account_id=? ORDER BY updated_at DESC",account));
					collectionReads.put(account,collection);
				}
				Set<Integer> unlocked = collection.unlocked();
				List<Item> equipment = equipment(p);
				Map<Integer,List<Item>> sources = equipment.stream().filter(i -> byId.containsKey(i.getItemSkinTemplate().getTemplateId()))
					.collect(Collectors.groupingBy(i -> i.getItemSkinTemplate().getTemplateId()));
				String query = args.getOrDefault("q","").strip().toLowerCase(Locale.ROOT);
				if (query.length() > 60) query = query.substring(0,60);
				final String term = query;
				String category = args.getOrDefault("category","All"), filter = args.getOrDefault("filter","unlocked");
				int targetId = integer(args,"target",0);
				Item target = equipment.stream().filter(i -> i.getObjectId() == targetId).findFirst().orElse(null);
				List<Entry> allowed = permittedCatalog.computeIfAbsent(p.getRace()+":"+p.getGender(),
					key -> catalog.stream().filter(e -> WardrobeRules.permitted(e.template(),p.getRace(),p.getGender())).toList());
				// Owned/unlocked collections are small. Do not walk the complete client catalog for these views.
				Collection<Entry> candidates = filter.equals("unlocked") || filter.equals("owned")
					? (filter.equals("unlocked") ? unlocked : sources.keySet()).stream().map(entries::get).filter(Objects::nonNull)
						.sorted(Comparator.comparing((Entry e) -> e.template().getName()).thenComparingInt(e -> e.template().getTemplateId())).toList()
					: allowed;
				List<Entry> matches = candidates.stream().filter(e -> e.search().contains(term))
					.filter(e -> category.equals("All") || e.category().equals(category))
					.filter(e -> WardrobeRules.permitted(e.template(),p.getRace(),p.getGender()))
					.filter(e -> target == null || WardrobeRules.compatible(target.getItemTemplate(),e.template()))
					.filter(e -> switch (filter) {
						case "unlocked" -> unlocked.contains(e.template().getTemplateId());
						case "owned" -> sources.containsKey(e.template().getTemplateId()) && !unlocked.contains(e.template().getTemplateId());
						case "locked" -> !unlocked.contains(e.template().getTemplateId());
						default -> true;
					}).toList();
				int pages = Math.max(1,(matches.size()+35)/36), page = Math.min(pages,Math.max(1,integer(args,"page",1)));
				List<Map<String,Object>> skins = matches.stream().skip((page-1)*36L).limit(36)
					.map(e -> skinView(e.template(),unlocked,sources)).toList();
				Map<String,Object> state = new LinkedHashMap<>();
				state.put("character",p.getName()); state.put("unlocked",unlocked.stream().map(byId::get).filter(Objects::nonNull)
					.filter(t -> WardrobeRules.permitted(t,p.getRace(),p.getGender())).count());
				state.put("total",allowed.size());
				state.put("tickets",p.getInventory().getItemCountByItemId(WardrobeRules.UNLOCK_ITEM));
				state.put("ticketItem",WardrobeRules.UNLOCK_ITEM); state.put("categories",List.of("All","Weapons","Armor","Costumes","Headwear","Shields","Wings"));
				state.put("equipment",equipment.stream().map(WardrobeService::itemView).toList());
				state.put("skins",skins); state.put("results",matches.size()); state.put("page",page); state.put("pages",pages);
				state.put("target",target == null ? 0 : targetId);
				state.put("outfits",collection.outfits());
				Set<Integer> outfitIds = new HashSet<>();
				for (var outfit : collection.outfits()) {
					Map<String,Integer> saved = JSON.parseObject(outfit.get("skins_json").toString(),new com.alibaba.fastjson2.TypeReference<Map<String,Integer>>() {});
					if (saved != null) outfitIds.addAll(saved.values());
				}
				state.put("outfitAppearances",outfitIds.stream().map(byId::get).filter(Objects::nonNull)
					.filter(t -> WardrobeRules.permitted(t,p.getRace(),p.getGender())).map(t -> skinView(t,unlocked,sources)).toList());
				int selected = integer(args,"skin",0);
				ItemTemplate skin = byId.get(selected);
				if (skin != null && WardrobeRules.permitted(skin,p.getRace(),p.getGender()))
				{
					Map<String,Object> selectedView = skinView(skin,unlocked,sources);
					selectedView.put("compatible",target != null && WardrobeRules.compatible(target.getItemTemplate(),skin));
					state.put("selected",selectedView);
				}
				return state;
			}
		}
	}

	private static Map<String,Object> skinView(ItemTemplate t, Set<Integer> unlocked, Map<Integer,List<Item>> sources) {
		Map<String,Object> v = new LinkedHashMap<>();
		v.put("item",t.getTemplateId()); v.put("name",t.getName()); v.put("category",WardrobeRules.category(t));
		v.put("type",WardrobeRules.typeName(t)); v.put("quality",t.getItemQuality().name());
		v.put("unlocked",unlocked.contains(t.getTemplateId()));
		v.put("group",t.getItemGroup().name()); v.put("slots",t.getItemGroup().getValidEquipmentSlots());
		v.put("sources",sources.getOrDefault(t.getTemplateId(),List.of()).stream()
			.map(i -> Map.of("object",i.getObjectId(),"name",i.getItemName(),"equipped",i.isEquipped())).toList());
		return v;
	}

	private static Map<String,Object> itemView(Item i) {
		Map<String,Object> result = new LinkedHashMap<>(Map.of("object",i.getObjectId(),"item",i.getItemId(),"skin",i.getItemSkinTemplate().getTemplateId(),
			"name",i.getItemName(),"equipped",i.isEquipped(),"slot",i.getEquipmentSlot(),"type",WardrobeRules.typeName(i.getItemTemplate()),
			"tooltip","item="+i.getItemId()+"&count=1&enchant_count="+i.getEnchantLevel()+"&authorize_count="+i.getTempering()));
		result.put("group",i.getItemTemplate().getItemGroup().name());
		result.put("slots",i.getItemTemplate().getItemGroup().getValidEquipmentSlots());
		return result;
	}

	public static String action(Player p, Map<String,String> args, String request) throws Exception {
		available(p);
		Object guard = p.getClientConnection();
		if (guard == null) throw new IllegalArgumentException("Log in, then reopen Wardrobe.");
		synchronized (guard) {
			available(p);
			if (p.isDead() || p.isTrading() || GameServer.isShuttingDownSoon())
				throw new IllegalArgumentException("Wardrobe is unavailable during trade, death or shutdown.");
			if (!InventoryDAO.store(p)) throw new SQLException("Inventory save failed.");
			List<Runnable> committed = new ArrayList<>();
			try (Connection c = DatabaseFactory.getConnection()) {
				c.setAutoCommit(false);
				boolean saved = false;
				try {
					int account = p.getAccount().getId();
					update(c,"INSERT IGNORE INTO wardrobe_accounts VALUES(?)",account);
					row(c,"SELECT account_id FROM wardrobe_accounts WHERE account_id=? FOR UPDATE",account);
					Map<String,Object> receipt = row(c,"SELECT result FROM wardrobe_requests WHERE request_id=? AND account_id=?",request,account);
					if (receipt != null) return receipt.get("result").toString();
					String result = switch (args.getOrDefault("action","")) {
						case "unlock" -> unlock(c,p,args,committed);
						case "apply" -> apply(c,p,args,committed);
						case "applyChanges" -> applyChanges(c,p,args,committed);
						case "restore" -> restore(c,p,args,committed);
						case "saveOutfit" -> saveOutfit(c,p,args);
						case "applyOutfit" -> applyOutfit(c,p,args,committed);
						case "deleteOutfit" -> deleteOutfit(c,account,args);
						default -> throw new IllegalArgumentException("Select a Wardrobe action.");
					};
					update(c,"DELETE FROM wardrobe_requests WHERE created_at<?",System.currentTimeMillis()-86400000);
					update(c,"INSERT INTO wardrobe_requests VALUES(?,?,?,?)",request,account,result,System.currentTimeMillis());
					c.commit(); saved = true;
					for (Runnable change : committed) change.run();
					collectionReads.remove(account);
					if (Set.of("apply","restore","applyChanges","applyOutfit").contains(args.get("action")) && !committed.isEmpty())
						PacketSendUtility.broadcastPacket(p,new SM_UPDATE_PLAYER_APPEARANCE(p.getObjectId(),p.getEquipment().getEquippedForAppearance()),true);
					return result;
				} catch (Exception e) {
					if (!saved) c.rollback();
					else {
						InventoryDAO.quarantineMarketInventory(p.getObjectId());
						log.error("Committed Wardrobe update could not refresh {}. Inventory saves blocked until login.",p,e);
						p.getClientConnection().close();
					}
					throw e;
				}
			}
		}
	}

	private static Item target(Player p, Map<String,String> args) {
		int id = integer(args,"target",0);
		return equipment(p).stream().filter(i -> i.getObjectId() == id).findFirst()
			.orElseThrow(() -> new IllegalArgumentException("Select compatible equipment in Inventory or equipped slots."));
	}

	private static ItemTemplate skin(Player p, Map<String,String> args) {
		ItemTemplate skin = byId.get(integer(args,"skin",0));
		if (skin == null || !WardrobeRules.permitted(skin,p.getRace(),p.getGender()))
			throw new IllegalArgumentException("This appearance is unavailable for your character.");
		return skin;
	}

	private static String unlock(Connection c, Player p, Map<String,String> args, List<Runnable> committed) throws SQLException {
		ItemTemplate skin = skin(p,args);
		int account = p.getAccount().getId();
		if (unlocked(c,account).contains(skin.getTemplateId())) return "Appearance already unlocked.";
		int sourceId = integer(args,"source",0);
		Item source = equipment(p).stream().filter(i -> i.getObjectId() == sourceId
			&& i.getItemSkinTemplate().getTemplateId() == skin.getTemplateId()).findFirst()
			.orElseThrow(() -> new IllegalArgumentException("Keep the appearance item in Inventory or equipped slots to unlock it."));
		lockItem(c,p,source);
		Item ticket = p.getInventory().getItemsByItemId(WardrobeRules.UNLOCK_ITEM).stream().filter(i -> i.getItemCount() > 0).findFirst()
			.orElseThrow(() -> new IllegalArgumentException("You need one Appearance Unlock item."));
		lockItem(c,p,ticket);
		commitUnlock(c,account,skin.getTemplateId(),p.getObjectId(),ticket.getObjectId(),ticket.getItemCount());
		long remaining = ticket.getItemCount()-1;
		committed.add(() -> {
			ticket.setItemCount(remaining); ticket.setPersistentState(PersistentState.UPDATED);
			if (remaining == 0) {
				p.getInventory().remove(ticket);
				ItemPacketService.sendItemDeletePacket(p,StorageType.CUBE,ticket,ItemDeleteType.USE);
				IDFactory.getInstance().releaseId(ticket.getObjectId());
			} else ItemPacketService.sendItemUpdatePacket(p,StorageType.CUBE,ticket,ItemUpdateType.DEC_ITEM_USE);
		});
		return "Unlocked " + skin.getName() + ". Equipment kept.";
	}

	static void commitUnlock(Connection c,int account,int skin,int character,int ticket,long count) throws SQLException {
		if (count < 1) throw new IllegalArgumentException("You need one Appearance Unlock item.");
		if (update(c,"INSERT IGNORE INTO wardrobe_skins VALUES(?,?,?,?)",account,skin,System.currentTimeMillis(),character) != 1)
			throw new IllegalArgumentException("Appearance already unlocked.");
		int changed = count == 1
			? update(c,"DELETE FROM inventory WHERE item_unique_id=? AND item_owner=? AND item_id=? AND item_location=0 AND is_equipped=0 AND item_count=1",ticket,character,WardrobeRules.UNLOCK_ITEM)
			: update(c,"UPDATE inventory SET item_count=item_count-1 WHERE item_unique_id=? AND item_owner=? AND item_id=? AND item_location=0 AND is_equipped=0 AND item_count=?",ticket,character,WardrobeRules.UNLOCK_ITEM,count);
		if (changed != 1) throw new IllegalArgumentException("Appearance Unlock item changed. Refresh Wardrobe.");
	}

	private static String apply(Connection c,Player p,Map<String,String> args,List<Runnable> committed) throws SQLException {
		Item target = target(p,args); ItemTemplate skin = skin(p,args);
		if (!unlocked(c,p.getAccount().getId()).contains(skin.getTemplateId()))
			throw new IllegalArgumentException("Unlock this appearance first.");
		if (!WardrobeRules.compatible(target.getItemTemplate(),skin))
			throw new IllegalArgumentException("This appearance does not match the selected equipment.");
		changeSkin(c,p,target,skin,committed);
		return "Applied " + skin.getName() + ".";
	}

	private static String restore(Connection c,Player p,Map<String,String> args,List<Runnable> committed) throws SQLException {
		Item target = target(p,args); changeSkin(c,p,target,target.getItemTemplate(),committed);
		return "Restored the original appearance.";
	}

	private static String applyChanges(Connection c,Player p,Map<String,String> args,List<Runnable> committed) throws SQLException {
		List<Map<String,Integer>> changes;
		try { changes = JSON.parseObject(args.getOrDefault("changes",""), new com.alibaba.fastjson2.TypeReference<List<Map<String,Integer>>>() {}); }
		catch (Exception e) { throw new IllegalArgumentException("Invalid appearance changes."); }
		if (changes == null || changes.isEmpty() || changes.size() > 16) throw new IllegalArgumentException("Select up to 16 equipment appearances.");
		Set<Integer> owned = unlocked(c,p.getAccount().getId()), seen = new HashSet<>();
		List<Item> equipment = equipment(p);
		Map<Item,ItemTemplate> validated = new LinkedHashMap<>();
		for (Map<String,Integer> change : changes) {
			if (change == null) throw new IllegalArgumentException("Invalid appearance changes.");
			Integer object = change.get("object"), skinId = change.get("skin"), expected = change.get("expected");
			if (object == null || skinId == null || expected == null || !seen.add(object)) throw new IllegalArgumentException("Invalid appearance changes.");
			Item item = equipment.stream().filter(i -> i.getObjectId() == object).findFirst()
				.orElseThrow(() -> new IllegalArgumentException("Equipment changed. Refresh Wardrobe."));
			if (item.getItemSkinTemplate().getTemplateId() != expected) throw new IllegalArgumentException("Equipment changed. Refresh Wardrobe.");
			ItemTemplate skin = skinId == 0 ? item.getItemTemplate() : byId.get(skinId);
			if (skin == null || skinId != 0 && (!owned.contains(skinId) || !WardrobeRules.permitted(skin,p.getRace(),p.getGender())
				|| !WardrobeRules.compatible(item.getItemTemplate(),skin))) throw new IllegalArgumentException("Unlock a compatible appearance before applying changes.");
			validated.put(item,skin);
		}
		// All selections are validated before any writes. The surrounding transaction commits the whole outfit together.
		for (var entry : validated.entrySet()) changeSkin(c,p,entry.getKey(),entry.getValue(),committed);
		return "Applied " + validated.size() + " appearance changes.";
	}

	private static void lockItem(Connection c,Player p,Item item) throws SQLException {
		Map<String,Object> saved = row(c,"SELECT item_count,item_id,item_skin,is_equipped FROM inventory WHERE item_unique_id=? AND item_owner=? AND item_location=0 FOR UPDATE",item.getObjectId(),p.getObjectId());
		if (saved == null || number(saved,"item_count") != item.getItemCount() || number(saved,"item_id") != item.getItemId()
			|| (number(saved,"is_equipped") != 0) != item.isEquipped()
			|| number(saved,"item_skin") != item.getItemSkinTemplate().getTemplateId()
				&& !(number(saved,"item_skin") == 0 && !item.isSkinnedItem()))
			throw new IllegalArgumentException("Equipment changed. Refresh Wardrobe.");
	}

	private static void changeSkin(Connection c,Player p,Item target,ItemTemplate skin,List<Runnable> committed) throws SQLException {
		lockItem(c,p,target);
		int skinId = skin.getTemplateId();
		commitAppearance(c,p.getObjectId(),target.getObjectId(),skinId);
		committed.add(() -> {
			target.setItemSkinTemplate(skin); target.setPersistentState(PersistentState.UPDATED);
			ItemPacketService.updateItemAfterInfoChange(p,target);
		});
	}

	static void commitAppearance(Connection c,int owner,int object,int skin) throws SQLException {
		try (PreparedStatement statement = c.prepareStatement("UPDATE inventory SET item_skin=? WHERE item_unique_id=? AND item_owner=? AND item_location=0")) {
			statement.setInt(1,skin); statement.setInt(2,object); statement.setInt(3,owner);
			if (statement.executeUpdate() != 1) throw new IllegalArgumentException("Equipment changed. Refresh Wardrobe.");
		}
	}

	private static String name(Map<String,String> args) {
		String name = args.getOrDefault("name","").strip();
		if (name.isBlank() || name.length() > 32 || name.codePoints().anyMatch(Character::isISOControl))
			throw new IllegalArgumentException("Enter an outfit name, up to 32 characters.");
		return name;
	}

	private static String saveOutfit(Connection c,Player p,Map<String,String> args) throws SQLException {
		String name = name(args); int account = p.getAccount().getId();
		if (row(c,"SELECT name FROM wardrobe_outfits WHERE account_id=? AND name=?",account,name) == null
			&& rows(c,"SELECT name FROM wardrobe_outfits WHERE account_id=?",account).size() >= 20)
			throw new IllegalArgumentException("You can save up to 20 outfits. Delete an outfit first.");
		Set<Integer> owned = unlocked(c,account);
		Map<String,Integer> skins = new LinkedHashMap<>();
		for (Item i : equipment(p)) if (i.isEquipped()) {
			int skin = i.isSkinnedItem() ? i.getItemSkinTemplate().getTemplateId() : 0;
			if (skin != 0 && !owned.contains(skin)) throw new IllegalArgumentException("Unlock " + i.getItemSkinTemplate().getName() + " before saving this outfit.");
			skins.put(Long.toString(i.getEquipmentSlot()),skin);
		}
		if (skins.isEmpty()) throw new IllegalArgumentException("Equip appearance-compatible equipment first.");
		update(c,"INSERT INTO wardrobe_outfits VALUES(?,?,?,?) ON DUPLICATE KEY UPDATE skins_json=VALUES(skins_json),updated_at=VALUES(updated_at)",account,name,JSON.toJSONString(skins),System.currentTimeMillis());
		return "Saved outfit " + name + ".";
	}

	private static String applyOutfit(Connection c,Player p,Map<String,String> args,List<Runnable> committed) throws SQLException {
		String name = name(args);
		Map<String,Object> saved = row(c,"SELECT skins_json FROM wardrobe_outfits WHERE account_id=? AND name=?",p.getAccount().getId(),name);
		if (saved == null) throw new IllegalArgumentException("This outfit was deleted. Refresh Wardrobe.");
		Map<String,Integer> skins = JSON.parseObject(saved.get("skins_json").toString(),new com.alibaba.fastjson2.TypeReference<Map<String,Integer>>() {});
		Set<Integer> owned = unlocked(c,p.getAccount().getId());
		for (var entry : skins.entrySet()) {
			long slot = Long.parseLong(entry.getKey());
			Item target = equipment(p).stream().filter(i -> i.isEquipped() && i.getEquipmentSlot() == slot).findFirst()
				.orElseThrow(() -> new IllegalArgumentException("Equip the equipment slots saved in this outfit first."));
			ItemTemplate skin = entry.getValue() == 0 ? target.getItemTemplate() : byId.get(entry.getValue());
			if (skin == null || entry.getValue() != 0 && (!owned.contains(entry.getValue())
				|| !WardrobeRules.permitted(skin,p.getRace(),p.getGender()) || !WardrobeRules.compatible(target.getItemTemplate(),skin)))
				throw new IllegalArgumentException("This outfit does not match your equipped items.");
			changeSkin(c,p,target,skin,committed);
		}
		return "Applied outfit " + name + ".";
	}

	private static String deleteOutfit(Connection c,int account,Map<String,String> args) throws SQLException {
		update(c,"DELETE FROM wardrobe_outfits WHERE account_id=? AND name=?",account,name(args));
		return "Outfit deleted.";
	}

	static int integer(Map<String,String> args,String key,int fallback) {
		try { return Integer.parseInt(args.getOrDefault(key,Integer.toString(fallback))); }
		catch (NumberFormatException e) { throw new IllegalArgumentException("Invalid selection."); }
	}
	static long number(Map<String,Object> row,String key) {
		Object value = row.get(key);
		// Connector/J returns TINYINT(1) inventory flags as Boolean.
		if (value instanceof Boolean flag) return flag ? 1 : 0;
		return value == null ? 0 : ((Number)value).longValue();
	}
	static int update(Connection c,String sql,Object... args) throws SQLException {
		try (PreparedStatement s = c.prepareStatement(sql)) { bind(s,args); return s.executeUpdate(); }
	}
	static List<Map<String,Object>> rows(Connection c,String sql,Object... args) throws SQLException {
		try (PreparedStatement s = c.prepareStatement(sql)) {
			bind(s,args);
			try (ResultSet r = s.executeQuery()) {
				List<Map<String,Object>> result = new ArrayList<>();
				while (r.next()) { Map<String,Object> row = new LinkedHashMap<>();
					for (int i=1;i<=r.getMetaData().getColumnCount();i++) row.put(r.getMetaData().getColumnLabel(i),r.getObject(i));
					result.add(row);
				}
				return result;
			}
		}
	}
	static Map<String,Object> row(Connection c,String sql,Object... args) throws SQLException {
		List<Map<String,Object>> rows = rows(c,sql,args); return rows.isEmpty() ? null : rows.getFirst();
	}
	private static void bind(PreparedStatement s,Object[] args) throws SQLException { for (int i=0;i<args.length;i++) s.setObject(i+1,args[i]); }
}
