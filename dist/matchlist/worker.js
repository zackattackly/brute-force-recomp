// Brute Force PC recomp - the online match list (a Cloudflare Worker with a D1 database bound as DB).
//
// It only lists matches; the game traffic goes straight between the players and the host. Plain-text replies so the loader's
// small HTTP client can read them.
//   GET /host?room=R&port=P   a hosting game says "I host in room R on UDP port P" (every 30 s); its address is the caller's
//   GET /stop?port=P          the host stopped
//   GET /list?room=R          the matches of room R, one "address port" per line (hosts heard from in the last 90 s)
// Room 0 is the public room; a private room is a number made from the password (the password itself is never sent).
//   GET /relay?room=R (WebSocket)   Milestone 100: the System Link relay. Every player's game connects out to the room R relay
//   (a Durable Object), so no router needs a port forward. Messages are the Ethernet frames of the emulated Xbox network chip;
//   the room works like a switch: broadcasts (a search for matches) go to everyone else in the room, frames to one console go
//   to the connection that console was last seen on (everyone else if unknown). Frames shorter than 14 bytes are keep-alives.

import { DurableObject } from "cloudflare:workers";

const MAX_ROOM = 64;
// the rooms' internal names: changing the prefix gives every room a fresh object running the current code (the old public
// room kept running its first code after a deploy and ignored the round-trip test)
const ROOM_PREFIX = "r2-";
const hex6 = (b, o) => { let s = ""; for (let i = 0; i < 6; i++) s += (b[o + i] | 0x100).toString(16).slice(1); return s; };

export class Relay extends DurableObject {
  async fetch(request) {
    if (new URL(request.url).pathname === "/stats") {   // who is in this room: console address, messages and frames sent, seconds since the last one
      const now = Date.now(), list = this.ctx.getWebSockets().map(w => { const a = w.deserializeAttachment() || {};
        return { mac: a.mac || "", msgs: a.n || 0, frames: a.f || 0, connected_s: Math.round((now - (a.t0 || now)) / 1000), last_msg_s: a.tl ? Math.round((now - a.tl) / 1000) : null, last_len: a.len || 0, last_frame: a.last || "" }; });
      return new Response(JSON.stringify(list, null, 1) + "\n", { headers: { "content-type": "application/json", "cache-control": "no-store" } });
    }
    if (this.ctx.getWebSockets().length >= MAX_ROOM) return new Response("room full\n", { status: 503 });
    const pair = new WebSocketPair();
    this.ctx.acceptWebSocket(pair[1]);
    pair[1].serializeAttachment({ mac: "", n: 0, f: 0, t0: Date.now(), tl: 0 });
    return new Response(null, { status: 101, webSocket: pair[0] });
  }
  async webSocketMessage(ws, msg) {
    if (typeof msg === "string") return;
    const b = new Uint8Array(msg), att = ws.deserializeAttachment() || {};
    att.n = (att.n || 0) + 1; att.tl = Date.now(); if (b.length >= 14) att.f = (att.f || 0) + 1;
    if (b.length >= 14) { att.mac = hex6(b, 6); att.last = Array.from(b.subarray(0, Math.min(48, b.length)), x => (x | 0x100).toString(16).slice(1)).join(" "); att.len = b.length; }   // learn which console is on this connection; the latest frame's start (for /stats)
    ws.serializeAttachment(att);
    if (b.length === 4 && b[0] === 0x50 && b[1] === 0x49 && b[2] === 0x4E && b[3] === 0x47) {   // "PING": the helper's round-trip test
      try { ws.send(new Uint8Array([0x50, 0x4F, 0x4E, 0x47])); } catch (e) {}                  // "PONG"
      return;
    }
    if (b.length < 14) return;                                   // keep-alive
    const all = this.ctx.getWebSockets();
    let to = null;
    if (!(b[0] & 1)) { const dst = hex6(b, 0); to = all.find(w => w !== ws && (w.deserializeAttachment() || {}).mac === dst) || null; }
    const send = w => { try { w.send(msg); } catch (e) {} };
    if (to) send(to); else for (const w of all) if (w !== ws) send(w);
  }
  async webSocketClose(ws, code) { try { ws.close(code, "bye"); } catch (e) {} }
  async webSocketError(ws) { try { ws.close(1011, "error"); } catch (e) {} }
}

const TTL = 90;          // seconds a host stays listed after its last report
const MAX_LIST = 64;

async function ready(db) {
  await db.prepare("CREATE TABLE IF NOT EXISTS hosts (ip TEXT NOT NULL, port INTEGER NOT NULL, room INTEGER NOT NULL, seen INTEGER NOT NULL, PRIMARY KEY (ip, port))").run();
}

function text(body, status = 200) {
  return new Response(body, { status, headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" } });
}

function num(v, lo, hi) {
  if (v === null || !/^\d{1,10}$/.test(v)) return null;
  const n = Number(v);
  return n >= lo && n <= hi ? n : null;
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const ip = request.headers.get("CF-Connecting-IP") || "";
    const now = Math.floor(Date.now() / 1000);
    if (!env.DB) return text("the D1 database is not bound as DB\n", 500);
    await ready(env.DB);

    // the game connects over IPv4: a host may name its internet IPv4 address (from its router) as ip=; otherwise the caller's
    // address is used if it is IPv4 (a request that came over IPv6 cannot be listed without ip=)
    const v4 = s => s && /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.test(s) && s.split(".").every(x => Number(x) <= 255);
    const given = url.searchParams.get("ip");
    const hostip = v4(given) ? given : (v4(ip) ? ip : null);
    if (url.pathname === "/host") {
      const room = num(url.searchParams.get("room"), 0, 4294967295), port = num(url.searchParams.get("port"), 1, 65535);
      if (room === null || port === null) return text("bad request\n", 400);
      if (!hostip) return text("need ipv4\n", 400);
      await env.DB.prepare("INSERT INTO hosts (ip, port, room, seen) VALUES (?1, ?2, ?3, ?4) ON CONFLICT (ip, port) DO UPDATE SET room = ?3, seen = ?4")
        .bind(hostip, port, room, now).run();
      return text("ok " + hostip + "\n");
    }
    if (url.pathname === "/stop") {
      const port = num(url.searchParams.get("port"), 1, 65535);
      if (port === null || !hostip) return text("bad request\n", 400);
      await env.DB.prepare("DELETE FROM hosts WHERE ip = ?1 AND port = ?2").bind(hostip, port).run();
      return text("ok\n");
    }
    if (url.pathname === "/relay") {   // Milestone 100: the game traffic itself, over a WebSocket (no port forwarding anywhere)
      const room = num(url.searchParams.get("room"), 0, 4294967295);
      if (room === null) return text("bad request\n", 400);
      if (request.headers.get("Upgrade") !== "websocket") return text("websocket expected\n", 426);
      return env.RELAY.get(env.RELAY.idFromName(ROOM_PREFIX + room)).fetch(request);
    }
    if (url.pathname === "/stats") {   // the live state of a relay room (for testing): GET /stats?room=0
      const room = num(url.searchParams.get("room"), 0, 4294967295);
      if (room === null) return text("bad request\n", 400);
      return env.RELAY.get(env.RELAY.idFromName(ROOM_PREFIX + room)).fetch(new Request("https://relay/stats"));
    }
    if (url.pathname === "/list") {
      const room = num(url.searchParams.get("room"), 0, 4294967295);
      if (room === null) return text("bad request\n", 400);
      await env.DB.prepare("DELETE FROM hosts WHERE seen < ?1").bind(now - TTL).run();
      const rows = await env.DB.prepare("SELECT ip, port FROM hosts WHERE room = ?1 ORDER BY seen DESC LIMIT ?2").bind(room, MAX_LIST).all();
      return text((rows.results || []).map(r => r.ip + " " + r.port).join("\n") + "\n");
    }
    return text("Brute Force match list\n");
  }
};
