package com.aionemu.gameserver.services;

import java.nio.file.*;
import java.sql.*;
import java.util.*;
import com.aionemu.gameserver.model.templates.item.ItemTemplate;
import com.aionemu.gameserver.model.templates.item.enums.ItemGroup;
import com.aionemu.gameserver.model.Gender;
import com.aionemu.gameserver.model.Race;

/** Uses a new empty schema, copying the inventory structure only. */
public final class WardrobeDatabaseCheck {

	private static int checks;
	private static void check(boolean pass,String label) { checks++; if (!pass) throw new AssertionError(label); }
	private static long value(Connection c,String sql,Object... args) throws SQLException { return WardrobeService.number(WardrobeService.row(c,sql,args),"n"); }
	private static void ticket(Connection c,int id,int owner,long count) throws SQLException {
		WardrobeService.update(c,"INSERT INTO inventory(item_unique_id,item_id,item_count,item_owner,item_location,is_equipped) VALUES(?,?,?, ?,0,0)",id,WardrobeRules.UNLOCK_ITEM,count,owner);
	}
	private static void refused(Connection c,int account,int skin,int character,int ticket,long count) throws Exception {
		boolean refused = false;
		try { WardrobeService.commitUnlock(c,account,skin,character,ticket,count); }
		catch (IllegalArgumentException e) { refused = true; c.rollback(); }
		check(refused,"invalid unlock rejected");
	}
	private static ItemTemplate template(ItemGroup group,String name) throws Exception {
		var t = new ItemTemplate();
		for (var pair : Map.<String,Object>of("itemGroup",group,"name",name,"mask",4096).entrySet()) {
			var field = ItemTemplate.class.getDeclaredField(pair.getKey()); field.setAccessible(true); field.set(t,pair.getValue());
		}
		return t;
	}
	public static void main(String[] args) throws Exception {
		Properties p = new Properties(); Path deployment = Path.of(args[0]);
		try (var in = Files.newInputStream(deployment.resolve("config/network/database.properties"))) { p.load(in); }
		if (Files.exists(deployment.resolve("config/mygs.properties")))
			try (var in = Files.newInputStream(deployment.resolve("config/mygs.properties"))) { p.load(in); }
		String url = p.getProperty("database.url").replace("${gameserver.timezone}","UTC");
		String user = p.getProperty("database.user"), password = p.getProperty("database.password");
		String schema = "aion_wardrobe_check_"+System.currentTimeMillis();
		check(schema.matches("aion_wardrobe_check_[0-9]+"),"isolated schema name");
		try (Connection live = DriverManager.getConnection(url,user,password); Statement s = live.createStatement()) {
			String source = live.getCatalog(); check(source.matches("[A-Za-z0-9_]+") && !source.equals(schema),"read-only source schema");
			s.execute("CREATE DATABASE `"+schema+"`");
			try {
				s.execute("CREATE TABLE `"+schema+"`.inventory LIKE `"+source+"`.inventory");
				try (Connection c = DriverManager.getConnection(url.replace("/"+source,"/"+schema),user,password); Statement ddl = c.createStatement()) {
					check(schema.equals(c.getCatalog()),"test connection isolated");
					for (String sql : Files.readString(Path.of("game-server/config/wardrobe/schema.sql")).split(";")) if (!sql.isBlank()) ddl.execute(sql);
					check(value(c,"SELECT COUNT(*) n FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME LIKE 'wardrobe_%' AND ENGINE='InnoDB'")==4,"four transactional wardrobe tables");
					c.setAutoCommit(false);
					ticket(c,1001,101,2); ticket(c,1002,101,1); c.commit();
					var inventoryRow = WardrobeService.row(c,"SELECT is_equipped,item_count,item_id,item_skin FROM inventory WHERE item_unique_id=1001 FOR UPDATE");
					check(WardrobeService.number(inventoryRow,"is_equipped")==0,"inventory driver flag decodes unequipped");
					WardrobeService.update(c,"UPDATE inventory SET is_equipped=1 WHERE item_unique_id=1001");
					check(WardrobeService.number(WardrobeService.row(c,"SELECT is_equipped FROM inventory WHERE item_unique_id=1001"),"is_equipped")==1,"inventory driver flag decodes equipped");
					c.rollback();
					check(WardrobeService.number(Map.of("flag",Boolean.TRUE),"flag")==1 && WardrobeService.number(Map.of("flag",Boolean.FALSE),"flag")==0,"boolean equipment flags accepted");
					WardrobeService.commitUnlock(c,11,100000096,101,1001,2); c.commit();
					check(value(c,"SELECT item_count n FROM inventory WHERE item_unique_id=1001")==1,"exactly one ticket consumed");
					check(WardrobeService.unlocked(c,11).contains(100000096),"account skin unlocked");
					check(WardrobeService.unlocked(c,12).isEmpty(),"other accounts do not inherit unlocks");
					refused(c,11,100000096,101,1001,1);
					check(value(c,"SELECT item_count n FROM inventory WHERE item_unique_id=1001")==1,"duplicate unlock does not consume a ticket");
					WardrobeService.commitUnlock(c,11,100000097,101,1002,1); c.rollback();
					check(value(c,"SELECT item_count n FROM inventory WHERE item_unique_id=1002")==1 && !WardrobeService.unlocked(c,11).contains(100000097),"rollback restores ticket and collection together");
					refused(c,12,100000098,102,1002,1);
					check(!WardrobeService.unlocked(c,12).contains(100000098),"foreign ticket cannot unlock a skin");
					refused(c,11,100000098,101,1001,5);
					check(!WardrobeService.unlocked(c,11).contains(100000098),"stale ticket count cannot unlock a skin");
					WardrobeService.commitUnlock(c,11,100000099,101,1002,1); c.commit();
					check(value(c,"SELECT COUNT(*) n FROM inventory WHERE item_unique_id=1002")==0,"last ticket removes its row");
					WardrobeService.update(c,"INSERT INTO wardrobe_outfits VALUES(11,'Outfit','{\"8\":100000096}',1)"); c.commit();
					check(value(c,"SELECT COUNT(*) n FROM wardrobe_outfits WHERE account_id=12")==0,"outfits are account private");
					WardrobeService.update(c,"UPDATE wardrobe_outfits SET skins_json='{}' WHERE account_id=11"); c.rollback();
					check(WardrobeService.row(c,"SELECT skins_json FROM wardrobe_outfits WHERE account_id=11").get("skins_json").toString().contains("100000096"),"outfit update rolls back");
					c.commit();
					ticket(c,1101,101,1); ticket(c,1102,101,1); c.commit();
					WardrobeService.commitAppearance(c,101,1101,100000096);
					WardrobeService.commitAppearance(c,101,1102,100000097); c.commit();
					check(value(c,"SELECT item_skin n FROM inventory WHERE item_unique_id=1101")==100000096
						&& value(c,"SELECT item_skin n FROM inventory WHERE item_unique_id=1102")==100000097,"appearance batch persists every item");
					boolean invalidBatch=false;
					try { WardrobeService.commitAppearance(c,101,1101,100000099); WardrobeService.commitAppearance(c,102,1102,100000099); }
					catch (IllegalArgumentException e) { invalidBatch=true; c.rollback(); }
					check(invalidBatch && value(c,"SELECT item_skin n FROM inventory WHERE item_unique_id=1101")==100000096,"foreign item rolls back whole appearance batch");
					WardrobeService.commitAppearance(c,101,1101,168100001); c.rollback();
					check(value(c,"SELECT item_skin n FROM inventory WHERE item_unique_id=1101")==100000096,"uncommitted appearance leaves equipment unchanged");
				}
			} finally { s.execute("DROP DATABASE `"+schema+"`"); }
		}
		ItemTemplate sword = template(ItemGroup.SWORD,"Sword"), mace = template(ItemGroup.MACE,"Mace");
		ItemTemplate robe = template(ItemGroup.RB_TORSO,"Robe"), plate = template(ItemGroup.PL_TORSO,"Plate"), costume = template(ItemGroup.CL_TORSO,"Costume");
		check(WardrobeRules.compatible(sword,sword),"same weapon type allowed");
		check(!WardrobeRules.compatible(sword,mace),"different weapon type refused");
		check(WardrobeRules.compatible(plate,robe) && WardrobeRules.compatible(robe,plate),"armor material does not restrict skins");
		check(WardrobeRules.compatible(costume,plate),"appearance equipment can receive armor skins");
		check(!WardrobeRules.compatible(plate,template(ItemGroup.PL_SHOES,"Boots")),"armor body slots remain distinct");
		check(WardrobeRules.compatible(template(ItemGroup.HEAD,"Headwear"),template(ItemGroup.CL_HEADS,"Appearance Headwear")),"headwear types share their visual slot");
		check(WardrobeRules.compatible(template(ItemGroup.CL_SHIELD,"Appearance Shield"),template(ItemGroup.SHIELD,"Shield")),"shield appearance types work both ways");
		check(!WardrobeRules.compatible(template(ItemGroup.SHIELD,"Shield"),sword),"shield cannot receive a weapon skin");
		check(WardrobeRules.compatible(plate,template(ItemGroup.CL_MULTISLOT,"Costume")),"multi-slot costume works on chest armor");
		check(WardrobeRules.category(template(ItemGroup.TORSO,"Appearance Chest")).equals("Costumes"),"generic appearance armor is a costume");
		check(WardrobeRules.compatible(plate,costume),"costume accepted on matching armor slot");
		check(!WardrobeRules.compatible(sword,costume),"costume cannot become a weapon skin");
		check(WardrobeRules.eligible(sword) && !WardrobeRules.eligible(template(ItemGroup.SWORD,"Test Sword")),"test appearances excluded");
		for (ItemGroup group : ItemGroup.values()) if (group.name().contains("STIGMA") || group.name().contains("SHARD")
			|| Set.of("PLUME","RING","EARRING","NECKLACE","BELT").contains(group.name()))
			check(!WardrobeRules.eligible(template(group,"Equipment")),"nonvisual equipment excluded: "+group);
		check(WardrobeRules.permitted(sword,Race.ELYOS,Gender.MALE),"unrestricted skin permitted");
		var race = ItemTemplate.class.getDeclaredField("race"); race.setAccessible(true); race.set(sword,Race.ASMODIANS);
		check(!WardrobeRules.permitted(sword,Race.ELYOS,Gender.MALE),"opposite faction skin refused");
		System.out.println("PASS: "+checks+" Wardrobe database and appearance checks. No live character data changed.");
	}
}
