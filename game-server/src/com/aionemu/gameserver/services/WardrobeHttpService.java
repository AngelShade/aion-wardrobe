package com.aionemu.gameserver.services;

import java.io.IOException;
import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.SecureRandom;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import com.alibaba.fastjson2.JSON;
import com.aionemu.gameserver.model.gameobjects.player.Player;
import com.sun.net.httpserver.HttpExchange;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public final class WardrobeHttpService {

	private static final Logger log = LoggerFactory.getLogger(WardrobeHttpService.class);
	private static final Path MEDIA = Path.of("config/wardrobe/media");
	private static final SecureRandom RANDOM = new SecureRandom();
	private static final Map<String,Form> FORMS = new ConcurrentHashMap<>();
	private record Form(Player player,Object connection,long expires) {}
	private WardrobeHttpService() {}

	public static void handle(HttpExchange x) throws IOException {
		try {
			x.getResponseHeaders().set("Cache-Control","no-store");
			x.getResponseHeaders().set("X-Content-Type-Options","nosniff");
			x.getResponseHeaders().set("Referrer-Policy","no-referrer");
			String path = x.getRequestURI().getPath(), method = x.getRequestMethod();
			if (!method.equals("GET") && !method.equals("POST")) { send(x,405,"application/json","{\"error\":\"Method not allowed.\"}"); return; }
			if (method.equals("GET") && path.equals("/market/wardrobe")) {
				send(x,200,"text/html",Files.readString(MEDIA.resolve("wardrobe.html"))); return;
			}
			if (method.equals("GET") && path.matches("/market/wardrobe/media/wardrobe\\.(css|js)")) {
				String file = path.substring(path.lastIndexOf('/')+1);
				send(x,200,file.endsWith(".js") ? "application/javascript" : "text/css",Files.readString(MEDIA.resolve(file))); return;
			}
			boolean action = path.equals("/market/wardrobe/action");
			if (!action && !path.equals("/market/wardrobe/state")) { send(x,404,"application/json","{\"error\":\"Not found.\"}"); return; }
			if (!method.equals(action ? "POST" : "GET")) { send(x,405,"application/json","{\"error\":\"Method not allowed.\"}"); return; }
			Map<String,String> args = parse(action ? new String(x.getRequestBody().readNBytes(8193),StandardCharsets.UTF_8) : x.getRequestURI().getRawQuery());
			Player p = CentralMarketHttpService.findPlayer(args.getOrDefault("session_id",""));
			if (p == null || !p.isOnline()) { send(x,403,"application/json","{\"error\":\"Log in, then reopen Wardrobe.\"}"); return; }
			String notice = "";
			if (action) {
				String origin = x.getRequestHeaders().getFirst("Origin"), expected = "http://"+x.getRequestHeaders().getFirst("Host");
				if (origin != null && !origin.equals(expected)) throw new IllegalArgumentException("Reopen Wardrobe to continue.");
				String request = args.getOrDefault("request",""); Form form = FORMS.get(request);
				if (form == null || form.player() != p || form.connection() != p.getClientConnection() || form.expires() < System.currentTimeMillis()) {
					send(x,403,"application/json","{\"error\":\"This form expired. Refresh Wardrobe.\"}"); return;
				}
				notice = WardrobeService.action(p,args,request);
			}
			long started = System.nanoTime();
			Map<String,Object> state = WardrobeService.snapshot(p,args);
			x.getResponseHeaders().set("Server-Timing","wardrobe;dur="+(System.nanoTime()-started)/1_000_000);
			long now = System.currentTimeMillis(); FORMS.entrySet().removeIf(e -> e.getValue().expires() < now);
			if (FORMS.size() >= 5000) throw new IllegalStateException("Wardrobe is busy. Try again.");
			byte[] bytes = new byte[24]; RANDOM.nextBytes(bytes);
			String request = Base64.getUrlEncoder().withoutPadding().encodeToString(bytes);
			FORMS.put(request,new Form(p,p.getClientConnection(),now+600000));
			state.put("request",request); state.put("notice",notice);
			send(x,200,"application/json",JSON.toJSONString(state));
		} catch (IllegalArgumentException e) { send(x,400,"application/json",JSON.toJSONString(Map.of("error",e.getMessage()))); }
		catch (Exception e) { log.error("Wardrobe request failed",e); send(x,503,"application/json","{\"error\":\"Wardrobe is unavailable. Refresh before trying again.\"}"); }
		finally { x.close(); }
	}

	private static Map<String,String> parse(String text) {
		Map<String,String> args = new HashMap<>();
		if (text == null) return args;
		if (text.length() > 8192) throw new IllegalArgumentException("Request is too large.");
		for (String part : text.split("&")) { String[] kv = part.split("=",2);
			if (kv.length == 2) args.put(URLDecoder.decode(kv[0],StandardCharsets.UTF_8),URLDecoder.decode(kv[1],StandardCharsets.UTF_8));
		}
		return args;
	}
	private static void send(HttpExchange x,int status,String type,String body) throws IOException {
		byte[] bytes = body.getBytes(StandardCharsets.UTF_8); x.getResponseHeaders().set("Content-Type",type+"; charset=utf-8");
		x.sendResponseHeaders(status,bytes.length); x.getResponseBody().write(bytes);
	}
}
