# PocketTV Premium Proxy

PocketTV Premium should not hardcode or publish the raw Dispatcharr M3U/EPG URLs. This Worker keeps those upstream URLs in Cloudflare secrets and exposes subscription-gated proxy URLs for the app.

## Endpoints

After deployment, configure PocketTV with:

```text
M3U: https://YOUR_WORKER_DOMAIN/premium/m3u?session=SHORT_LIVED_SESSION_TOKEN
EPG: https://YOUR_WORKER_DOMAIN/premium/epg?session=SHORT_LIVED_SESSION_TOKEN
```

The app gets those URLs by redeeming an 8-character activation code with the signup API. Sessions expire and can be renewed by redeeming the activation code again.

The M3U response rewrites stream URLs through:

```text
https://YOUR_WORKER_DOMAIN/premium/stream?u=...
```

Logo and XMLTV icon URLs are rewritten through:

```text
https://YOUR_WORKER_DOMAIN/premium/asset?u=...
```

The `u` value is encrypted with `PROXY_URL_SECRET`, so the app-visible playlist and EPG do not expose reversible upstream stream or logo URLs.

## Deploy

```bash
cd workers/premium-proxy
npm install
npx wrangler secret put UPSTREAM_M3U_URL
npx wrangler secret put UPSTREAM_EPG_URL
npx wrangler secret put PROXY_URL_SECRET
npx wrangler secret put DISPATCHARR_PROXY_SECRET
npm run deploy
```

Use the Dispatcharr M3U URL as `UPSTREAM_M3U_URL` and the Dispatcharr XMLTV URL as `UPSTREAM_EPG_URL`. Do not commit those values.

`PROXY_URL_SECRET` should be a long random value. It is used only by the proxy Worker to encrypt and decrypt rewritten target URLs.

`DISPATCHARR_PROXY_SECRET` is sent to upstream Dispatcharr requests as `X-PocketTV-Proxy-Secret`. Add a WAF Skip rule on the Dispatcharr zone that matches this header before any broad country block.

## Monetization Path

Use Stripe or Lemon Squeezy on an external website:

1. User subscribes on the website.
2. Backend creates an 8-character PocketTV activation code.
3. PocketTV redeems the activation code for short-lived M3U and EPG session URLs.
4. Worker validates the session before returning M3U/EPG/stream/asset responses.
5. Canceling a subscription revokes the activation code and prevents new sessions.

## Rights Note

Only sell access to channels and guide data you have rights to redistribute. The proxy protects URLs and access control, but it does not solve content licensing.
