package com.aionemu.gameserver.services;

import java.util.Locale;
import com.aionemu.gameserver.model.Gender;
import com.aionemu.gameserver.model.Race;
import com.aionemu.gameserver.model.templates.item.ItemTemplate;
import com.aionemu.gameserver.model.templates.item.enums.ItemSubType;
import com.aionemu.gameserver.model.templates.item.enums.ItemGroup;
import java.util.EnumSet;

/** Appearance rules shared by collection browsing and every apply operation. */
public final class WardrobeRules {

	public static final int UNLOCK_ITEM = 168100001;
	private static final EnumSet<ItemGroup> VISUAL = EnumSet.of(
		ItemGroup.SWORD, ItemGroup.GREATSWORD, ItemGroup.DAGGER, ItemGroup.MACE, ItemGroup.ORB,
		ItemGroup.SPELLBOOK, ItemGroup.POLEARM, ItemGroup.STAFF, ItemGroup.BOW, ItemGroup.HARP,
		ItemGroup.GUN, ItemGroup.CANNON, ItemGroup.KEYBLADE, ItemGroup.SHIELD, ItemGroup.CL_SHIELD,
		ItemGroup.HEAD, ItemGroup.LT_HEADS, ItemGroup.CL_HEADS, ItemGroup.WING, ItemGroup.CL_MULTISLOT,
		ItemGroup.TORSO, ItemGroup.GLOVE, ItemGroup.SHOULDER, ItemGroup.PANTS, ItemGroup.SHOES,
		ItemGroup.RB_TORSO, ItemGroup.RB_GLOVE, ItemGroup.RB_SHOULDER, ItemGroup.RB_PANTS, ItemGroup.RB_SHOES,
		ItemGroup.LT_TORSO, ItemGroup.LT_GLOVE, ItemGroup.LT_SHOULDER, ItemGroup.LT_PANTS, ItemGroup.LT_SHOES,
		ItemGroup.CH_TORSO, ItemGroup.CH_GLOVE, ItemGroup.CH_SHOULDER, ItemGroup.CH_PANTS, ItemGroup.CH_SHOES,
		ItemGroup.PL_TORSO, ItemGroup.PL_GLOVE, ItemGroup.PL_SHOULDER, ItemGroup.PL_PANTS, ItemGroup.PL_SHOES,
		ItemGroup.CL_TORSO, ItemGroup.CL_GLOVE, ItemGroup.CL_SHOULDER, ItemGroup.CL_PANTS, ItemGroup.CL_SHOES);
	private WardrobeRules() {}

	public static boolean eligible(ItemTemplate t) {
		if (t == null || !VISUAL.contains(t.getItemGroup())
			|| t.getExpireTime() != 0 || t.getName().isBlank()) return false;
		String n = t.getName().toLowerCase(Locale.ROOT);
		if (n.matches(".*(test|debug|dummy|prototype|placeholder|npc).*" ) || n.matches(".*\\bprop\\b.*")) return false;
		var action = t.getActions() == null ? null : t.getActions().getRemodelAction();
		return action == null || action.getExpireMinutes() == 0;
	}

	public static boolean permitted(ItemTemplate skin, Race race, Gender gender) {
		return (skin.getRace() == null || skin.getRace() == Race.PC_ALL || skin.getRace() == race)
			&& (skin.getUseLimits() == null || skin.getUseLimits().getGenderPermitted() == null
				|| skin.getUseLimits().getGenderPermitted() == gender);
	}

	public static boolean compatible(ItemTemplate target, ItemTemplate skin) {
		if (target == null || skin == null) return false;
		var keep = target.getItemGroup(); var extract = skin.getItemGroup();
		if (!VISUAL.contains(keep) || !VISUAL.contains(extract)) return false;
		if (target.isWeapon() || skin.isWeapon())
			return target.isWeapon() && skin.isWeapon() && keep == extract;
		// Armor material does not limit appearances. Native models still require
		// the same body slot; shields must not use a weapon's off-hand slot.
		return (keep.getValidEquipmentSlots() & extract.getValidEquipmentSlots()) != 0;
	}

	public static String category(ItemTemplate t) {
		String g = t.getItemGroup().name();
		if (t.isWeapon()) return "Weapons";
		if (g.contains("HEAD")) return "Headwear";
		if (g.equals("WING")) return "Wings";
		if (g.contains("SHIELD")) return "Shields";
		if (t.getItemGroup().getItemSubType() == ItemSubType.CLOTHES
			|| t.getItemGroup().getItemSubType() == ItemSubType.ALL_ARMOR) return "Costumes";
		return "Armor";
	}

	public static String typeName(ItemTemplate t) {
		String g = t.getItemGroup().name();
		String material = g.startsWith("RB_") ? "Cloth " : g.startsWith("LT_") ? "Leather "
			: g.startsWith("CH_") ? "Chain " : g.startsWith("PL_") ? "Plate " : "";
		if (g.startsWith("CL_")) return category(t);
		String part = g.contains("_") ? g.substring(g.indexOf('_') + 1) : g;
		String name = switch (part) {
			case "TORSO" -> "Chest Armor";
			case "GLOVE" -> "Gloves";
			case "SHOULDER" -> "Shoulder Armor";
			case "PANTS" -> "Leg Armor";
			case "SHOES" -> "Boots";
			case "HEAD", "HEADS" -> "Headwear";
			case "WING" -> "Wings";
			case "GUN" -> "Pistol";
			case "CANNON" -> "Aethercannon";
			case "KEYBLADE" -> "Cipher-Blade";
			default -> part.substring(0,1) + part.substring(1).toLowerCase(Locale.ROOT);
		};
		return material + name;
	}
}
