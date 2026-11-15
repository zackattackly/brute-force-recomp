// Brute Force PC recomp - the online match list (a Cloudflare Worker with a D1 database bound as DB).
//
// It only lists matches; the game traffic goes straight between the players and the host. Plain-text replies so the loader's
// small HTTP client can read them.
//   GET /host?room=R&port=P   a hosting game says "I host in room R on UDP port P" (every 30 s); its address is the caller's
//   GET /stop?port=P          the host stopped
//   GET /list?room=R          the matches of room R, one "address port" per line (hosts heard from in the last 90 s)
// Room 0 is the public room; a private room is a number made from the password (the password itself is never sent).

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
