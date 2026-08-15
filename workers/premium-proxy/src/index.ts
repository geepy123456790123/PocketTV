interface Env {
  SERVICE_NAME: string;
  PREMIUM_SHARED_TOKEN: string;
  UPSTREAM_M3U_URL: string;
  UPSTREAM_EPG_URL: string;
}

const TEXT_HEADERS = {
  "Access-Control-Allow-Origin": "*",
  "Cache-Control": "private, max-age=60",
  "X-Content-Type-Options": "nosniff"
};

class HttpError extends Error {
  constructor(public status: number, message: string) {
    super(message);
  }
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    try {
      if (request.method === "OPTIONS") return new Response(null, { headers: TEXT_HEADERS });
      if (request.method !== "GET") throw new HttpError(405, "Method not allowed");

      const url = new URL(request.url);
      if (url.pathname === "/" || url.pathname === "/health") {
        return json({ ok: true, service: env.SERVICE_NAME });
      }

      requirePremiumToken(request, url, env);

      if (url.pathname === "/premium/m3u") return proxyM3u(request, env);
      if (url.pathname === "/premium/epg") return proxyEpg(request, env);
      if (url.pathname === "/premium/stream") return proxyBinary(request, url);
      if (url.pathname === "/premium/asset") return proxyBinary(request, url);

      throw new HttpError(404, "Not found");
    } catch (error) {
      if (error instanceof HttpError) return json({ error: error.message }, error.status);
      console.error(error);
      return json({ error: "Internal server error" }, 500);
    }
  }
};

async function proxyM3u(request: Request, env: Env): Promise<Response> {
  const upstream = await fetch(env.UPSTREAM_M3U_URL, {
    headers: upstreamHeaders(request)
  });
  if (!upstream.ok) throw new HttpError(502, `Upstream M3U failed: ${upstream.status}`);

  const publicUrl = new URL(request.url);
  const playlist = rewriteM3u(await upstream.text(), publicUrl);
  return new Response(playlist, {
    headers: {
      ...TEXT_HEADERS,
      "Content-Type": "audio/x-mpegurl; charset=utf-8"
    }
  });
}

async function proxyEpg(request: Request, env: Env): Promise<Response> {
  const upstream = await fetch(env.UPSTREAM_EPG_URL, {
    headers: upstreamHeaders(request)
  });
  if (!upstream.ok) throw new HttpError(502, `Upstream EPG failed: ${upstream.status}`);

  const publicUrl = new URL(request.url);
  const epg = rewriteXmltvIcons(await upstream.text(), publicUrl);
  return new Response(epg, {
    headers: {
      ...TEXT_HEADERS,
      "Cache-Control": "private, max-age=300",
      "Content-Type": "application/xml; charset=utf-8"
    }
  });
}

async function proxyBinary(request: Request, url: URL): Promise<Response> {
  const target = unwrapUrl(url.searchParams.get("u"));
  if (!target) throw new HttpError(400, "Missing target");

  const upstream = await fetch(target, {
    headers: upstreamHeaders(request),
    redirect: "follow"
  });
  if (!upstream.ok) throw new HttpError(502, `Upstream asset failed: ${upstream.status}`);

  const headers = new Headers(upstream.headers);
  headers.set("Access-Control-Allow-Origin", "*");
  headers.set("Cache-Control", "private, max-age=60");
  headers.set("X-Content-Type-Options", "nosniff");
  headers.delete("Set-Cookie");
  return new Response(upstream.body, { status: upstream.status, headers });
}

function rewriteM3u(source: string, publicUrl: URL): string {
  return source
    .replace(/\btvg-logo="([^"]+)"/g, (_match, logo: string) => `tvg-logo="${proxyUrl(publicUrl, "/premium/asset", logo)}"`)
    .split(/\r?\n/)
    .map((line) => {
      const trimmed = line.trim();
      if (!trimmed || trimmed.startsWith("#")) return line;
      if (!/^https?:\/\//i.test(trimmed)) return line;
      return proxyUrl(publicUrl, "/premium/stream", trimmed);
    })
    .join("\n");
}

function rewriteXmltvIcons(source: string, publicUrl: URL): string {
  return source.replace(/\bsrc="(https?:\/\/[^"]+)"/g, (_match, icon: string) => `src="${proxyUrl(publicUrl, "/premium/asset", icon)}"`);
}

function proxyUrl(publicUrl: URL, pathname: string, target: string): string {
  const next = new URL(publicUrl.origin);
  next.pathname = pathname;
  next.searchParams.set("u", wrapUrl(target));
  const token = publicUrl.searchParams.get("token");
  if (token) next.searchParams.set("token", token);
  return next.toString();
}

function requirePremiumToken(request: Request, url: URL, env: Env): void {
  const configured = env.PREMIUM_SHARED_TOKEN;
  if (!configured) throw new HttpError(500, "Premium token is not configured");

  const auth = request.headers.get("Authorization") ?? "";
  const bearer = auth.startsWith("Bearer ") ? auth.slice("Bearer ".length) : "";
  const queryToken = url.searchParams.get("token") ?? "";
  if (bearer !== configured && queryToken !== configured) throw new HttpError(401, "Unauthorized");
}

function upstreamHeaders(request: Request): Headers {
  const headers = new Headers();
  const range = request.headers.get("Range");
  if (range) headers.set("Range", range);
  headers.set("User-Agent", "PocketTV-Premium-Proxy/0.1");
  return headers;
}

function wrapUrl(value: string): string {
  return btoa(value).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
}

function unwrapUrl(value: string | null): string | null {
  if (!value) return null;
  const padded = value.replace(/-/g, "+").replace(/_/g, "/").padEnd(Math.ceil(value.length / 4) * 4, "=");
  try {
    const decoded = atob(padded);
    return /^https?:\/\//i.test(decoded) ? decoded : null;
  } catch {
    return null;
  }
}

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      ...TEXT_HEADERS,
      "Content-Type": "application/json; charset=utf-8"
    }
  });
}
