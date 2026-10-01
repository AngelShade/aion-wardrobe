package com.aionemu.gameserver.services;

import java.nio.file.*;
import java.nio.*;
import java.util.*;
import javax.xml.stream.*;
import com.aionemu.gameserver.model.templates.item.ItemTemplate;
import com.aionemu.gameserver.model.templates.item.enums.ItemGroup;
import com.aionemu.gameserver.model.templates.item.actions.*;

/** Checks the complete server catalog against the matching client item index. */
public final class WardrobeCatalogCheck {
    private static void set(Object object,String name,Object value) throws Exception {
        var f=object.getClass().getDeclaredField(name);f.setAccessible(true);f.set(object,value);
    }
    private static String attr(XMLStreamReader r,String name,String fallback) {
        String value=r.getAttributeValue(null,name);return value==null?fallback:value;
    }
    public static void main(String[] args) throws Exception {
        var bytes=ByteBuffer.wrap(Files.readAllBytes(Path.of(args[1]))).order(ByteOrder.LITTLE_ENDIAN);
        int count=bytes.getInt(8); Set<Integer> client=new HashSet<>();
        for(int i=0;i<count;i++) client.add(bytes.getInt(56+i*8));
        Map<String,Integer> categories=new TreeMap<>(), restoredGroups=new TreeMap<>();
        int restored=0, total=0;
        try(var in=Files.newInputStream(Path.of(args[0]))) {
            var r=XMLInputFactory.newFactory().createXMLStreamReader(in);
            ItemTemplate t=null; int mask=0, type=-1, minutes=0;
            while(r.hasNext()) {
                int event=r.next();
                if(event==XMLStreamConstants.START_ELEMENT && r.getLocalName().equals("item_template")) {
                    t=new ItemTemplate();type=-1;minutes=0;mask=Integer.parseInt(attr(r,"mask","0"));
                    set(t,"itemId",Integer.parseInt(attr(r,"id","0")));
                    set(t,"name",attr(r,"name",""));set(t,"mask",mask);
                    set(t,"itemGroup",ItemGroup.valueOf(attr(r,"item_group","NONE")));
                    set(t,"expireTime",Integer.parseInt(attr(r,"expire_time","0")));
                } else if(t!=null && event==XMLStreamConstants.START_ELEMENT && r.getLocalName().equals("remodel")) {
                    type=Integer.parseInt(attr(r,"type","0"));minutes=Integer.parseInt(attr(r,"minutes","0"));
                    var action=new RemodelAction();set(action,"extractType",type);set(action,"expireMinutes",minutes);
                    var actions=new ItemActions();set(actions,"itemActions",List.of(action));set(t,"actions",actions);
                } else if(t!=null && event==XMLStreamConstants.END_ELEMENT && r.getLocalName().equals("item_template")) {
                    if(client.contains(t.getTemplateId()) && WardrobeRules.eligible(t)) {
                        total++;categories.merge(WardrobeRules.category(t),1,Integer::sum);
                        if(type==2 || (mask&4096)==0) {restored++;restoredGroups.merge(t.getItemGroup().name(),1,Integer::sum);}
                    }
                    t=null;
                }
            }
            r.close();
        }
        if(total<39000 || restored<500 || categories.getOrDefault("Costumes",0)==0
            || restoredGroups.getOrDefault("HEAD",0)==0 || restoredGroups.getOrDefault("WING",0)==0)
            throw new AssertionError("Incomplete appearance coverage: "+categories+" restored="+restoredGroups);
        for(ItemGroup a:ItemGroup.values()) for(ItemGroup b:ItemGroup.values()) {
            var target=new ItemTemplate();var skin=new ItemTemplate();
            set(target,"itemGroup",a);set(skin,"itemGroup",b);
            if(target.isWeapon() || skin.isWeapon()) {
                boolean expected=target.isWeapon() && skin.isWeapon() && a==b && a!=ItemGroup.NOWEAPON
                    && !a.name().startsWith("TOOL") && a!=ItemGroup.NPC_MACE;
                if(WardrobeRules.compatible(target,skin)!=expected) throw new AssertionError("Weapon boundary: "+a+" / "+b);
            }
        }
        System.out.println("PASS: "+total+" client-backed appearances; "+restored+" restored across "+restoredGroups.size()+" item groups.");
        System.out.println("Categories: "+categories);
        System.out.println("Restored groups: "+restoredGroups);
        System.out.println("PASS: every weapon group pair checked, including shields and nonvisual equipment.");
    }
}
